# libcurl multi + NQE-like RTT Estimation (Cross-Platform)

This repository demonstrates a comprehensive, cross-platform Network Quality Estimator (NQE) inspired by Chromium's implementation, integrated with libcurl's multi interface.

## Features

### RTT Sources
- **HTTP RTT (upper bound)**: TTFB via `CURLINFO_STARTTRANSFER_TIME`
  - Supports granular observation sources: HTTP, CACHED_HTTP, H2_PINGS, H3_PINGS
  - Automatic bounding: Transport RTT ≤ HTTP RTT ≤ End-to-End RTT
  - Smart adjustment: When sample count < 30, HTTP RTT adjusted using End-to-End RTT
- **Transport RTT (lower bound)**: periodic RTT from TCP_INFO (Linux/Android/macOS/iOS) or SIO_TCP_INFO (Windows 10+)
  - Supports TCP, QUIC sources
- **End-to-End RTT**: Full request-response cycle measurement
  - Supports URL_REQUEST, H2_PINGS, H3_PINGS sources
  - Used for HTTP RTT bounding and adjustment
- **12+ observation sources**: Fine-grained tracking including URL_REQUEST, TCP, QUIC, H2_PINGS, H3_PINGS, CACHED_HTTP, and more

### Throughput Estimation
- **Downstream throughput tracking**: Calculated from HTTP response sizes and transfer times
- **Time-weighted median aggregation**: Recent samples weighted higher
- **Dual-factor observation weighting**: Combines time decay with signal strength weighting
  - Time weight: exponential decay based on sample age
  - Signal weight: based on current vs. sample signal strength difference
- **ECT-based throughput clamping**: Maximum throughput limited by current Effective Connection Type
  - SLOW_2G: max 40 kbps
  - 2G: max 75 kbps
  - 3G: max 400 kbps
  - 4G and above: unclamped
- **Window-level hanging detection**: Chromium cwnd-based heuristic to filter unreliable windows
  - Detects stalled TCP connections
  - Filters out low-throughput windows caused by network congestion
- **Configurable minimum transfer size**: Filter out small transfers for reliable estimates
- **Signal strength tracking**: Optional signal strength-aware observation weighting

### Effective Connection Type (ECT)
- **Network quality classification**: SLOW_2G, 2G, 3G, 4G, or UNKNOWN
- **Based on RTT and throughput thresholds**: Following Chromium's NQE approach
- **Real-time ECT updates**: As network conditions change
- **Configurable thresholds**: Customize ECT computation for different use cases
- **Intelligent recomputation**: Chromium NQE-style automatic triggers
  - Time-based recomputation (configurable interval)
  - Network change detection
  - Observation count thresholds
  - 50% observation increase detection
  - See `ECT_RECOMPUTATION.md` for details

### Network Change Detection
- **Cross-platform network monitoring**: Detect network interface changes automatically
- **Platform-specific implementations**:
  - Android: netlink sockets + JNI callbacks for detailed network info (WiFi, Cellular 2G/3G/4G/5G)
  - Linux: netlink sockets monitoring (RTMGRP_LINK, IPv4/IPv6 address changes)
  - Windows: NotifyAddrChange API
  - macOS: SystemConfiguration framework
- **Automatic sample management**: Optionally clear estimates when network changes (e.g., WiFi to cellular)
- **Observer notifications**: Get notified when connection type changes
- **Connection type detection**: Identify ETHERNET, WIFI, CELLULAR (2G/3G/4G/5G), BLUETOOTH, or NONE
- **Android JNI integration**: See `android_example/` for complete integration guide

### Aggregation & Estimation
- **Per-source time-weighted median**: Recent samples weighted higher using exponential decay
- **Heuristic combination**: lower = min(PING, transport), upper = HTTP_TTFB; returns log-space weighted midpoint, biased toward lower bound
- **Freshness threshold**: Drop stale sources automatically
- **Observer pattern**: Get notified of RTT, throughput, and ECT changes

### Statistics & Monitoring
- **Comprehensive statistics**: Track min, max, percentiles (50th, 95th, 99th) for each source
- **Sample counts**: Active socket tracking and transfer counting
- **Configurable logging**: Multiple log levels (DEBUG, INFO, WARNING, ERROR)
- **Real-time metrics**: Current ECT, RTT estimates, and throughput

