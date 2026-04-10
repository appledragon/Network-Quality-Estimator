# CMakeLists.txt for building NQE native library with Android NDK
# 
# Usage:
# 1. In your Android project's app/build.gradle, add:
#    android {
#        externalNativeBuild {
#            cmake {
#                path "path/to/this/CMakeLists.txt"
#                version "3.22.1"
#            }
#        }
#    }
#
# 2. Or build standalone with:
#    cmake -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK/build/cmake/android.toolchain.cmake \
#          -DANDROID_ABI=arm64-v8a \
#          -DANDROID_PLATFORM=android-21 \
#          -B build \
#          -S .
#    cmake --build build

cmake_minimum_required(VERSION 3.18.1)

project(nqe VERSION 1.0.0 LANGUAGES CXX)

# C++17 standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Compiler flags for optimization and warnings
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    add_compile_options(-O3 -DNDEBUG)
elseif(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_options(-O0 -g -DDEBUG)
endif()

add_compile_options(-Wall -Wextra -fexceptions -frtti)

# Determine source path based on project structure
# Supports both standalone build and Android project integration
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../src")
    set(NQE_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../src")
    set(NQE_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../include")
elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../../../src")
    # When placed in app/src/main/cpp/
    set(NQE_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../../src")
    set(NQE_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../../include")
else()
    message(FATAL_ERROR "Cannot find NQE source directory. Expected ../src or ../../../../src")
endif()

# NQE library source files
set(NQE_SOURCES
    ${NQE_SRC_DIR}/NetworkChangeNotifier.cpp
    ${NQE_SRC_DIR}/Nqe.cpp
    ${NQE_SRC_DIR}/EffectiveConnectionType.cpp
    ${NQE_SRC_DIR}/ThroughputAnalyzer.cpp
    ${NQE_SRC_DIR}/NetworkID.cpp
    ${NQE_SRC_DIR}/CachedNetworkQuality.cpp
    ${NQE_SRC_DIR}/NetworkQualityStore.cpp
)

# Create shared library
add_library(nqe SHARED ${NQE_SOURCES})

# Set library properties
set_target_properties(nqe PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION 1
    PUBLIC_HEADER "${NQE_INCLUDE_DIR}/nqe/Nqe.h"
)

# Include directories
target_include_directories(nqe
    PUBLIC
        $<BUILD_INTERFACE:${NQE_INCLUDE_DIR}>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${NQE_SRC_DIR}
)

# Compile definitions for Android
target_compile_definitions(nqe PRIVATE
    ANDROID
    __ANDROID__
)

# Find and link Android system libraries
find_library(log-lib log REQUIRED)
find_library(android-lib android REQUIRED)

# Link libraries
target_link_libraries(nqe
    PRIVATE
        ${log-lib}
        ${android-lib}
)

# Add pthread support
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
target_link_libraries(nqe PRIVATE Threads::Threads)

# Strip symbols in release builds for smaller library size
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    add_custom_command(TARGET nqe POST_BUILD
        COMMAND ${CMAKE_STRIP} --strip-unneeded $<TARGET_FILE:nqe>
        COMMENT "Stripping symbols from libnqe.so"
    )
endif()

# Print configuration summary
message(STATUS "")
message(STATUS "NQE Android Native Library Configuration:")
message(STATUS "  Version:           ${PROJECT_VERSION}")
message(STATUS "  Build Type:        ${CMAKE_BUILD_TYPE}")
message(STATUS "  C++ Standard:      ${CMAKE_CXX_STANDARD}")
message(STATUS "  Android ABI:       ${ANDROID_ABI}")
message(STATUS "  Android Platform:  ${ANDROID_PLATFORM}")
message(STATUS "  Source Directory:  ${NQE_SRC_DIR}")
message(STATUS "  Include Directory: ${NQE_INCLUDE_DIR}")
message(STATUS "")
