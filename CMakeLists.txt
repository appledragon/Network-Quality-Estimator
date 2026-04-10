cmake_minimum_required(VERSION 3.15)
project(libcurl_multi_nqe_example CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Force UTF-8 encoding for MSVC to handle Unicode characters in source files
if(MSVC)
  add_compile_options(/utf-8)
endif()

# Find dependencies
find_package(Threads REQUIRED)

# libcurl is optional for Android NDK builds when BUILD_EXAMPLES=OFF
if(BUILD_EXAMPLES)
  find_package(CURL REQUIRED)
elseif(NOT ANDROID)
  find_package(CURL QUIET)
endif()

# Include public headers and internal aggregator headers
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src)

# Helper function to add platform-specific libraries
function(add_platform_libs target)
  if(WIN32)
    target_link_libraries(${target} PRIVATE Ws2_32 iphlpapi)
  endif()
  if(ANDROID)
    target_link_libraries(${target} PRIVATE log android)
  endif()
  if(APPLE)
    target_link_libraries(${target} PRIVATE "-framework CoreFoundation" "-framework SystemConfiguration")
  endif()
endfunction()

# NQE library (for Android NDK and other standalone builds)
option(BUILD_NQE_LIBRARY "Build NQE as a library" OFF)

if(BUILD_NQE_LIBRARY OR (ANDROID AND NOT BUILD_EXAMPLES))
  # Source files for NQE library
  set(NQE_LIB_SOURCES
    src/Nqe.cpp
    src/EffectiveConnectionType.cpp
    src/ThroughputAnalyzer.cpp
    src/NetworkID.cpp
    src/CachedNetworkQuality.cpp
    src/NetworkQualityStore.cpp
  )
  
  # Add NetworkChangeNotifier only for non-Android builds in library mode
  # Android NDK builds require JNI setup which is better done in Android-specific projects
  if(NOT ANDROID)
    list(APPEND NQE_LIB_SOURCES src/NetworkChangeNotifier.cpp)
  endif()
  
  add_library(nqe SHARED ${NQE_LIB_SOURCES})
  target_include_directories(nqe PUBLIC 
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
  )
  target_link_libraries(nqe PRIVATE Threads::Threads)
  add_platform_libs(nqe)
  
  # Define macro to disable NetworkChangeNotifier for Android
  if(ANDROID)
    target_compile_definitions(nqe PRIVATE NQE_DISABLE_NETWORK_CHANGE_NOTIFIER=1)
  endif()
endif()

# Optional: Build additional examples
option(BUILD_EXAMPLES "Build additional examples" ON)

# Only build examples if libcurl is available
if(BUILD_EXAMPLES AND CURL_FOUND)
  # Main example with libcurl
  add_executable(libcurl_multi_nqe_example
    test/libcurl_multi_nqe_example.cpp
    src/Nqe.cpp
    src/EffectiveConnectionType.cpp
    src/ThroughputAnalyzer.cpp
    src/NetworkChangeNotifier.cpp
    src/NetworkID.cpp
    src/CachedNetworkQuality.cpp
    src/NetworkQualityStore.cpp
  )

  target_include_directories(libcurl_multi_nqe_example PRIVATE ${CURL_INCLUDE_DIRS})
  target_link_libraries(libcurl_multi_nqe_example PRIVATE ${CURL_LIBRARIES} Threads::Threads)
  add_platform_libs(libcurl_multi_nqe_example)
endif()

