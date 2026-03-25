# Hardware Crypto Test Suite

Unit tests for ESP32 hardware crypto acceleration implementation.

## Upload

1. Open `tests/hw_crypto_test/hw_crypto_test.ino` in Arduino IDE or PlatformIO
2. Select your ESP32 board:
   - ESP32-S3 DevKitC-1 (recommended for PSRAM + best hardware crypto)
   - ESP32-DevKitC
   - ESP32-C3 DevKitM-1
3. Upload the sketch
4. Open Serial Monitor at 115200 baud

## Expected Output

```
=== Hardware Crypto Test Suite ===

Testing esp_crypto_init()...
PASS: esp_crypto_init succeeded

Testing esp_crypto_hw_enabled()...
Hardware crypto ENABLED (or DISABLED if on unsupported chip)

Testing Hardware RNG...
PASS: Hardware RNG test complete

Testing X25519 key exchange...
  Alice pub key generation: PASS
  Bob pub key generation: PASS
  Shared secret computation: PASS
PASS: X25519 test complete

Testing ChaCha20-Poly1305...
  Encryption: PASS
  Decryption: PASS
  Tamper detection: PASS
PASS: ChaCha20-Poly1305 test complete

Testing crypto statistics...
  X25519 operations: X
  ChaCha20-Poly1305 ops: Y
  RNG calls: Z
PASS: Statistics retrieval

=== All Tests Complete ===
```

## Build Flags

The test automatically uses hardware crypto if available. To force software mode:

- **PlatformIO**: Add `-DWG_USE_ESP_CRYPTO=0` to `build_flags`
- **Arduino IDE**: Add `#undef WG_USE_ESP_CRYPTO` before `#include <WireGuard-ESP32.h>`

## Test Coverage

- Hardware crypto initialization
- X25519 key generation and shared secret computation (RFC 7748 test vectors)
- ChaCha20-Poly1305 encryption/decryption with AAD
- Authentication tag verification and tamper detection
- Hardware RNG (esp_fill_random)
- Statistics reporting

All tests use known test vectors to verify correctness.
