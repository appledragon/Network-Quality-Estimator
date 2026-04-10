#!/bin/bash

# Exit on error
set -e

echo "============================================"
echo "   NQE macOS Build - CURL Setup Script"
echo "============================================"
echo ""

# Get the script directory
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
cd "$SCRIPT_DIR"

# Create third_party directory if it doesn't exist
mkdir -p third_party
cd third_party

# Check if CURL is already installed via Homebrew
if command -v brew &> /dev/null; then
    echo "[INFO] Homebrew detected"
    
    if brew list curl &> /dev/null; then
        echo "[INFO] CURL is already installed via Homebrew"
        CURL_PREFIX=$(brew --prefix curl)
        CURL_INCLUDE_DIR="$CURL_PREFIX/include"
        CURL_LIBRARY="$CURL_PREFIX/lib/libcurl.dylib"
        USE_SYSTEM_CURL=1
    else
        echo "[1/4] Installing CURL via Homebrew..."
        brew install curl
        CURL_PREFIX=$(brew --prefix curl)
        CURL_INCLUDE_DIR="$CURL_PREFIX/include"
        CURL_LIBRARY="$CURL_PREFIX/lib/libcurl.dylib"
        USE_SYSTEM_CURL=1
        echo "[SUCCESS] Installed CURL via Homebrew"
        echo ""
    fi
else
    # Homebrew not available, download and build from source
    echo "[INFO] Homebrew not found, will download CURL from source"
    USE_SYSTEM_CURL=0
    
    # Check if CURL is already downloaded
    if [ -d "curl" ]; then
        echo "[INFO] CURL directory already exists. Skipping download."
    else
        echo "[1/4] Downloading CURL source code..."
        echo ""
        
        CURL_VERSION="8.10.1"
        CURL_URL="https://curl.se/download/curl-${CURL_VERSION}.tar.gz"
        CURL_TAR="curl-${CURL_VERSION}.tar.gz"
        
        # Download CURL source
        curl -L -o "$CURL_TAR" "$CURL_URL"
        
        if [ $? -ne 0 ]; then
            echo "[ERROR] Failed to download CURL from curl.se"
            echo "Please check your internet connection and try again."
            exit 1
        fi
        
        echo "[SUCCESS] Downloaded CURL successfully"
        echo ""
        
        echo "[2/4] Extracting CURL..."
        echo ""
        
        # Extract the tar.gz file
        tar -xzf "$CURL_TAR"
        
        if [ $? -ne 0 ]; then
            echo "[ERROR] Failed to extract CURL"
            exit 1
        fi
        
        # Rename the extracted folder to "curl"
        mv "curl-${CURL_VERSION}" curl
        
        # Clean up tar file
        rm "$CURL_TAR"
        
        echo "[SUCCESS] Extracted CURL to third_party/curl"
        echo ""
        
        # Build CURL
        echo "[2.5/4] Building CURL from source..."
        echo ""
        
        cd curl
        
        # Configure CURL with SSL support (using system OpenSSL/LibreSSL)
        ./configure --prefix="$SCRIPT_DIR/third_party/curl-install" \
                    --with-ssl \
                    --disable-ldap \
                    --disable-ldaps \
                    --enable-ipv6 \
                    --without-brotli \
                    --without-zstd
        
        if [ $? -ne 0 ]; then
            echo "[ERROR] CURL configuration failed"
            exit 1
        fi
        
        # Build and install
        make -j$(sysctl -n hw.ncpu)
        make install
        
        if [ $? -ne 0 ]; then
            echo "[ERROR] CURL build failed"
            exit 1
        fi
        
        cd ..
        
        echo "[SUCCESS] Built and installed CURL"
        echo ""
    fi
    
    CURL_INCLUDE_DIR="$SCRIPT_DIR/third_party/curl-install/include"
    CURL_LIBRARY="$SCRIPT_DIR/third_party/curl-install/lib/libcurl.dylib"
fi

cd "$SCRIPT_DIR"

echo "[3/4] Configuring CMake with CURL..."
echo ""

# Detect Xcode and command line tools
if ! command -v xcodebuild &> /dev/null; then
    echo "[ERROR] Xcode Command Line Tools not found"
    echo "Please install with: xcode-select --install"
    exit 1
fi

XCODE_VERSION=$(xcodebuild -version | head -n 1)
echo "[INFO] Found $XCODE_VERSION"
echo ""

# Set up CURL paths
echo "[INFO] CURL Include: $CURL_INCLUDE_DIR"
echo "[INFO] CURL Library: $CURL_LIBRARY"
echo ""

# Clean and recreate build directory
if [ -d "build_macos" ]; then
    echo "[INFO] Cleaning existing build directory..."
    rm -rf build_macos
    echo "[INFO] Build directory cleaned"
fi
mkdir -p build_macos
cd build_macos

echo "[INFO] Configuring CMake with Xcode or Unix Makefiles..."
echo ""

# Detect if ninja is available
if command -v ninja &> /dev/null; then
    CMAKE_GENERATOR="Ninja"
    echo "[INFO] Using Ninja build system"
else
    CMAKE_GENERATOR="Unix Makefiles"
    echo "[INFO] Using Unix Makefiles"
fi

# Configure CMake with CURL paths
cmake -G "$CMAKE_GENERATOR" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCURL_INCLUDE_DIR="$CURL_INCLUDE_DIR" \
    -DCURL_LIBRARY="$CURL_LIBRARY" \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=10.15 \
    ..

if [ $? -ne 0 ]; then
    echo "[ERROR] CMake configuration failed"
    exit 1
fi

echo "[SUCCESS] CMake configuration completed"
echo ""

echo "[4/4] Building all targets..."
echo ""

# Build with parallel compilation
cmake --build . --config Release -j $(sysctl -n hw.ncpu)

if [ $? -ne 0 ]; then
    echo "[ERROR] Build failed"
    exit 1
fi

echo "[SUCCESS] Build completed successfully"
echo ""

# Copy or link CURL library if needed (for non-system CURL)
if [ $USE_SYSTEM_CURL -eq 0 ]; then
    echo "[INFO] Copying CURL library to output directory..."
    if [ -f "$SCRIPT_DIR/third_party/curl-install/lib/libcurl.4.dylib" ]; then
        cp "$SCRIPT_DIR/third_party/curl-install/lib/libcurl.4.dylib" . 2>/dev/null || true
    fi
fi

cd "$SCRIPT_DIR"

echo ""
echo "============================================"
echo "   Build Complete!"
echo "============================================"
echo ""
echo "Output directory: build_macos/Release or build_macos/"
echo ""
echo "Test executables:"
echo "  - feature_test"
echo "  - network_change_test"
echo "  - throughput_analyzer_test"
echo "  - libcurl_multi_nqe_example"
echo "  - advanced_features_demo"
echo "  - complete_features_demo"
echo "  - And more..."
echo ""
echo "To run tests: cd build_macos && ./feature_test"
echo ""

# Make the script executable
chmod +x "$0"

echo "[INFO] Setup script is now executable"
echo ""
