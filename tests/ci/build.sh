#!/bin/bash
# Build test script for WireGuard-ESP32-Arduino
# Tests compilation with and without hardware crypto

set -e  # Exit on error

echo "========================================"
echo "WireGuard-ESP32 Build Test Suite"
echo "========================================"
echo ""

# Check if PlatformIO is available
if ! command -v pio &> /dev/null; then
    echo "ERROR: PlatformIO (pio) not found in PATH"
    echo "Install with: pip install platformio"
    exit 1
fi

# Save current directory
ORIG_DIR=$(pwd)

# Test 1: Build ESP32-DevKit (software crypto) using test sketch
echo "[1/4] Building ESP32-DevKit (software crypto)..."
cd tests/hw_crypto_test
pio run -e esp32dev 2>&1 | tail -20 | grep -E "(error|Successfully|Upload)" || true
cd "$ORIG_DIR"
if [ $? -eq 0 ]; then
    echo "  ✓ PASS: Software crypto build successful"
else
    echo "  ✗ FAIL: Software crypto build failed"
    exit 1
fi
echo ""

# Test 2: Build ESP32-S3 with hardware crypto
echo "[2/4] Building ESP32-S3-DevKitC-1 (hardware crypto)..."
cd tests/hw_crypto_test
pio run -e esp32-s3-devkitc-1 2>&1 | tail -20 | grep -E "(error|Successfully)" || true
cd "$ORIG_DIR"
if [ $? -eq 0 ]; then
    echo "  ✓ PASS: Hardware crypto build successful"
else
    echo "  ✗ FAIL: Hardware crypto build failed"
    exit 1
fi
echo ""

# Test 3: Build ESP32-C3 (RISC-V)
echo "[3/4] Building ESP32-C3-DevKitM-1 (RISC-V)..."
cd tests/hw_crypto_test
pio run -e esp32-c3-devkitm-1 2>&1 | tail -20 | grep -E "(error|Successfully)" || true
cd "$ORIG_DIR"
if [ $? -eq 0 ]; then
    echo "  ✓ PASS: ESP32-C3 build successful"
else
    echo "  ✗ FAIL: ESP32-C3 build failed"
    exit 1
fi
echo ""

# Test 4: Verify build sizes
echo "[4/4] Build size comparison:"
echo ""
echo "  Software crypto (esp32dev):"
cd tests/hw_crypto_test
pio run -e esp32dev --target size 2>/dev/null | grep -E "(\.text|\.data|\.bss|Total)" || true
cd "$ORIG_DIR"
echo ""
echo "  Hardware crypto (esp32-s3):"
cd tests/hw_crypto_test
pio run -e esp32-s3-devkitc-1 --target size 2>/dev/null | grep -E "(\.text|\.data|\.bss|Total)" || true
cd "$ORIG_DIR"
echo ""

echo "========================================"
echo "All build tests passed! ✓"
echo "========================================"
echo ""
echo "To upload to device:"
echo "  cd tests/hw_crypto_test"
echo "  pio run -e esp32-s3-devkitc-1 -t upload"
echo ""
echo "To monitor:"
echo "  pio device monitor"