### Configuration & Validation
- **Options validation**: Comprehensive pre-flight checks
- **Flexible configuration**: Decay rates, sampling periods, bias factors, and ECT thresholds
- **Thread-safe API**: All public methods can be called from multiple threads

### Integration Examples
- **libcurl multi example**: Full integration with socket registration and throughput tracking
- **Observer examples**: Demonstrate callback-based notifications
- **HTTP RTT tracking**: Standalone TTFB measurement
- **PING RTT tracking**: Standalone QUIC/H2 PING measurement

## Advanced Features (Chrome NQE Parity)

This implementation achieves ~100% feature parity with Chromium's Network Quality Estimator through the following advanced capabilities:

### 1. End-to-End RTT Observation Category
- Separate tracking for full request-response cycle RTT
- Supports H2/H3 PING observations for QUIC connections
- Used for HTTP RTT bounding and adjustment algorithms

### 2. Granular Observation Sources (12+ types)
- **HTTP sources**: URL_REQUEST, CACHED_HTTP, H2_PINGS, H3_PINGS
- **Transport sources**: TCP, QUIC
- **Application sources**: DATABASE, DNS, PUSH_STREAM, and more
- Each source tracked independently for accurate aggregation

### 3. HTTP RTT Bounding Logic
- **Lower bound**: `max(Transport RTT, 0)` - HTTP RTT cannot be faster than transport
- **Upper bound**: `min(HTTP RTT, End-to-End RTT)` - HTTP RTT cannot exceed total request time
- Automatically enforces physical constraints on observations

### 4. HTTP RTT Adjustment Algorithm
- When HTTP RTT sample count < 30:
  - Blends HTTP RTT with End-to-End RTT for stability
  - Weight based on sample count ratio
  - Prevents unreliable estimates with sparse data

### 5. ECT-Based Throughput Clamping
- Maximum throughput constrained by network quality classification
- Prevents unrealistic throughput estimates on poor networks
- Thresholds: SLOW_2G(40kbps), 2G(75kbps), 3G(400kbps), 4G+(unlimited)

### 6. Dual-Factor Observation Weighting
- **Time factor**: Exponential decay favoring recent observations
- **Signal strength factor**: Weight based on signal level similarity
- Combined weight = `time_weight × signal_strength_weight`
- Improves accuracy on variable network conditions (WiFi, cellular)

### 7. Window-Level Hanging Detection
- Chromium cwnd (congestion window) based heuristic
- Filters throughput windows with TCP stalls/hangs
- Formula: `bits_per_rtt < cwnd_size × multiplier` → hanging
- Prevents polluted estimates from congested connections

### 8. Signal Strength Tracking
- Infrastructure for signal strength-aware observations
- Platform-specific implementations (iOS/Android/Windows)
- Enables signal strength-based observation weighting
- Currently supports manual signal strength reporting

For detailed implementation and usage examples, see test programs:
- `advanced_features_demo` - Demonstrates features 1-5
- `complete_features_demo` - Demonstrates all 8 features

## Build

### Prerequisites
- CMake ≥ 3.15
- C++17 toolchain
- libcurl (built with SSL backend of your choice)
- On Linux/macOS: pthreads (CMake `Threads` will link automatically)
- On Windows: Windows 10+ recommended for `SIO_TCP_INFO`

### Automated Build (Recommended)

**Windows:**
```batch
setup_curl_windows.bat
```
Automatically downloads prebuilt CURL, configures CMake, and builds all targets.

**macOS:**
```bash
chmod +x setup_curl_macos.sh
./setup_curl_macos.sh
```
Installs CURL via Homebrew (if available) or builds from source, then builds the project.

**Linux:**
```bash
chmod +x setup_curl_linux.sh
./setup_curl_linux.sh
```
Installs CURL via system package manager (apt/yum/dnf/pacman) or builds from source.

