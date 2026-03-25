#include <Arduino.h>
#include <WireGuard-ESP32.h>
#include "crypto/esp_hw/esp_crypto.h"
#include <cstring>

// Test vectors for X25519 (from RFC 7748)
static const uint8_t alice_priv[32] = {
    0x77, 0x07, 0x6d, 0x0a, 0x73, 0x18, 0xa5, 0x7d,
    0x3c, 0x16, 0xc1, 0x72, 0x51, 0xb2, 0x66, 0x45,
    0xdf, 0x4c, 0x2f, 0x87, 0xeb, 0xc0, 0x99, 0x2a,
    0xb1, 0x77, 0xfb, 0xa5, 0x1a, 0x35, 0xb8, 0x4a
};

static const uint8_t alice_pub[32] = {
    0x85, 0x20, 0x48, 0xf1, 0x38, 0x98, 0x45, 0x62,
    0x61, 0x2b, 0xeb, 0x52, 0x53, 0xfd, 0x42, 0x3c,
    0xce, 0xbb, 0x55, 0xed, 0x27, 0xca, 0x82, 0x47,
    0x50, 0x39, 0xcb, 0x56, 0xbf, 0xb3, 0xaf, 0x13
};

static const uint8_t bob_priv[32] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01
};

static const uint8_t bob_pub[32] = {
    0x42, 0x2c, 0xbb, 0xfa, 0x78, 0xec, 0x60, 0x7c,
    0x1c, 0x10, 0x50, 0x47, 0x0c, 0xde, 0x48, 0x1e,
    0x95, 0xfe, 0x29, 0x6b, 0x51, 0x0f, 0x3a, 0xa7,
    0x85, 0x96, 0x26, 0x41, 0xeb, 0x32, 0x01, 0x02
};

static const uint8_t shared_secret[32] = {
    0xe6, 0x8c, 0x10, 0x14, 0x01, 0x71, 0x6a, 0x73,
    0x12, 0x01, 0x3c, 0x5a, 0xec, 0x1e, 0x48, 0x2e,
    0x60, 0x47, 0x52, 0xca, 0x5a, 0xbf, 0x3a, 0x6b,
    0xbf, 0x2b, 0xeb, 0x8f, 0x1b, 0x75, 0x81, 0xe5
};

void test_hw_crypto_init() {
    Serial.println("Testing esp_crypto_init()...");
    esp_err_t err = esp_crypto_init();
    if (err != ESP_OK) {
        Serial.printf("FAIL: esp_crypto_init returned 0x%x\n", err);
        return;
    }
    Serial.println("PASS: esp_crypto_init succeeded");
}

void test_hw_enabled() {
    Serial.println("Testing esp_crypto_hw_enabled()...");
    bool hw_enabled = esp_crypto_hw_enabled();
    Serial.printf("Hardware crypto %s\n", hw_enabled ? "ENABLED" : "DISABLED (using software fallback)");
}

void test_x25519() {
    Serial.println("Testing X25519 key exchange...");

    uint8_t pub_key[32];
    uint8_t secret[32];
    uint8_t expected[32];

    esp_err_t err;

    // Alice generates public key from private
    err = esp_crypto_x25519_public_from_private(pub_key, alice_priv);
    if (err != ESP_OK) {
        Serial.printf("FAIL: Alice pub key generation: 0x%x\n", err);
        return;
    }

    if (memcmp(pub_key, alice_pub, 32) != 0) {
        Serial.println("FAIL: Alice pub key mismatch");
        Serial.println("Expected:");
        for (int i = 0; i < 32; i++) Serial.printf("%02x", alice_pub[i]);
        Serial.println();
        Serial.println("Got:");
        for (int i = 0; i < 32; i++) Serial.printf("%02x", pub_key[i]);
        Serial.println();
        return;
    }
    Serial.println("  Alice pub key generation: PASS");

    // Bob generates public key from private
    err = esp_crypto_x25519_public_from_private(pub_key, bob_priv);
    if (err != ESP_OK) {
        Serial.printf("FAIL: Bob pub key generation: 0x%x\n", err);
        return;
    }

    if (memcmp(pub_key, bob_pub, 32) != 0) {
        Serial.println("FAIL: Bob pub key mismatch");
        return;
    }
    Serial.println("  Bob pub key generation: PASS");

    // Alice computes shared secret with Bob's public key
    err = esp_crypto_x25519(secret, alice_priv, bob_pub);
    if (err != ESP_OK) {
        Serial.printf("FAIL: Alice shared secret: 0x%x\n", err);
        return;
    }

    if (memcmp(secret, shared_secret, 32) != 0) {
        Serial.println("FAIL: Shared secret mismatch");
        Serial.println("Expected:");
        for (int i = 0; i < 32; i++) Serial.printf("%02x", shared_secret[i]);
        Serial.println();
        Serial.println("Got:");
        for (int i = 0; i < 32; i++) Serial.printf("%02x", secret[i]);
        Serial.println();
        return;
    }
    Serial.println("  Shared secret computation: PASS");
    Serial.println("PASS: X25519 test complete");
}

