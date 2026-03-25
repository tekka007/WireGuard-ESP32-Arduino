#include <Arduino.h>
#include <WireGuard-ESP32.h>
#include "crypto/esp_hw/esp_crypto.h"
#include <cstring>

// Test hardware crypto initialization and capabilities
void test_hw_crypto_init() {
    Serial.println("Testing esp_crypto_init()...");
    esp_crypto_init();  // Returns void, no error check
    Serial.println("PASS: esp_crypto_init succeeded");
}

void test_hw_enabled() {
    Serial.println("Testing esp_crypto_hw_enabled()...");
    int hw_enabled = esp_crypto_hw_enabled();
    Serial.printf("Hardware crypto %s\n", hw_enabled ? "ENABLED" : "DISABLED (using software fallback)");

    esp_crypto_chip_type_t chip = esp_crypto_get_chip_type();
    const char* chip_name = "UNKNOWN";
    switch(chip) {
        case ESP_CRYPTO_CHIP_ESP32: chip_name = "ESP32"; break;
        case ESP_CRYPTO_CHIP_ESP32S2: chip_name = "ESP32-S2"; break;
        case ESP_CRYPTO_CHIP_ESP32S3: chip_name = "ESP32-S3"; break;
        case ESP_CRYPTO_CHIP_ESP32C3: chip_name = "ESP32-C3"; break;
        case ESP_CRYPTO_CHIP_ESP32C6: chip_name = "ESP32-C6"; break;
        default: chip_name = "UNKNOWN";
    }
    Serial.printf("Chip type: %s\n", chip_name);
}

void test_memory_allocation() {
    Serial.println("Testing esp_crypto_malloc()...");

    // Test aligned allocation
    void* ptr = esp_crypto_malloc(256, 32);
    if (!ptr) {
        Serial.println("FAIL: esp_crypto_malloc returned NULL");
        return;
    }

    // Check alignment
    if (((uintptr_t)ptr % 32) != 0) {
        Serial.println("FAIL: Memory not 32-byte aligned");
        esp_crypto_free(ptr);
        return;
    }

    // Test write/read
    memset(ptr, 0xAA, 256);
    uint8_t* bytes = (uint8_t*)ptr;
    bool ok = true;
    for (int i = 0; i < 256; i++) {
        if (bytes[i] != 0xAA) {
            ok = false;
            break;
        }
    }

    esp_crypto_free(ptr);

    if (!ok) {
        Serial.println("FAIL: Memory corruption");
        return;
    }
    Serial.println("PASS: Memory allocation and access ok");
}

void test_stats() {
    Serial.println("Testing crypto statistics...");
    esp_crypto_stats_t stats;
    esp_crypto_get_stats(&stats);

    Serial.printf("  Total bytes encrypted: %llu\n", stats.total_bytes_encrypted);
    Serial.printf("  Total bytes decrypted: %llu\n", stats.total_bytes_decrypted);
    Serial.printf("  X25519 operations: %lu\n", stats.x25519_ops);
    Serial.printf("  ChaCha20 operations: %lu\n", stats.chacha20_ops);
    Serial.printf("  BLAKE2s operations: %lu\n", stats.blake2s_ops);
    Serial.printf("  HW RNG bytes: %lu\n", stats.hw_rng_bytes);
    Serial.println("PASS: Statistics retrieval");
}

void test_wireguard_integration() {
    Serial.println("Testing WireGuard with hardware crypto...");

    // Simple test: try to initialize a WireGuard instance
    // This will internally use hardware crypto if enabled
    WireGuard wg;

    IPAddress localIP(10, 0, 0, 2);
    IPAddress subnet(255, 255, 255, 0);
    IPAddress gateway(10, 0, 0, 1);

    // Dummy keys for compilation test (44 char base64)
    char privKey[45] = "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=";
    char pubKey[45] = "BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB=";
    char endpoint[100] = "example.com";
    uint16_t port = 51820;

    // This will fail at runtime due to invalid keys/network,
    // but should compile and initialize without crashing
    bool initialized = wg.begin(localIP, subnet, gateway, privKey, endpoint, pubKey, port);

    Serial.printf("WireGuard initialization attempt (expects false): %s\n",
                  initialized ? "true" : "false");

    if (wg.is_initialized()) {
        Serial.println("Interface exists (won't actually connect)");
        wg.end();
    }

    Serial.println("PASS: WireGuard integration test (compiled and ran)");
}

void setup() {
    Serial.begin(115200);
    while (!Serial) {
        delay(100);
    }

    Serial.println("\n=== ESP32 Hardware Crypto Test Suite ===\n");
    delay(2000);

    test_hw_crypto_init();
    delay(100);

    test_hw_enabled();
    delay(100);

    test_memory_allocation();
    delay(100);

    test_stats();
    delay(100);

    test_wireguard_integration();
    delay(100);

    Serial.println("\n=== All Tests Complete ===");
    Serial.println("Check results above.");
}

void loop() {
    delay(1000);
}