### Manual Build

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . -j
```

This will build:
- `libcurl_multi_nqe_example` - Main example with libcurl integration, throughput tracking, and ECT
- `http_rtt_example` - Standalone HTTP RTT tracking example
- `ping_rtt_example` - Standalone PING RTT tracking example
- `feature_test` - Comprehensive test of core library features
- `extended_features_test` - Test suite for ECT, throughput, and observers
- `network_change_test` - Test suite for network change detection
- `advanced_features_demo` - Demonstrates Chrome NQE features: End-to-End RTT, granular sources, bounding, adjustment, and clamping
- `complete_features_demo` - Demonstrates all 8 Chrome NQE parity features including dual-factor weighting and hanging detection

To build only the main example:
```bash
cmake .. -DCMAKE_BUILD_TYPE=Release -DBUILD_EXAMPLES=OFF
cmake --build . -j
```

## Run

### Main libcurl Example

```bash
./libcurl_multi_nqe_example https://example.com/ https://www.google.com/ https://www.cloudflare.com/
```

The example tests real websites and generates comprehensive reports:
- **HTML Report**: `nqe_test_report.html` - Beautiful, styled report with tables and metrics
- **Text Report**: `nqe_test_report.txt` - Plain text version for easy viewing in terminal

Output example:
```
[NQE] rtt=25.3ms http=120 tr=18 ping=-1
[HTTP] TTFB(ms) https://example.com/ = 110.5
[NQE] rtt=23.1ms http=110 tr=20 ping=-1
... 
=== Generating Test Report ===
HTML report generated: nqe_test_report.html
Text report generated: nqe_test_report.txt
=== Report Generation Complete ===
```

The reports include:
- Test summary (duration, URLs tested, sample counts)
- Final RTT estimates (combined, HTTP TTFB, transport RTT, PING RTT)
- Per-URL test results with TTFB measurements
- Detailed statistics by source (min, max, percentiles)
- Test configuration parameters

- `http` = aggregated HTTP TTFB (upper bound)
- `tr` = aggregated transport RTT (lower bound)
- `ping` = aggregated QUIC/H2 PING (if integrated)
- `rtt` = single combined RTT estimate
- `throughput` = downstream throughput estimate in kbps
- `ECT` = Effective Connection Type (network quality classification)

### Extended Features Test

```bash
./extended_features_test
```

Demonstrates and tests:
- Effective Connection Type classification
- Throughput analyzer
- Observer pattern for RTT, throughput, and ECT changes
- Integrated NQE with all features

### HTTP RTT Example

```bash
./http_rtt_example
```

Demonstrates using `HttpRttSource` to track HTTP TTFB manually.

### PING RTT Example

```bash
./ping_rtt_example
```

Demonstrates using `QuicH2PingSource` to track PING/PONG RTT.

### Feature Test

```bash
./feature_test
```

Runs a comprehensive test of all library features including validation, statistics, logging, and state queries.

### Report Generation Demo

```bash
./report_demo
```

Demonstrates report generation with simulated test data. This example:
- Simulates HTTP TTFB and transport RTT samples
- Generates both HTML and text reports (`nqe_demo_report.html` and `nqe_demo_report.txt`)
- Shows what a complete report looks like with successful test results

This is useful for:
- Understanding the report format without needing network access
- Testing the report generation functionality
- Seeing example output with mock data

## API Usage

### Basic Setup

```cpp
#include "nqe/Nqe.h"
#include "nqe/Logger.h"

// Configure logging (optional)
nqe::Logger::instance().setMinLevel(nqe::LogLevel::INFO);
nqe::Logger::instance().setCallback([](nqe::LogLevel level, const std::string& msg) {
  std::cout << "[" << nqe::logLevelToString(level) << "] " << msg << std::endl;
});

// Create and configure NQE
nqe::Nqe::Options opts;
opts.decay_lambda_per_sec = 0.03;
opts.transport_sample_period = std::chrono::milliseconds(1000);
opts.combine_bias_to_lower = 0.65;
opts.freshness_threshold = std::chrono::seconds(60);

// Chrome NQE advanced features (optional)
opts.enable_http_rtt_bounding = true;           // Bound HTTP RTT by transport and end-to-end
opts.enable_http_rtt_adjustment = true;         // Adjust HTTP RTT when sample count < 30
opts.http_rtt_adjustment_threshold = 30;        // Sample count threshold for adjustment
opts.enable_throughput_clamping = true;         // Clamp throughput based on ECT
opts.enable_signal_strength_weighting = false;  // Signal strength-aware weighting (requires platform support)
opts.weight_multiplier_per_signal_level = 0.98; // Signal strength decay factor

// Validate options before use
std::string error;
if (!nqe::Nqe::validateOptions(opts, &error)) {
  std::cerr << "Invalid options: " << error << std::endl;
  return 1;
}

