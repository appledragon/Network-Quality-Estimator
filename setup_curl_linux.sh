#!/bin/bash

# Exit on error
set -e

echo "============================================"
echo "   NQE Linux Build - CURL Setup Script"
echo "============================================"
echo ""

# Get the script directory
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
cd "$SCRIPT_DIR"

# Detect package manager and install CURL if needed
USE_SYSTEM_CURL=1

if command -v apt-get &> /dev/null; then
    # Debian/Ubuntu
    PKG_MANAGER="apt-get"
    echo "[INFO] Detected Debian/Ubuntu system"
    
    if ! dpkg -l | grep -q libcurl4-openssl-dev; then
        echo "[1/4] Installing CURL development package..."
        sudo apt-get update
        sudo apt-get install -y libcurl4-openssl-dev build-essential cmake
        echo "[SUCCESS] Installed libcurl4-openssl-dev"
    else
        echo "[INFO] CURL development package already installed"
    fi
    
    CURL_INCLUDE_DIR="/usr/include"
    CURL_LIBRARY="/usr/lib/x86_64-linux-gnu/libcurl.so"
    
elif command -v yum &> /dev/null; then
    # RHEL/CentOS/Fedora
    PKG_MANAGER="yum"
    echo "[INFO] Detected RHEL/CentOS/Fedora system"
    
    if ! rpm -qa | grep -q libcurl-devel; then
        echo "[1/4] Installing CURL development package..."
        sudo yum install -y libcurl-devel gcc-c++ cmake make
        echo "[SUCCESS] Installed libcurl-devel"
    else
        echo "[INFO] CURL development package already installed"
    fi
    
    CURL_INCLUDE_DIR="/usr/include"
    CURL_LIBRARY="/usr/lib64/libcurl.so"
    
elif command -v dnf &> /dev/null; then
    # Fedora (newer versions)
    PKG_MANAGER="dnf"
    echo "[INFO] Detected Fedora system (dnf)"
    
    if ! rpm -qa | grep -q libcurl-devel; then
        echo "[1/4] Installing CURL development package..."
        sudo dnf install -y libcurl-devel gcc-c++ cmake make
        echo "[SUCCESS] Installed libcurl-devel"
    else
        echo "[INFO] CURL development package already installed"
    fi
    
    CURL_INCLUDE_DIR="/usr/include"
    CURL_LIBRARY="/usr/lib64/libcurl.so"
    
elif command -v pacman &> /dev/null; then
    # Arch Linux
    PKG_MANAGER="pacman"
    echo "[INFO] Detected Arch Linux system"
    
    if ! pacman -Qi curl &> /dev/null; then
        echo "[1/4] Installing CURL..."
        sudo pacman -Sy --noconfirm curl base-devel cmake
        echo "[SUCCESS] Installed curl"
    else
        echo "[INFO] CURL already installed"
    fi
    
    CURL_INCLUDE_DIR="/usr/include"
    CURL_LIBRARY="/usr/lib/libcurl.so"
    
else
    # Fallback: download and build from source
    echo "[INFO] Package manager not detected, will build CURL from source"
    USE_SYSTEM_CURL=0
    
    mkdir -p third_party
    cd third_party
    
    if [ -d "curl" ]; then
        echo "[INFO] CURL directory already exists. Skipping download."
    else
        echo "[1/4] Downloading CURL source code..."
        echo ""
        
        CURL_VERSION="8.10.1"
        CURL_URL="https://curl.se/download/curl-${CURL_VERSION}.tar.gz"
        CURL_TAR="curl-${CURL_VERSION}.tar.gz"
        
        # Download CURL source
        wget -O "$CURL_TAR" "$CURL_URL" || curl -L -o "$CURL_TAR" "$CURL_URL"
        
        if [ $? -ne 0 ]; then
            echo "[ERROR] Failed to download CURL"
            echo "Please check your internet connection and try again."
            exit 1
        fi
        
        echo "[SUCCESS] Downloaded CURL successfully"
        echo ""
        
        echo "[2/4] Extracting CURL..."
        tar -xzf "$CURL_TAR"
        mv "curl-${CURL_VERSION}" curl
        rm "$CURL_TAR"
        
        echo "[SUCCESS] Extracted CURL"
        echo ""
        
        # Build CURL
        echo "[2.5/4] Building CURL from source..."
        cd curl
        
        ./configure --prefix="$SCRIPT_DIR/third_party/curl-install" \
                    --with-ssl \
                    --disable-ldap \
                    --disable-ldaps \
                    --enable-ipv6
        
        make -j$(nproc)
        make install
        
        cd ..
        echo "[SUCCESS] Built and installed CURL"
        echo ""
    fi
    
    CURL_INCLUDE_DIR="$SCRIPT_DIR/third_party/curl-install/include"
    CURL_LIBRARY="$SCRIPT_DIR/third_party/curl-install/lib/libcurl.so"
    
    cd "$SCRIPT_DIR"
fi

echo ""
echo "[3/4] Configuring CMake with CURL..."
echo ""

# Check for CMake
if ! command -v cmake &> /dev/null; then
    echo "[ERROR] CMake not found"
    echo "Please install CMake using your package manager"
    exit 1
fi

CMAKE_VERSION=$(cmake --version | head -n 1)
echo "[INFO] Found $CMAKE_VERSION"
echo ""

# Set up CURL paths
echo "[INFO] CURL Include: $CURL_INCLUDE_DIR"
echo "[INFO] CURL Library: $CURL_LIBRARY"
echo ""

# Clean and recreate build directory
if [ -d "build_linux" ]; then
    echo "[INFO] Cleaning existing build directory..."
    rm -rf build_linux
    echo "[INFO] Build directory cleaned"
fi
mkdir -p build_linux
cd build_linux

echo "[INFO] Configuring CMake..."
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
NUM_CORES=$(nproc 2>/dev/null || echo 4)
cmake --build . --config Release -j $NUM_CORES

if [ $? -ne 0 ]; then
    echo "[ERROR] Build failed"
    exit 1
fi

echo "[SUCCESS] Build completed successfully"
echo ""

# Copy CURL library if built from source
if [ $USE_SYSTEM_CURL -eq 0 ]; then
    echo "[INFO] Copying CURL library to output directory..."
    if [ -f "$SCRIPT_DIR/third_party/curl-install/lib/libcurl.so.4" ]; then
        cp "$SCRIPT_DIR/third_party/curl-install/lib/libcurl.so.4" . 2>/dev/null || true
    fi
fi

cd "$SCRIPT_DIR"

echo ""
echo "============================================"
echo "   Build Complete!"
echo "============================================"
echo ""
echo "Output directory: build_linux/"
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
echo "To run tests: cd build_linux && ./feature_test"
echo ""

# Make the script executable
chmod +x "$0"

echo "[INFO] Setup script is now executable"
echo ""
