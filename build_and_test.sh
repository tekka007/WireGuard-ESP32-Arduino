#!/bin/bash
# WireGuard ESP32 Library - Build and Test Script
# Usage: ./build_and_test.sh [board]
# Example: ./build_and_test.sh esp32dev
#          ./build_and_test.sh esp32-s3-devkitc-1

set -e

BOARD="${1:-esp32dev}"
PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

echo "========================================"
echo "WireGuard ESP32 Build & Test"
echo "========================================"
echo "Board: $BOARD"
echo "Project: $PROJECT_DIR"
echo ""

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Check if PlatformIO is installed
if ! command -v pio &> /dev/null; then
    echo -e "${RED}ERROR: PlatformIO not found!${NC}"
    echo "Install: pip install platformio"
    exit 1
fi

# Build
echo -e "${YELLOW}[1/3] Building...${NC}"
if pio run -e "$BOARD"; then
    echo -e "${GREEN}✓ Build successful${NC}"
else
    echo -e "${RED}✗ Build failed${NC}"
    exit 1
fi

# Run unit tests
echo ""
echo -e "${YELLOW}[2/3] Running unit tests...${NC}"
if pio test -e test; then
    echo -e "${GREEN}✓ All tests passed${NC}"
else
    echo -e "${RED}✗ Some tests failed${NC}"
    exit 1
fi

# Check code size
echo ""
echo -e "${YELLOW}[3/3] Code size analysis...${NC}"
pio run -e "$BOARD" -t size || true

echo ""
echo "========================================"
echo -e "${GREEN}Build & test complete!${NC}"
echo "========================================"
echo ""
echo "To flash:"
echo "  pio run -e $BOARD -t upload"
echo ""
echo "To monitor serial:"
echo "  pio device monitor"
echo ""
echo "Configuration options in:"
echo "  - Kconfig.projbuild (Arduino IDE)"
echo "  - platformio.ini (PlatformIO)"
echo ""