nqe::Nqe estimator(opts);
estimator.startTransportSampler();
```

### Getting Estimates and Statistics

```cpp
// Get current RTT estimate
auto estimate = estimator.getEstimate();
std::cout << "Combined RTT: " << estimate.rtt_ms << "ms" << std::endl;
std::cout << "ECT: " << nqe::effectiveConnectionTypeToString(estimate.effective_type) << std::endl;

if (estimate.throughput_kbps) {
  std::cout << "Throughput: " << *estimate.throughput_kbps << " kbps" << std::endl;
}

// Get detailed statistics
auto stats = estimator.getStatistics();
std::cout << "Total samples: " << stats.total_samples << std::endl;
std::cout << "Active sockets: " << stats.active_sockets << std::endl;
std::cout << "Current ECT: " << nqe::effectiveConnectionTypeToString(stats.effective_type) << std::endl;

if (stats.http.sample_count > 0) {
  std::cout << "HTTP: min=" << *stats.http.min_ms 
            << " p50=" << *stats.http.percentile_50th
            << " p95=" << *stats.http.percentile_95th
            << " max=" << *stats.http.max_ms << std::endl;
}
```

### Socket Registration (for transport RTT)

```cpp
// Register sockets for TCP_INFO sampling
estimator.registerTcpSocket(socket_fd);

// Unregister when socket closes
estimator.unregisterTcpSocket(socket_fd);
```

### Adding Samples Manually

```cpp
// Add HTTP TTFB sample
estimator.addSample(nqe::Source::HTTP_TTFB, ttfb_ms);

// Add transport RTT sample
estimator.addSample(nqe::Source::TRANSPORT_RTT, rtt_ms);

// Add PING RTT sample
estimator.addSample(nqe::Source::PING_RTT, ping_ms);

// Add throughput sample (bytes, start_time, end_time)
auto start = nqe::Clock::now();
// ... perform download ...
auto end = nqe::Clock::now();
estimator.addThroughputSample(bytes_received, start, end);
```

### Advanced Throughput Tracking (Request-Based)

For more accurate throughput estimation following Chromium's approach, use the detailed request tracking API:

```cpp
#include "nqe/ThroughputAnalyzer.h"

// Create throughput analyzer with custom options
nqe::ThroughputAnalyzer::Options tp_opts;
tp_opts.min_transfer_size_bytes = 32000;  // 32KB minimum (Chromium default)
tp_opts.throughput_min_requests_in_flight = 5;  // Minimum concurrent requests for window
tp_opts.decay_lambda_per_sec = 0.02;
tp_opts.freshness_threshold = std::chrono::seconds(60);

// Window-level hanging detection (Chromium cwnd heuristic)
tp_opts.enable_hanging_window_detection = true;
tp_opts.hanging_window_cwnd_multiplier = 0.5;   // cwnd threshold multiplier
tp_opts.hanging_window_cwnd_size_kb = 10;       // Assumed cwnd size in KB

nqe::ThroughputAnalyzer analyzer(tp_opts);

// Track HTTP requests lifecycle
void* request_id = /* your request identifier */;

// 1. Start transaction
nqe::ThroughputAnalyzer::Request req{
  .id = request_id,
  .method = "GET",  // Only GET requests used for throughput
  .bytes_received = 0,
  .start_time = nqe::Clock::now(),
  .is_cached = false,  // Set true if from cache
  .is_hanging = false  // Set true if request is stalled
};
analyzer.notifyStartTransaction(req);

// 2. Update as bytes arrive
analyzer.notifyBytesRead(request_id, bytes_count);

// 3. Complete the request
analyzer.notifyRequestCompleted(request_id);

// Get throughput estimate
auto throughput = analyzer.getEstimate();
if (throughput) {
  std::cout << "Throughput: " << *throughput << " kbps\n";
}

// Get detailed statistics including request tracking
auto stats = analyzer.getStatistics();
std::cout << "Active requests: " << stats.active_requests << "\n";
std::cout << "Degrading requests: " << stats.degrading_requests << "\n";

// Handle network changes
analyzer.onConnectionTypeChanged();  // Resets observation window
```

**Key Features:**
- **Request Filtering**: Only GET requests tracked (POST/PUT/etc. ignored)
- **Accuracy Preservation**: Cached and hanging requests separated to degrading set
- **Observation Windows**: Throughput calculated only when ≥5 concurrent valid requests
- **Automatic Limits**: Maximum 300 requests tracked, 32KB minimum transfer size
- **Network Change Aware**: Resets state on connection type changes


### Using Observers

```cpp
#include "nqe/NetworkQualityObserver.h"

