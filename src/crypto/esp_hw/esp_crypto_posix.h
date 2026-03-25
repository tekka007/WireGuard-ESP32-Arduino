/*
 * ESP32 Hardware Crypto Acceleration for WireGuard
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * This module provides hardware-accelerated implementations of:
 * - X25519 key exchange (via esp_crypto)
 * - ChaCha20-Poly1305 AEAD
 * - BLAKE2s hashing
 *
 * Requires ESP32 V3+ or ESP32-S3 with cryptographic accelerators.
 * Falls back to software crypto if hardware not available.
 */

#ifndef ESP_CRYPTO_POSIX_H
#define ESP_CRYPTO_POSIX_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Detect if we have hardware crypto
#if defined(CONFIG_WIREGUARD_USE_HW_CRYPTO) && \
    (defined(ESP32) && (ESP_IDF_VERSION_MAJOR >= 4) || defined(ESP32S3))

#define WG_HW_CRYPTO_AVAILABLE 1

#include "esp_crypto.h"
#include "esp_aes.h"
#include "esp_chacha20.h"

// Hardware-accelerated ChaCha20-Poly1305
int chacha20poly1305_encrypt_hw(
    uint8_t *dst, const uint8_t *src, size_t src_len,
    const uint8_t *ad, size_t ad_len,
    const uint8_t *nonce, const uint8_t *key);

int chacha20poly1305_decrypt_hw(
    uint8_t *dst, const uint8_t *src, size_t src_len,
    const uint8_t *ad, size_t ad_len,
    const uint8_t *nonce, const uint8_t *key);

// Hardware-accelerated X25519 (if available via mbedTLS)
int x25519_hw(uint8_t[32], const uint8_t[32], const uint8_t[32]);

// BLAKE2s - no direct HW, but can use esp_sha for blake2s? (not available)
// So BLAKE2s remains software-only

#else
#define WG_HW_CRYPTO_AVAILABLE 0
#endif

/**
 * Initialize hardware crypto accelerators
 */
void esp_crypto_hw_init(void);

/**
 * Check if hardware crypto acceleration is available and enabled
 */
int esp_crypto_hw_available(void);

#ifdef __cplusplus
}
#endif

#endif /* ESP_CRYPTO_POSIX_H */
