#!/bin/bash
# Build script for NQE Android native library
# Usage: ./build_android.sh [Release|Debug]

set -e

# Configuration
ANDROID_NDK="${ANDROID_NDK:-$HOME/Android/sdk/ndk/27.0.12077973}"
API_LEVEL=21
BUILD_TYPE="${1:-Release}"

# Check if NDK exists
if [ ! -d "$ANDROID_NDK" ]; then
    echo "Error: Android NDK not found at: $ANDROID_NDK"
    echo "Please set ANDROID_NDK environment variable or update the script"
    exit 1
fi

echo "========================================="
echo "Building NQE for Android"
echo "========================================="
echo "NDK:        $ANDROID_NDK"
echo "API Level:  $API_LEVEL"
echo "Build Type: $BUILD_TYPE"
echo "========================================="
echo ""

# Android ABIs to build
ABIS=("arm64-v8a" "armeabi-v7a" "x86_64" "x86")

# Build for each ABI
for ABI in "${ABIS[@]}"; do
    echo "Building for $ABI..."
    
    cmake -G "Unix Makefiles" \
          -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK/build/cmake/android.toolchain.cmake" \
          -DANDROID_ABI="$ABI" \
          -DANDROID_PLATFORM="android-$API_LEVEL" \
          -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
          -DCMAKE_MAKE_PROGRAM="$ANDROID_NDK/prebuilt/windows-x86_64/bin/make.exe" \
          -B "build/$ABI" \
          -S .
    
    cmake --build "build/$ABI" -j$(nproc 2>/dev/null || echo 4)
    
    echo "✓ $ABI build complete"
    echo ""
done

echo "========================================="
echo "Build Summary"
echo "========================================="

# Show built libraries
for ABI in "${ABIS[@]}"; do
    LIB_PATH="build/$ABI/libnqe.so"
    if [ -f "$LIB_PATH" ]; then
        SIZE=$(ls -lh "$LIB_PATH" | awk '{print $5}')
        echo "  $ABI: $LIB_PATH ($SIZE)"
    else
        echo "  $ABI: FAILED"
    fi
done

echo ""
echo "Build complete!"
echo ""
echo "To use in Android project, copy libraries from:"
echo "  build/*/libnqe.so"
echo "To your project's:"
echo "  app/src/main/jniLibs/<abi>/libnqe.so"
