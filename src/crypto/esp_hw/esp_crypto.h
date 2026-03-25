/*
 * ESP32 Hardware Crypto Acceleration for WireGuard
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * This module provides optimized crypto operations for ESP32:
 * - Hardware RNG (already used via esp_fill_random)
 * - Cache-optimized memory allocation for DMA
 * - Xtensa instruction set optimizations
 * - mbedTLS with ESP32-specific optimizations
 *
 * Supports: ESP32 (v2+), ESP32-S2, ESP32-S3, ESP32-C3, ESP32-C6
 */

#ifndef ESP_CRYPTO_H
#define ESP_CRYPTO_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * ESP32 Chip Types for Crypto Features
 */
typedef enum {
    ESP_CRYPTO_CHIP_UNKNOWN = 0,
    ESP_CRYPTO_CHIP_ESP32,       // Original ESP32 (Xtensa LX6)
    ESP_CRYPTO_CHIP_ESP32S2,     // ESP32-S2 (Xtensa LX7)
    ESP_CRYPTO_CHIP_ESP32S3,     // ESP32-S3 (Xtensa LX7 with vector instructions)
    ESP_CRYPTO_CHIP_ESP32C3,     // ESP32-C3 (RISC-V)
    ESP_CRYPTO_CHIP_ESP32C6,     // ESP32-C6 (RISC-V)
} esp_crypto_chip_type_t;

/**
 * Initialize hardware crypto subsystem
 * Call this once at startup before any crypto operations
 */
void esp_crypto_init(void);

/**
 * Check if hardware acceleration is available
 * @return 1 if HW acceleration enabled, 0 otherwise
 */
int esp_crypto_hw_enabled(void);

/**
 * Get chip type
 * @return Chip type enumeration
 */
esp_crypto_chip_type_t esp_crypto_get_chip_type(void);

/**
 * Allocate crypto-safe memory (DMA-capable, cache-optimized)
 * @param size Number of bytes to allocate
 * @param align Minimum alignment (typically 16 or 32 bytes)
 * @return Pointer to aligned memory, or NULL on failure
 *
 * For optimal performance, allocate buffers with cache-friendly alignment.
 * ESP32 has separate instruction and data cache, so DMA operations need
 * cache maintenance. This function ensures proper cache behavior.
 */
void* esp_crypto_malloc(size_t size, size_t align);

/**
 * Free crypto-safe memory
 */
void esp_crypto_free(void* ptr);

/**
 * Get performance statistics
 * Returns bytes processed, operations count, etc.
 */
typedef struct {
    uint64_t total_bytes_encrypted;
    uint64_t total_bytes_decrypted;
    uint32_t chacha20_ops;
    uint32_t x25519_ops;
    uint32_t blake2s_ops;
    uint32_t hw_rng_bytes;
} esp_crypto_stats_t;

void esp_crypto_get_stats(esp_crypto_stats_t* stats);

/**
 * Enable/disable hardware acceleration at runtime
 * @param enable 1 to enable, 0 to disable
 *
 * This is primarily for testing/debugging. Default is enabled
 * if the chip supports it.
 */
void esp_crypto_set_enable(int enable);

/**
 * Set log verbosity
 * @param level 0=none, 1=error, 2=warn, 3=info, 4=debug
 */
void esp_crypto_set_log_level(int level);

#ifdef __cplusplus
}
#endif

#endif /* ESP_CRYPTO_H */
