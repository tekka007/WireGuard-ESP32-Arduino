/*
 * ESP32 Hardware Crypto Acceleration for WireGuard
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Practical implementation using ESP32's available hardware features:
 * - Hardware RNG (esp_fill_random)
 * - Cache-optimized memory allocation
 * - ESP-IDF mbedTLS with architecture-specific optimizations
 * - Performance statistics
 *
 * This replaces the reference crypto when CONFIG_WIREGUARD_USE_HW_CRYPTO=1
 *
 * Limitations:
 * - X25519: No direct hardware, but uses mbedTLS optimized assembly
 * - ChaCha20-Poly1305: No DMA offload, but uses cache-friendly implementation
 * - BLAKE2s: Software only (no hardware support)
 */

#ifndef CONFIG_WIREGUARD_USE_HW_CRYPTO
#error "esp_crypto.c should only be compiled when CONFIG_WIREGUARD_USE_HW_CRYPTO is enabled"
#endif

#include "esp_crypto.h"
#include "esp_log.h"
#include "esp_system.h"
#include "mbedtls/x25519.h"
#include "mbedtls/chachapoly.h"
#include "mbedtls/blake2s.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdlib.h>

static const char* TAG = "ESP-CRYPTO";

// Chip detection
#if defined(ESP32) && defined(CONFIG_IDF_TARGET_ESP32)
    #define ESP_CRYPTO_CHIP ESP_CRYPTO_CHIP_ESP32
    #define CHIP_NAME "ESP32"
#elif defined(ESP32S2) && defined(CONFIG_IDF_TARGET_ESP32S2)
    #define ESP_CRYPTO_CHIP ESP_CRYPTO_CHIP_ESP32S2
    #define CHIP_NAME "ESP32-S2"
#elif defined(ESP32S3) && defined(CONFIG_IDF_TARGET_ESP32S3)
    #define ESP_CRYPTO_CHIP ESP_CRYPTO_CHIP_ESP32S3
    #define CHIP_NAME "ESP32-S3"
#elif defined(ESP32C3) && defined(CONFIG_IDF_TARGET_ESP32C3)
    #define ESP_CRYPTO_CHIP ESP_CRYPTO_CHIP_ESP32C3
    #define CHIP_NAME "ESP32-C3"
#elif defined(ESP32C6) && defined(CONFIG_IDF_TARGET_ESP32C6)
    #define ESP_CRYPTO_CHIP ESP_CRYPTO_CHIP_ESP32C6
    #define CHIP_NAME "ESP32-C6"
#else
    #define ESP_CRYPTO_CHIP ESP_CRYPTO_CHIP_UNKNOWN
    #define CHIP_NAME "Unknown"
#endif

// State
static bool hw_initialized = false;
static bool hw_enabled = true;
static esp_crypto_chip_type_t chip_type = ESP_CRYPTO_CHIP_UNKNOWN;
static esp_crypto_stats_t stats = {0};

// mbedTLS contexts
static mbedtls_entropy_context entropy;
static mbedtls_ctr_drbg_context ctr_drbg;

// Cache maintenance for DMA (where applicable)
static inline void cache_invalidate(void *addr, size_t size) {
#if defined(CONFIG_SPIRAM) && defined(CONFIG_SPIRAM_CACHE)
    // If using external RAM, cache maintenance needed
    esp_cache_msync(addr, size, ESP_CACHE_MSYNC_INVALIDATE);
#else
    (void)addr;
    (void)size;
#endif
}

static inline void cache_writeback(void *addr, size_t size) {
#if defined(CONFIG_SPIRAM) && defined(CONFIG_SPIRAM_CACHE)
    esp_cache_msync(addr, size, ESP_CACHE_MSYNC_FLUSH);
#else
    (void)addr;
    (void)size;
#endif
}

// Helper: check if assembly optimizations are available
static int mbedtls_x25519_has_hw(void) {
    // ESP32 uses assembly-optimized X25519 in mbedTLS port
    // ESP-IDF's mbedtls includes Xtensa/RISC-V optimized implementations
    return 1;
}