// Create custom observer
class MyRTTObserver : public nqe::RTTObserver {
public:
  void onRTTObservation(double rtt_ms, const char* source) override {
    std::cout << "New RTT: " << rtt_ms << "ms from " << source << std::endl;
  }
};

// Register observer
MyRTTObserver rtt_observer;
estimator.addRTTObserver(&rtt_observer);

// Later: unregister
estimator.removeRTTObserver(&rtt_observer);
```

### Effective Connection Type

```cpp
#include "nqe/EffectiveConnectionType.h"

// Get current ECT
auto ect = estimator.getEffectiveConnectionType();
std::cout << "Connection type: " 
          << nqe::effectiveConnectionTypeToString(ect) << std::endl;

// Customize ECT thresholds
nqe::Nqe::Options opts;
opts.ect_thresholds.http_rtt_3g = 500;  // 3G threshold
opts.ect_thresholds.downstream_throughput_3g = 600;  // 600 kbps
```

### Network Change Detection

```cpp
#include "nqe/NetworkChangeNotifier.h"

// Enable network change detection with automatic sample clearing
estimator.enableNetworkChangeDetection(true);  // clear_on_change=true

// Check if enabled
bool enabled = estimator.isNetworkChangeDetectionEnabled();

// Disable when no longer needed
estimator.disableNetworkChangeDetection();

// Alternatively, enable without clearing samples on network change
estimator.enableNetworkChangeDetection(false);  // keep existing samples

// Using NetworkChangeNotifier directly for custom behavior
nqe::NetworkChangeNotifier& notifier = nqe::NetworkChangeNotifier::instance();

// Create custom observer
class MyNetworkObserver : public nqe::NetworkChangeObserver {
public:
  void onNetworkChanged(nqe::ConnectionType type) override {
    std::cout << "Network changed to: " 
              << nqe::connectionTypeToString(type) << std::endl;
  }
};

MyNetworkObserver observer;
notifier.addObserver(&observer);
notifier.start();

// Get current connection type
nqe::ConnectionType current = notifier.getCurrentConnectionType();

// Manually trigger a network check
notifier.checkForChanges();

// Cleanup
notifier.removeObserver(&observer);
notifier.stop();
```

### Generating Reports

```cpp
#include "nqe/ReportGenerator.h"

// Prepare test data
nqe::ReportGenerator::TestData test_data;
test_data.test_start = std::chrono::steady_clock::now();
test_data.urls = {"https://example.com/", "https://www.google.com/"};
test_data.options = opts;

// ... run your tests ...

// Capture final results
test_data.test_end = std::chrono::steady_clock::now();
test_data.final_stats = estimator.getStatistics();
test_data.final_estimate = estimator.getEstimate();

// Add URL results
test_data.url_results.push_back({
  .url = "https://example.com/",
  .success = true,
  .error_msg = "",
  .ttfb_ms = 120.5
});

