#!/bin/bash
# Build script for SnowLoader (Linux/macOS)

set -e

echo "========================================"
echo "SnowLoader Build Script"
echo "========================================"
echo ""

# Check if CMake is installed
if ! command -v cmake &> /dev/null; then
    echo "CMake not found. Please install CMake first."
    echo "Ubuntu/Debian: sudo apt-get install cmake"
    echo "macOS: brew install cmake"
    exit 1
fi

# Create build directory
if [ ! -d "build" ]; then
    echo "Creating build directory..."
    mkdir build
fi

cd build

# Configure
echo "Configuring CMake..."
cmake .. -DCMAKE_BUILD_TYPE=Release

# Build
echo ""
echo "Building project..."
cmake --build . --parallel

# Success
echo ""
echo "========================================"
echo "Build completed successfully!"
echo "========================================"
echo ""
echo "Output location: $(pwd)/bin/SnowLoader.asi"
echo "Runtime folder:  $(pwd)/../assets/SnowLoader"