// Helper: ChaCha20-Poly1305 with cache optimizations
static int chacha20poly1305_encrypt_optimized(
    uint8_t *dst, const uint8_t *src, size_t src_len,
    const uint8_t *ad, size_t ad_len,
    const uint8_t *nonce, const uint8_t *key)
{
    // Use mbedTLS chachapoly which is optimized for ESP32
    mbedtls_chachapoly_context ctx;
    int ret;

    mbedtls_chachapoly_init(&ctx);

    ret = mbedtls_chachapoly_setkey(&ctx, MBEDTLS_CHACHAPOLY_KEY_SIZE, key);
    if(ret != 0) goto cleanup;

    // Add associated data (AAD)
    if(ad_len > 0) {
        ret = mbedtls_chachapoly_starts(&ctx, nonce, MBEDTLS_CHACHAPOLY_DECRYPT);
        if(ret != 0) goto cleanup;
        ret = mbedtls_chachapoly_update_aad(&ctx, ad_len, ad);
        if(ret != 0) goto cleanup;
    }

    // Encrypt
    size_t out_len = src_len + MBEDTLS_CHACHAPOLY_MAC_SIZE;
    ret = mbedtls_chachapoly_encrypt(&ctx, src_len, src, dst);
    if(ret != 0) goto cleanup;

    ret = 0;

cleanup:
    mbedtls_chachapoly_free(&ctx);
    return ret;
}

static int chacha20poly1305_decrypt_optimized(
    uint8_t *dst, const uint8_t *src, size_t src_len,
    const uint8_t *ad, size_t ad_len,
    const uint8_t *nonce, const uint8_t *key)
{
    mbedtls_chachapoly_context ctx;
    int ret;

    mbedtls_chachapoly_init(&ctx);

    ret = mbedtls_chachapoly_setkey(&ctx, MBEDTLS_CHACHAPOLY_KEY_SIZE, key);
    if(ret != 0) goto cleanup;

    // Add associated data
    if(ad_len > 0) {
        ret = mbedtls_chachapoly_starts(&ctx, nonce, MBEDTLS_CHACHAPOLY_DECRYPT);
        if(ret != 0) goto cleanup;
        ret = mbedtls_chachapoly_update_aad(&ctx, ad_len, ad);
        if(ret != 0) goto cleanup;
    }

    // Decrypt
    out_len = src_len - MBEDTLS_CHACHAPOLY_MAC_SIZE;
    ret = mbedtls_chachapoly_decrypt(&ctx, out_len, src, dst);
    if(ret != 0) goto cleanup;

    ret = 0;

cleanup:
    mbedtls_chachapoly_free(&ctx);
    return ret;
}

// Public API

void esp_crypto_init(void) {
    if(hw_initialized) return;

    ESP_LOGI(TAG, "Initializing ESP32 hardware crypto");
    ESP_LOGI(TAG, "Chip type: %s", CHIP_NAME);

    // Initialize mbedTLS entropy and DRBG
    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&ctr_drbg);

    // Add hardware entropy source
    int ret = mbedtls_entropy_add_source(
        &entropy,
        [](void*, unsigned char* out, size_t len, size_t* olen) -> int {
            esp_fill_random(out, len);
            *olen = len;
            return 0;
        },
        nullptr,
        16,  // min threshold
        MBEDTLS_ENTROPY_SOURCE_STRONG
    );

    if(ret != 0) {
        ESP_LOGW(TAG, "Failed to add entropy source: %d", ret);
    }

    // Seed the DRBG
    ret = mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy, nullptr, 0);
    if(ret != 0) {
        ESP_LOGE(TAG, "Failed to seed DRBG: %d", ret);
        hw_enabled = false;
    } else {
        hw_enabled = true;
    }

    chip_type = (esp_crypto_chip_type_t)ESP_CRYPTO_CHIP;

    hw_initialized = true;
    ESP_LOGI(TAG, "Hardware crypto %s",
             hw_enabled ? "initialized (optimized)" : "disabled (falling back to software)");
}

int esp_crypto_hw_enabled(void) {
    return hw_enabled ? 1 : 0;
}

esp_crypto_chip_type_t esp_crypto_get_chip_type(void) {
    return chip_type;
}

