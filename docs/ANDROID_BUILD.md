# Android JNI Integration for NetworkChangeNotifier

This directory contains example code for integrating the NQE NetworkChangeNotifier with Android using JNI (Java Native Interface).

## Overview

The NetworkChangeNotifier supports Android platform by combining:
1. **Native netlink monitoring** - Basic network interface change detection
2. **JNI callbacks** - Detailed network type information from Android's ConnectivityManager

## Architecture

```
┌─────────────────────────────────────┐
│   Android Java Layer                │
│  (NetworkChangeNotifier.java)       │
│   - ConnectivityManager             │
│   - TelephonyManager                │
│   - Network type detection          │
└───────────────┬─────────────────────┘
                │ JNI
                ↓
┌─────────────────────────────────────┐
│   Native C++ Layer                  │
│  (NetworkChangeNotifier.cpp)        │
│   - Netlink monitoring              │
│   - Observer notifications          │
│   - Cross-platform API              │
└─────────────────────────────────────┘
```

## Building with CMake (Modern Approach)

### Option 1: Integrate into Android Studio Project

1. Copy this entire `android_example` folder to your Android project
2. Copy the Java file to your project:
   ```
   app/src/main/java/com/nqe/NetworkChangeNotifier.java
   ```

3. In your `app/build.gradle`, add:
   ```gradle
   android {
       ...
       defaultConfig {
           ...
           externalNativeBuild {
               cmake {
                   cppFlags "-std=c++17 -fexceptions -frtti"
                   arguments "-DANDROID_STL=c++_shared"
               }
           }
           ndk {
               abiFilters 'armeabi-v7a', 'arm64-v8a', 'x86', 'x86_64'
           }
       }
       
       externalNativeBuild {
           cmake {
               path file('path/to/android_example/CMakeLists.txt')
               version '3.22.1'
           }
       }
   }
   ```

4. Build your project normally in Android Studio

### Option 2: Standalone CMake Build

Build for all architectures:

```bash
# Set your NDK path
export ANDROID_NDK=/path/to/android/ndk

# Build for arm64-v8a
cmake -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK/build/cmake/android.toolchain.cmake \
      -DANDROID_ABI=arm64-v8a \
      -DANDROID_PLATFORM=android-21 \
      -DCMAKE_BUILD_TYPE=Release \
      -B build/arm64-v8a \
      -S .
cmake --build build/arm64-v8a -j8

# Build for armeabi-v7a
cmake -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK/build/cmake/android.toolchain.cmake \
      -DANDROID_ABI=armeabi-v7a \
      -DANDROID_PLATFORM=android-21 \
      -DCMAKE_BUILD_TYPE=Release \
      -B build/armeabi-v7a \
      -S .
cmake --build build/armeabi-v7a -j8

# Build for x86_64
cmake -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK/build/cmake/android.toolchain.cmake \
      -DANDROID_ABI=x86_64 \
      -DANDROID_PLATFORM=android-21 \
      -DCMAKE_BUILD_TYPE=Release \
      -B build/x86_64 \
      -S .
cmake --build build/x86_64 -j8

# Build for x86
cmake -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK/build/cmake/android.toolchain.cmake \
      -DANDROID_ABI=x86 \
      -DANDROID_PLATFORM=android-21 \
      -DCMAKE_BUILD_TYPE=Release \
      -B build/x86 \
      -S .
cmake --build build/x86 -j8
```

Or use the helper script (if you create one):

```bash
# Build all architectures at once
./build_android.sh
```

### Build Script Example

Create `build_android.sh`:

```bash
#!/bin/bash
set -e

ANDROID_NDK="${ANDROID_NDK:-$HOME/Android/sdk/ndk/27.0.12077973}"
API_LEVEL=21
BUILD_TYPE="${1:-Release}"

ABIS=("arm64-v8a" "armeabi-v7a" "x86_64" "x86")

for ABI in "${ABIS[@]}"; do
    echo "Building for $ABI..."
    cmake -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK/build/cmake/android.toolchain.cmake" \
          -DANDROID_ABI="$ABI" \
          -DANDROID_PLATFORM="android-$API_LEVEL" \
          -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
          -B "build/$ABI" \
          -S .
    cmake --build "build/$ABI" -j$(nproc)
done

echo "Build complete! Libraries are in build/*/libnqe.so"
```

## Files

- **`java/com/nqe/NetworkChangeNotifier.java`** - Java wrapper class that:
  - Registers with Android's ConnectivityManager
  - Detects network type (WiFi, Cellular 2G/3G/4G/5G, Ethernet, Bluetooth)
  - Calls native JNI methods when network changes
  - Provides `getConnectionType()` method called from native code

- **`CMakeLists.txt`** - Modern CMake build configuration
  - Supports both Android Studio integration and standalone builds
  - Auto-detects source paths
  - Optimizes for release builds
  - Configurable for all Android ABIs

- **`../src/NetworkChangeNotifier.cpp`** - Native C++ implementation with:
  - Android platform support (`#ifdef __ANDROID__`)
  - JNI helper methods to call Java
  - Netlink socket monitoring for basic detection
  - Fallback to interface name detection if JNI not configured

## Integration Steps

### 1. Add to Android Project

Copy the Java file to your Android project:
```
app/src/main/java/com/nqe/NetworkChangeNotifier.java
```