// Generate reports
nqe::ReportGenerator::generateHtmlReport(test_data, "report.html");
nqe::ReportGenerator::generateTextReport(test_data, "report.txt");
```

## Integrating QUIC/H2 PING

Use `include/nqe/QuicH2PingSource.h`:
- Provide a Ping implementation via `setPingImpl(...)`
- Call `onPong(authority)` when a PONG is received to record RTT samples

## Android Integration

The library supports Android platform with JNI integration for accurate network type detection.

### Quick Start

1. **Add Java wrapper to your Android project:**
   ```
   android_example/java/com/nqe/NetworkChangeNotifier.java
   ```

2. **Build native library with Android NDK:**
   - Use provided `android_example/Android.mk` or `android_example/CMakeLists.txt`
   - Link with `-llog -landroid`

3. **Initialize from Java:**
   ```java
   NetworkChangeNotifier notifier = new NetworkChangeNotifier(context);
   notifier.start();
   ```

4. **Use from C++ code normally:**
   ```cpp
   auto& notifier = nqe::NetworkChangeNotifier::instance();
   notifier.start();
   nqe::ConnectionType current = notifier.getCurrentConnectionType();
   ```

### Features on Android

- **Accurate network type detection**: WiFi, Ethernet, Cellular (2G/3G/4G/5G), Bluetooth
- **Real-time change notifications**: Via Android's ConnectivityManager
- **Dual-mode operation**: JNI for detailed info + netlink for basic monitoring
- **Fallback support**: Works without JNI using interface name detection

See `android_example/README.md` for complete integration guide with:
- Detailed JNI setup instructions
- Android.mk and CMakeLists.txt examples
- Java implementation details
- Troubleshooting guide

## Notes and Limitations

- Socket registration is done via:
  - `CURLOPT_SOCKOPTFUNCTION` (register) and
  - `CURLOPT_CLOSESOCKETFUNCTION` (unregister).
- Some platforms restrict access to TCP_INFO for non-owned sockets; this sampler only targets sockets owned by the process (libcurl-created sockets).
- On older Windows versions where `SIO_TCP_INFO` is unavailable, transport RTT may not be collected; HTTP TTFB will still work.
- Time-weighted median parameters (`decay_lambda_per_sec`) and bias toward lower bound are configurable (`Nqe::Options`).

## Project Layout

```
.
├── CMakeLists.txt
├── .gitignore
├── android_example/                     # Android JNI integration
│   ├── README.md                        # Android integration guide
│   ├── Android.mk                       # NDK build file
│   ├── CMakeLists.txt                   # CMake for Android
│   └── java/com/nqe/
│       └── NetworkChangeNotifier.java   # Java wrapper
├── include/
│   └── nqe/
│       ├── Nqe.h                        # Main NQE interface
│       ├── EffectiveConnectionType.h    # ECT classification
│       ├── ThroughputAnalyzer.h         # Throughput estimation
│       ├── NetworkChangeNotifier.h      # Network change detection
│       ├── NetworkQualityObserver.h     # Observer interfaces
│       ├── HttpRttSource.h              # HTTP RTT helper
│       ├── QuicH2PingSource.h           # QUIC/H2 PING helper
│       └── Logger.h                     # Logging framework
└── src/
    ├── Nqe.cpp
    ├── EffectiveConnectionType.cpp
    ├── ThroughputAnalyzer.cpp
    ├── NetworkChangeNotifier.cpp        # Cross-platform network monitoring
    ├── aggregators/
    │   ├── Aggregator.h             # Shared aggregator wrapper (min/max/percentile)
    │   ├── WeightedMedian.h             # Time-weighted median (deque-based)
    │   └── Combiner.h                   # RTT combination logic
    ├── libcurl_multi_nqe_example.cpp
    ├── extended_features_test.cpp
    ├── http_rtt_example.cpp
    ├── ping_rtt_example.cpp
    └── network_change_test.cpp
```

## Best Practices

### Thread Safety
- All public methods of `Nqe`, `ThroughputAnalyzer` are thread-safe
- `HttpRttSource` and `QuicH2PingSource` are thread-safe
- `Nqe` uses `std::recursive_mutex` to allow safe observer callbacks while holding locks
- `Nqe` copy/move constructors are explicitly deleted to prevent unsafe sharing
- Observer callbacks are invoked outside of locks to prevent deadlocks
- You can call `addSample()`, `addThroughputSample()`, `getEstimate()`, and `getStatistics()` from different threads
- Use `NetworkChangeNotifier::shutdown()` for explicit cleanup before static destruction

### Performance Considerations
- The transport sampler runs in a separate thread with configurable polling period
- Avoid setting `transport_sample_period` too low (< 100ms) to reduce overhead
- Statistics computation involves sorting, so call `getStatistics()` judiciously
- Observer notifications are synchronous; keep observer callbacks lightweight

### Error Handling
- Always validate options using `Nqe::validateOptions()` before creating an instance
- Check for `std::nullopt` in estimate fields when no samples are available
- Enable logging to diagnose issues during development
- Throughput samples below `min_throughput_transfer_bytes` are automatically filtered

### Sample Management
- Samples are automatically pruned when exceeding internal limits (512 per source)
- Old samples are automatically discarded based on `freshness_threshold`
- The time-weighted median naturally gives less weight to older samples

## Logging

The library includes a flexible logging framework:

```cpp
// Set minimum log level
nqe::Logger::instance().setMinLevel(nqe::LogLevel::DEBUG);  // DEBUG, INFO, WARNING, ERROR