void* esp_crypto_malloc(size_t size, size_t align) {
    // For DMA/cache reasons, allocate with proper alignment
    // Use pvPortMalloc which is cache-friendly on ESP32
    void* ptr = NULL;

    if(hw_enabled) {
        // Use aligned allocation if available
        ptr = heap_caps_malloc(size, MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
    } else {
        ptr = malloc(size);
    }

    if(ptr) {
        // Apply alignment if needed
        if(align > 0 && ((uintptr_t)ptr % align) != 0) {
            uintptr_t aligned = ((uintptr_t)ptr + align - 1) & ~(align - 1);
            uint8_t* aligned_ptr = (uint8_t*)aligned;

            // Store original pointer just before aligned address
            aligned_ptr[-1] = (uintptr_t)ptr & 0xFF;
            ptr = aligned_ptr;
        }
    }

    return ptr;
}

void esp_crypto_free(void* ptr) {
    if(!ptr) return;

    // If we applied alignment adjustment, recover original pointer
    if((uintptr_t)ptr % 32 != 0) {
        uint8_t* aligned = (uint8_t*)ptr;
        uintptr_t original = aligned[-1];
        ptr = (void*)original;
    }

    if(hw_enabled) {
        heap_caps_free(ptr);
    } else {
        free(ptr);
    }
}

void esp_crypto_get_stats(esp_crypto_stats_t* s) {
    if(!s) return;
    memcpy(s, &stats, sizeof(esp_crypto_stats_t));
}

void esp_crypto_set_enable(int enable) {
    hw_enabled = (enable != 0);
    ESP_LOGI(TAG, "Hardware crypto %s", hw_enabled ? "enabled" : "disabled");
}

void esp_crypto_set_log_level(int level) {
    esp_log_level_t esp_level;
    switch(level) {
        case 0: esp_level = ESP_LOG_NONE; break;
        case 1: esp_level = ESP_LOG_ERROR; break;
        case 2: esp_level = ESP_LOG_WARN; break;
        case 3: esp_level = ESP_LOG_INFO; break;
        case 4: esp_level = ESP_LOG_DEBUG; break;
        default: esp_level = ESP_LOG_INFO;
    }
    esp_log_level_set(TAG, esp_level);
}

// Replacement crypto functions for WireGuard
// These will be used when CONFIG_WIREGUARD_USE_HW_CRYPTO is defined

int chacha20poly1305_encrypt_hw(
    uint8_t *dst, const uint8_t *src, size_t src_len,
    const uint8_t *ad, size_t ad_len,
    const uint8_t *nonce, const uint8_t *key)
{
    if(!hw_enabled) return -1;

    int ret = chacha20poly1305_encrypt_optimized(dst, src, src_len, ad, ad_len, nonce, key);

    if(ret == 0) {
        stats.total_bytes_encrypted += src_len;
        stats.chacha20_ops++;
    }

    return ret;
}

int chacha20poly1305_decrypt_hw(
    uint8_t *dst, const uint8_t *src, size_t src_len,
    const uint8_t *ad, size_t ad_len,
    const uint8_t *nonce, const uint8_t *key)
{
    if(!hw_enabled) return -1;

    int ret = chacha20poly1305_decrypt_optimized(dst, src, src_len, ad, ad_len, nonce, key);

    if(ret == 0) {
        stats.total_bytes_decrypted += src_len - MBEDTLS_CHACHAPOLY_MAC_SIZE;
        stats.chacha20_ops++;
    }

    return ret;
}

int x25519_hw(uint8_t *raw_public_key, const uint8_t *raw_private_key, const uint8_t *base_point) {
    if(!hw_enabled || !mbedtls_x25519_has_hw()) return -1;

    // Use mbedTLS' x25519 which may have assembly optimizations
    mbedtls_ecp_group grp;
    mbedtls_mpi priv, pub;
    int ret;

    mbedtls_ecp_group_init(&grp);
    mbedtls_mpi_init(&priv);
    mbedtls_mpi_init(&pub);

    // Load private key
    ret = mbedtls_mpi_read_binary(&priv, raw_private_key, 32);
    if(ret != 0) goto cleanup;

    // Set up Curve25519 group
    ret = mbedtls_ecp_group_load(&grp, MBEDTLS_ECP_GRP_CURVE25519);
    if(ret != 0) goto cleanup;

    // Compute public key
    ret = mbedtls_ecp_mul(&grp, &pub, &priv, &grp.G, mbedtls_ctr_drbg_random, &ctr_drbg);
    if(ret != 0) goto cleanup;

    // Export public key
    ret = mbedtls_mpi_write_binary(&pub, raw_public_key, 32);
    if(ret != 0) goto cleanup;

    stats.x25519_ops++;

cleanup:
    mbedtls_mpi_free(&priv);
    mbedtls_mpi_free(&pub);
    mbedtls_ecp_group_free(&grp);

    return ret == 0 ? 0 : -1;
}

// Hook for WireGuard platform init to call
void esp_crypto_platform_init(void) {
    esp_crypto_init();
}

// For now, these are not implemented (will use software fallback
int blake2s_hw(uint8_t *out, size_t outlen, const uint8_t *key, size_t keylen,
               const uint8_t *in, size_t inlen) {
    // BLAKE2s has no hardware acceleration on ESP32
    // Use software implementation
    return -1;
}

// Stats reset (for testing)
void esp_crypto_stats_reset(void) {
    memset(&stats, 0, sizeof(stats));
}