### 2. Configure CMake Build

See "Building with CMake" section above.

### 3. Load Native Library

In your Android application:

```java
public class MainActivity extends AppCompatActivity {
    static {
        System.loadLibrary("nqe");
    }
    
    private NetworkChangeNotifier networkNotifier;
    
    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        
        // Create and start network monitoring
        networkNotifier = new NetworkChangeNotifier(this);
        networkNotifier.start();
    }
    
    @Override
    protected void onDestroy() {
        super.onDestroy();
        if (networkNotifier != null) {
            networkNotifier.stop();
        }
    }
}
```

### 4. Add Permissions

In `AndroidManifest.xml`:

```xml
<uses-permission android:name="android.permission.ACCESS_NETWORK_STATE" />
<uses-permission android:name="android.permission.READ_PHONE_STATE" />
```

## JNI Methods

The native library exposes these JNI methods:

### `nativeInit(Object callback)`
Initializes the JNI bridge with a Java callback object.
- Called from Java `start()` method
- Stores global reference to the callback object
- Sets up JavaVM pointer for later calls

### `nativeCleanup()`
Cleans up JNI resources.
- Called from Java `stop()` method
- Deletes global references
- Resets JavaVM pointer

### `nativeOnNetworkChanged(int networkType)`
Notifies native code of network changes.
- Called from Java when ConnectivityManager detects changes
- Passes the current network type as integer
- Triggers observer notifications on C++ side

## Connection Type Mapping

The Java constants match the C++ `ConnectionType` enum:

| Java Constant | C++ Enum | Description |
|--------------|----------|-------------|
| CONNECTION_UNKNOWN (0) | UNKNOWN | Cannot determine type |
| CONNECTION_ETHERNET (1) | ETHERNET | Wired Ethernet |
| CONNECTION_WIFI (2) | WIFI | WiFi/802.11 |
| CONNECTION_CELLULAR_2G (3) | CELLULAR_2G | 2G (GPRS, EDGE, CDMA) |
| CONNECTION_CELLULAR_3G (4) | CELLULAR_3G | 3G (UMTS, HSPA, EVDO) |
| CONNECTION_CELLULAR_4G (5) | CELLULAR_4G | 4G (LTE) |
| CONNECTION_CELLULAR_5G (6) | CELLULAR_5G | 5G (NR) |
| CONNECTION_BLUETOOTH (7) | BLUETOOTH | Bluetooth tethering |
| CONNECTION_NONE (8) | NONE | No connection |

## Usage from Native Code

Once JNI is initialized, use the NetworkChangeNotifier normally:

```cpp
#include "nqe/NetworkChangeNotifier.h"

// Get singleton instance
auto& notifier = nqe::NetworkChangeNotifier::instance();

// Add observer
class MyObserver : public nqe::NetworkChangeObserver {
public:
    void onNetworkChanged(nqe::ConnectionType type) override {
        // Handle network change
    }
};

MyObserver observer;
notifier.addObserver(&observer);
notifier.start();

// Get current connection type
nqe::ConnectionType current = notifier.getCurrentConnectionType();
```

## Fallback Behavior

If JNI is not initialized:
1. The native code falls back to basic netlink monitoring
2. Connection type detection uses interface names:
   - `wlan*` → WiFi
   - `rmnet*`, `ccmni*` → Cellular (assumes 4G)
   - `eth*` → Ethernet
   - `bt-pan*` → Bluetooth
3. Cellular generation (2G/3G/4G/5G) cannot be accurately determined

For accurate cellular type detection, JNI integration is required.

## Testing

Test the integration by:
1. Building the native library for Android
2. Running the app on a device or emulator
3. Toggling WiFi/Cellular in Android settings
4. Checking logcat for NetworkChangeNotifier messages

Example logcat output:
```
[INFO] NetworkChangeNotifier: Android JNI initialized
[INFO] NetworkChangeNotifier: Started monitoring
[INFO] NetworkChangeNotifier: Connection type changed from Unknown to WiFi
[DEBUG] NetworkChangeNotifier: Java notified network change to WiFi
```

## Troubleshooting

**JNI not working:**
- Verify native library is loaded: `System.loadLibrary("nqe")`
- Check JNI function signatures match exactly
- Ensure proper permissions in AndroidManifest.xml

**Network type always UNKNOWN:**
- JNI might not be initialized
- Check that `nativeInit()` was called successfully
- Verify callback object is passed correctly

**Crashes on network change:**
- Check thread safety in JNI calls
- Ensure JavaVM is properly attached before calling Java methods
- Verify global references are valid

**CMake build fails:**
- Verify NDK path is correct
- Check that source files exist at expected paths
- Ensure CMake version is 3.18.1 or higher

## Requirements

- **Android NDK**: r21 or later (tested with NDK 27)
- **CMake**: 3.18.1 or later
- **Minimum SDK**: API 21 (Android 5.0 Lollipop)
- **Target SDK**: API 21+ (uses NetworkCallback on API 23+)
- **C++ Standard**: C++17

## Notes

- The Java class requires API 21 (Lollipop) or higher for NetworkCallback
- Falls back to deprecated NetworkInfo API on older versions
- TelephonyManager is used for cellular generation detection
- Thread-safe: JNI calls properly attach/detach threads as needed
- Release builds automatically strip symbols for smaller library size