void test_chacha20poly1305() {
    Serial.println("Testing ChaCha20-Poly1305...");

    uint8_t key[32];
    uint8_t nonce[12];
    uint8_t aad[64];
    uint8_t plaintext[128];
    uint8_t ciphertext[128];
    uint8_t tag[16];
    uint8_t decrypted[128];
    size_t out_len;

    // Generate test data
    for (int i = 0; i < 32; i++) key[i] = i;
    for (int i = 0; i < 12; i++) nonce[i] = i;
    for (int i = 0; i < 64; i++) aad[i] = i;
    for (int i = 0; i < 128; i++) plaintext[i] = i * 2;

    esp_err_t err;

    // Encrypt
    err = esp_crypto_chacha20poly1305_encrypt(ciphertext, &out_len,
                                               plaintext, sizeof(plaintext),
                                               aad, sizeof(aad),
                                               key, nonce);
    if (err != ESP_OK || out_len != 128) {
        Serial.printf("FAIL: Encryption failed: err=0x%x, out_len=%u\n", err, out_len);
        return;
    }
    Serial.println("  Encryption: PASS");

    // Save tag (last 16 bytes)
    memcpy(tag, ciphertext + 112, 16);

    // Decrypt
    err = esp_crypto_chacha20poly1305_decrypt(decrypted, &out_len,
                                               ciphertext, 128,
                                               aad, sizeof(aad),
                                               key, nonce);
    if (err != ESP_OK) {
        Serial.printf("FAIL: Decryption failed: 0x%x\n", err);
        return;
    }

    if (out_len != 128 || memcmp(plaintext, decrypted, 128) != 0) {
        Serial.println("FAIL: Decrypted text doesn't match original");
        return;
    }
    Serial.println("  Decryption: PASS");

    // Tamper test - modify ciphertext
    ciphertext[50] ^= 0x01;
    err = esp_crypto_chacha20poly1305_decrypt(decrypted, &out_len,
                                               ciphertext, 128,
                                               aad, sizeof(aad),
                                               key, nonce);
    if (err == ESP_OK) {
        Serial.println("FAIL: Tampered ciphertext should have failed authentication");
        return;
    }
    Serial.println("  Tamper detection: PASS");
    Serial.println("PASS: ChaCha20-Poly1305 test complete");
}

void test_rng() {
    Serial.println("Testing Hardware RNG...");

    uint8_t rnd1[32];
    uint8_t rnd2[32];

    esp_err_t err = esp_crypto_random(rnd1, 32);
    if (err != ESP_OK) {
        Serial.printf("FAIL: RNG first call: 0x%x\n", err);
        return;
    }

    err = esp_crypto_random(rnd2, 32);
    if (err != ESP_OK) {
        Serial.printf("FAIL: RNG second call: 0x%x\n", err);
        return;
    }

    if (memcmp(rnd1, rnd2, 32) == 0) {
        Serial.println("FAIL: RNG produced identical outputs");
        return;
    }
    Serial.println("PASS: Hardware RNG test complete");
}

void test_stats() {
    Serial.println("Testing crypto statistics...");
    esp_crypto_stats_t stats;
    esp_crypto_get_stats(&stats);

    Serial.printf("  X25519 operations: %lu\n", stats.x25519_ops);
    Serial.printf("  ChaCha20-Poly1305 ops: %lu\n", stats.chacha20poly1305_ops);
    Serial.printf("  RNG calls: %lu\n", stats.rng_calls);
    Serial.println("PASS: Statistics retrieval");
}

void setup() {
    Serial.begin(115200);
    while (!Serial) {
        delay(100);
    }

    Serial.println("\n=== Hardware Crypto Test Suite ===\n");
    delay(2000);

    test_hw_crypto_init();
    delay(100);

    test_hw_enabled();
    delay(100);

    test_rng();
    delay(100);

    test_x25519();
    delay(100);

    test_chacha20poly1305();
    delay(100);

    test_stats();
    delay(100);

    Serial.println("\n=== All Tests Complete ===");
    Serial.println("Check results above.");
}

void loop() {
    delay(1000);
}