// Set custom log callback
nqe::Logger::instance().setCallback([](nqe::LogLevel level, const std::string& msg) {
  // Your custom logging implementation
  std::cout << "[" << nqe::logLevelToString(level) << "] " << msg << std::endl;
});
```

Available log levels:
- `DEBUG`: Detailed information for debugging (sample additions, socket registration)
- `INFO`: General informational messages (initialization, state changes)
- `WARNING`: Warning messages (not currently used)
- `ERROR`: Error conditions (validation failures)

## Configuration Options

The `Nqe::Options` structure provides the following configuration:

### Core Options
- `decay_lambda_per_sec` (default: 0.02): Decay rate for time-weighted samples. Higher values favor more recent samples.
- `transport_sample_period` (default: 1000ms): How often to poll TCP_INFO for transport RTT.
- `combine_bias_to_lower` (default: 0.6): Bias factor (0-1) when combining lower/upper bounds. Higher values favor lower bound.
- `freshness_threshold` (default: 60s): Maximum age for samples before they're considered stale.

### Chrome NQE Advanced Options
- `enable_http_rtt_bounding` (default: false): Enable HTTP RTT bounding by transport and end-to-end RTT
- `enable_http_rtt_adjustment` (default: false): Enable HTTP RTT adjustment when sample count is low
- `http_rtt_adjustment_threshold` (default: 30): Minimum HTTP RTT samples before disabling adjustment
- `enable_throughput_clamping` (default: false): Enable ECT-based maximum throughput clamping
- `enable_signal_strength_weighting` (default: false): Enable signal strength-aware observation weighting
- `weight_multiplier_per_signal_level` (default: 0.98): Weight decay per signal strength level difference

### Throughput Analyzer Options
- `enable_hanging_window_detection` (default: true): Enable cwnd-based hanging window detection
- `hanging_window_cwnd_multiplier` (default: 0.5): Multiplier for cwnd threshold (0.0-1.0)
- `hanging_window_cwnd_size_kb` (default: 10): Assumed TCP congestion window size in KB

## Statistics

The library tracks comprehensive statistics for each source:

### RTT Sources (HTTP, Transport, PING)
- **Sample count**: Total number of samples collected
- **Min/Max**: Minimum and maximum RTT values observed
- **Percentiles**: 50th (median), 95th, and 99th percentile values
- **Last sample time**: Timestamp of the most recent sample

### Throughput
- **Sample count**: Number of throughput measurements
- **Min/Max**: Minimum and maximum throughput observed (kbps)
- **Percentiles**: 50th, 95th, and 99th percentile throughput values
- **Last sample time**: Timestamp of the most recent measurement

### Overall
- **Active sockets**: Number of currently registered TCP sockets
- **Total samples**: Sum of all RTT and throughput samples
- **Effective Connection Type**: Current network quality classification

## Implementation Details

This implementation is inspired by Chromium's Network Quality Estimator (NQE) at:
https://chromium.googlesource.com/chromium/src/+/HEAD/net/nqe

Key features implemented from Chromium's NQE:
- **Effective Connection Type (ECT)**: Network quality classification based on RTT and throughput
- **Observation sources**: 12+ granular sources (HTTP, TCP, QUIC, H2_PINGS, H3_PINGS, CACHED_HTTP, etc.)
- **Time-weighted aggregation**: Exponential decay favoring recent samples
- **Dual-factor weighting**: Time decay combined with signal strength weighting
- **End-to-End RTT tracking**: Separate category for full request-response cycle measurements
- **HTTP RTT bounding**: Automatic enforcement of transport ≤ HTTP ≤ end-to-end constraints
- **HTTP RTT adjustment**: Smart blending with end-to-end RTT when sample count < 30
- **ECT-based clamping**: Maximum throughput constrained by network quality classification
- **Hanging window detection**: Chromium cwnd heuristic to filter stalled TCP observations
- **Signal strength tracking**: Infrastructure for signal-aware observation weighting
- **Observer pattern**: Notifications for RTT, throughput, and ECT changes
- **Comprehensive statistics**: Percentiles, min/max tracking per source
- **Configurable thresholds**: Customizable ECT thresholds and estimation parameters
- **Network change detection**: Cross-platform monitoring with automatic sample management

Differences from Chromium's implementation:
- Simplified for cross-platform C++17 (no Chromium dependencies)
- Direct libcurl integration examples
- Standalone library design for easy integration

## License

This sample is provided for demonstration purposes and may require additional hardening for production use.