# Build additional examples if enabled
if(BUILD_EXAMPLES AND CURL_FOUND)
  # HTTP RTT example (uses CURL)
  add_executable(http_rtt_example
    test/http_rtt_example.cpp
    src/Nqe.cpp
    src/EffectiveConnectionType.cpp
    src/ThroughputAnalyzer.cpp
    src/NetworkChangeNotifier.cpp
    src/NetworkID.cpp
    src/CachedNetworkQuality.cpp
    src/NetworkQualityStore.cpp
  )
  target_link_libraries(http_rtt_example PRIVATE Threads::Threads CURL::libcurl)
  add_platform_libs(http_rtt_example)

  # Continuous HTTP monitor (real-time ECT monitoring)
  add_executable(continuous_http_monitor
    test/continuous_http_monitor.cpp
    src/Nqe.cpp
    src/EffectiveConnectionType.cpp
    src/ThroughputAnalyzer.cpp
    src/NetworkChangeNotifier.cpp
    src/NetworkID.cpp
    src/CachedNetworkQuality.cpp
    src/NetworkQualityStore.cpp
  )
  target_link_libraries(continuous_http_monitor PRIVATE Threads::Threads CURL::libcurl)
  add_platform_libs(continuous_http_monitor)

  # PING RTT example
  add_executable(ping_rtt_example
    test/ping_rtt_example.cpp
    src/Nqe.cpp
    src/EffectiveConnectionType.cpp
    src/ThroughputAnalyzer.cpp
    src/NetworkChangeNotifier.cpp
    src/NetworkID.cpp
    src/CachedNetworkQuality.cpp
    src/NetworkQualityStore.cpp
  )
  target_link_libraries(ping_rtt_example PRIVATE Threads::Threads)
  add_platform_libs(ping_rtt_example)

  # Comprehensive feature test
  add_executable(feature_test
    test/feature_test.cpp
    src/Nqe.cpp
    src/EffectiveConnectionType.cpp
    src/ThroughputAnalyzer.cpp
    src/NetworkChangeNotifier.cpp
    src/NetworkID.cpp
    src/CachedNetworkQuality.cpp
    src/NetworkQualityStore.cpp
  )
  target_include_directories(feature_test PRIVATE ${CURL_INCLUDE_DIRS})
  target_link_libraries(feature_test PRIVATE Threads::Threads ${CURL_LIBRARIES})
  add_platform_libs(feature_test)
  
  # Extended features test
  add_executable(extended_features_test
    test/extended_features_test.cpp
    src/Nqe.cpp
    src/EffectiveConnectionType.cpp
    src/ThroughputAnalyzer.cpp
    src/NetworkChangeNotifier.cpp
    src/NetworkID.cpp
    src/CachedNetworkQuality.cpp
    src/NetworkQualityStore.cpp
  )
  target_include_directories(extended_features_test PRIVATE ${CURL_INCLUDE_DIRS})
  target_link_libraries(extended_features_test PRIVATE Threads::Threads ${CURL_LIBRARIES})
  add_platform_libs(extended_features_test)

  # Report generation demo
  add_executable(report_demo
    test/report_demo.cpp
    src/Nqe.cpp
    src/EffectiveConnectionType.cpp
    src/ThroughputAnalyzer.cpp
    src/NetworkChangeNotifier.cpp
    src/NetworkID.cpp
    src/CachedNetworkQuality.cpp
    src/NetworkQualityStore.cpp
  )
  target_link_libraries(report_demo PRIVATE Threads::Threads)
  add_platform_libs(report_demo)
  
  # Network change detection test
  add_executable(network_change_test
    test/network_change_test.cpp
    src/Nqe.cpp
    src/EffectiveConnectionType.cpp
    src/ThroughputAnalyzer.cpp
    src/NetworkChangeNotifier.cpp
    src/NetworkID.cpp
    src/CachedNetworkQuality.cpp
    src/NetworkQualityStore.cpp
  )
  target_link_libraries(network_change_test PRIVATE Threads::Threads)
  add_platform_libs(network_change_test)
  
  # ThroughputAnalyzer comprehensive test
  add_executable(throughput_analyzer_test
    test/throughput_analyzer_test.cpp
    src/Nqe.cpp
    src/EffectiveConnectionType.cpp
    src/ThroughputAnalyzer.cpp
    src/NetworkChangeNotifier.cpp
    src/NetworkID.cpp
    src/CachedNetworkQuality.cpp
    src/NetworkQualityStore.cpp
  )
  target_link_libraries(throughput_analyzer_test PRIVATE Threads::Threads)
  add_platform_libs(throughput_analyzer_test)
  
  # Network quality caching test
  add_executable(network_quality_cache_test
    test/network_quality_cache_test.cpp
    src/Nqe.cpp
    src/EffectiveConnectionType.cpp
    src/ThroughputAnalyzer.cpp
    src/NetworkChangeNotifier.cpp
    src/NetworkID.cpp
    src/CachedNetworkQuality.cpp
    src/NetworkQualityStore.cpp
  )
  target_link_libraries(network_quality_cache_test PRIVATE Threads::Threads)
  add_platform_libs(network_quality_cache_test)
  
  # ECT recomputation test
  add_executable(ect_recomputation_test
    test/ect_recomputation_test.cpp
    src/Nqe.cpp
    src/EffectiveConnectionType.cpp
    src/ThroughputAnalyzer.cpp
    src/NetworkChangeNotifier.cpp
    src/NetworkID.cpp
    src/CachedNetworkQuality.cpp
    src/NetworkQualityStore.cpp
  )
  target_link_libraries(ect_recomputation_test PRIVATE Threads::Threads)
  add_platform_libs(ect_recomputation_test)
  
  # Advanced features demo (Chrome NQE-inspired features)
  add_executable(advanced_features_demo
    test/advanced_features_demo.cpp
    src/Nqe.cpp
    src/EffectiveConnectionType.cpp
    src/ThroughputAnalyzer.cpp
    src/NetworkChangeNotifier.cpp
    src/NetworkID.cpp
    src/CachedNetworkQuality.cpp
    src/NetworkQualityStore.cpp
  )
  target_include_directories(advanced_features_demo PRIVATE ${CURL_INCLUDE_DIRS})
  target_link_libraries(advanced_features_demo PRIVATE Threads::Threads ${CURL_LIBRARIES})
  add_platform_libs(advanced_features_demo)
  
  # Complete features demo (ALL Chrome NQE features)
  add_executable(complete_features_demo
    test/complete_features_demo.cpp
    src/Nqe.cpp
    src/EffectiveConnectionType.cpp
    src/ThroughputAnalyzer.cpp
    src/NetworkChangeNotifier.cpp
    src/NetworkID.cpp
    src/CachedNetworkQuality.cpp
    src/NetworkQualityStore.cpp
  )
  target_include_directories(complete_features_demo PRIVATE ${CURL_INCLUDE_DIRS})
  target_link_libraries(complete_features_demo PRIVATE Threads::Threads ${CURL_LIBRARIES})
  add_platform_libs(complete_features_demo)
endif()

# Unit test (no CURL dependency)
add_executable(unit_test
  test/unit_test.cpp
  src/Nqe.cpp
  src/EffectiveConnectionType.cpp
  src/ThroughputAnalyzer.cpp
  src/NetworkChangeNotifier.cpp
  src/NetworkID.cpp
  src/CachedNetworkQuality.cpp
  src/NetworkQualityStore.cpp
)
target_link_libraries(unit_test PRIVATE Threads::Threads)
add_platform_libs(unit_test)