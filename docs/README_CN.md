# libcurl multi + 类 NQE 的 RTT 估算（跨平台）

本仓库展示了一个全面的、跨平台的网络质量估算器（NQE），灵感来自 Chromium 的实现，并集成了 libcurl 的 multi 接口。

## 功能特性

### RTT 数据源
- **HTTP RTT（上界）**：通过 `CURLINFO_STARTTRANSFER_TIME` 获取 TTFB（首字节时间）
  - 支持细粒度观测源：HTTP、CACHED_HTTP、H2_PINGS、H3_PINGS
  - 自动边界：传输层 RTT ≤ HTTP RTT ≤ 端到端 RTT
  - 智能调整：当样本数 < 30 时，使用端到端 RTT 调整 HTTP RTT
- **传输层 RTT（下界）**：从 TCP_INFO（Linux/Android/macOS/iOS）或 SIO_TCP_INFO（Windows 10+）定期获取 RTT
  - 支持 TCP、QUIC 源
- **端到端 RTT**：完整请求-响应周期测量
  - 支持 URL_REQUEST、H2_PINGS、H3_PINGS 源
  - 用于 HTTP RTT 边界和调整
- **12+ 观测源**：细粒度追踪，包括 URL_REQUEST、TCP、QUIC、H2_PINGS、H3_PINGS、CACHED_HTTP 等

### 吞吐量估算
- **下行吞吐量追踪**：基于 HTTP 响应大小和传输时间计算
- **时间加权中位数聚合**：近期样本权重更高
- **双因子观测加权**：结合时间衰减和信号强度加权
  - 时间权重：基于样本年龄的指数衰减
  - 信号权重：基于当前与样本信号强度差异
- **基于 ECT 的吞吐量钳制**：最大吞吐量受当前有效连接类型限制
  - SLOW_2G：最大 40 kbps
  - 2G：最大 75 kbps
  - 3G：最大 400 kbps
  - 4G 及以上：不限制
- **窗口级挂起检测**：Chromium cwnd 启发式算法过滤不可靠窗口
  - 检测停滞的 TCP 连接
  - 过滤由网络拥塞导致的低吞吐量窗口
- **可配置的最小传输大小**：过滤小传输以获得可靠的估算
- **信号强度追踪**：可选的信号强度感知观测加权

### 有效连接类型（ECT）
- **网络质量分类**：SLOW_2G、2G、3G、4G 或 UNKNOWN
- **基于 RTT 和吞吐量阈值**：遵循 Chromium 的 NQE 方法
- **实时 ECT 更新**：随网络条件变化
- **可配置阈值**：为不同用例自定义 ECT 计算
- **智能重新计算**：Chromium NQE 风格的自动触发
  - 基于时间的重新计算（可配置间隔）
  - 网络变化检测
  - 观测数量阈值
  - 50% 观测增长检测
  - 详见 `ECT_RECOMPUTATION.md`

### 聚合与估算
- **每个数据源的时间加权中位数**：使用指数衰减使近期样本权重更高
- **启发式组合**：下界 = min(PING, 传输层)，上界 = HTTP_TTFB；返回对数空间加权中点，偏向下界
- **新鲜度阈值**：自动丢弃过时数据源
- **观察者模式**：获取 RTT、吞吐量和 ECT 变化的通知

### 统计与监控
- **全面的统计信息**：追踪每个数据源的最小值、最大值、百分位数（第 50、95、99 百分位）
- **样本计数**：活动套接字追踪和传输计数
- **可配置日志**：多个日志级别（DEBUG、INFO、WARNING、ERROR）
- **实时指标**：当前 ECT、RTT 估算值和吞吐量

### 配置与验证
- **选项验证**：全面的预检查
- **灵活配置**：衰减率、采样周期、偏置因子和 ECT 阈值
- **线程安全 API**：所有公共方法可从多个线程调用

### 集成示例
- **libcurl multi 示例**：完整集成，包含套接字注册和吞吐量追踪
- **观察者示例**：演示基于回调的通知
- **HTTP RTT 追踪**：独立的 TTFB 测量
- **PING RTT 追踪**：独立的 QUIC/H2 PING 测量

## 高级功能（Chrome NQE 平等性）

本实现通过以下高级功能实现了与 Chromium 网络质量估算器 ~100% 的功能平等性：

### 1. 端到端 RTT 观测类别
- 对完整请求-响应周期 RTT 进行单独追踪
- 支持 QUIC 连接的 H2/H3 PING 观测
- 用于 HTTP RTT 边界和调整算法

### 2. 细粒度观测源（12+ 类型）
- **HTTP 源**：URL_REQUEST、CACHED_HTTP、H2_PINGS、H3_PINGS
- **传输层源**：TCP、QUIC
- **应用层源**：DATABASE、DNS、PUSH_STREAM 等
- 每个源独立追踪以实现准确聚合

### 3. HTTP RTT 边界逻辑
- **下界**：`max(传输层 RTT, 0)` - HTTP RTT 不能比传输层更快
- **上界**：`min(HTTP RTT, 端到端 RTT)` - HTTP RTT 不能超过总请求时间
- 自动强制执行观测的物理约束

### 4. HTTP RTT 调整算法
- 当 HTTP RTT 样本数 < 30 时：
  - 将 HTTP RTT 与端到端 RTT 混合以提高稳定性
  - 权重基于样本数比例
  - 防止数据稀疏时的不可靠估计

### 5. 基于 ECT 的吞吐量钳制
- 最大吞吐量受网络质量分类约束
- 防止在差网络上出现不切实际的吞吐量估计
- 阈值：SLOW_2G(40kbps)、2G(75kbps)、3G(400kbps)、4G+(无限制)

### 6. 双因子观测加权
- **时间因子**：指数衰减偏好近期观测
- **信号强度因子**：基于信号级别相似性的权重
- 组合权重 = `时间权重 × 信号强度权重`
- 提高变化网络条件（WiFi、蜂窝）下的准确性

### 7. 窗口级挂起检测
- Chromium cwnd（拥塞窗口）启发式算法
- 过滤具有 TCP 停滞/挂起的吞吐量窗口
- 公式：`bits_per_rtt < cwnd_size × multiplier` → 挂起
- 防止拥塞连接的污染估计

### 8. 信号强度追踪
- 信号强度感知观测的基础设施
- 平台特定实现（iOS/Android/Windows）
- 支持基于信号强度的观测加权
- 当前支持手动信号强度报告

详细实现和使用示例，请参见测试程序：
- `advanced_features_demo` - 演示功能 1-5
- `complete_features_demo` - 演示所有 8 个功能

## 构建

### 前置要求
- CMake ≥ 3.15
- C++17 工具链
- libcurl（使用您选择的 SSL 后端构建）
- Linux/macOS：pthreads（CMake `Threads` 会自动链接）
- Windows：推荐 Windows 10+ 以支持 `SIO_TCP_INFO`

### 自动化构建（推荐）

**Windows:**
```batch
setup_curl_windows.bat
```
自动下载预编译的 CURL，配置 CMake，并构建所有目标。

**macOS:**
```bash
chmod +x setup_curl_macos.sh
./setup_curl_macos.sh
```
通过 Homebrew 安装 CURL（如果可用）或从源代码构建，然后构建项目。

**Linux:**
```bash
chmod +x setup_curl_linux.sh
./setup_curl_linux.sh
```
通过系统包管理器（apt/yum/dnf/pacman）安装 CURL 或从源代码构建。

### 手动构建

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . -j
```

这将构建：
- `libcurl_multi_nqe_example` - 主示例，包含 libcurl 集成、吞吐量追踪和 ECT
- `http_rtt_example` - 独立的 HTTP RTT 追踪示例
- `ping_rtt_example` - 独立的 PING RTT 追踪示例
- `feature_test` - 核心库功能的综合测试
- `extended_features_test` - ECT、吞吐量和观察者的测试套件
- `advanced_features_demo` - 演示 Chrome NQE 功能：端到端 RTT、细粒度源、边界、调整和钳制
- `complete_features_demo` - 演示所有 8 个 Chrome NQE 平等性功能，包括双因子加权和挂起检测

仅构建主示例：
```bash
cmake .. -DCMAKE_BUILD_TYPE=Release -DBUILD_EXAMPLES=OFF
cmake --build . -j
```

## 运行

### 主 libcurl 示例

```bash
./libcurl_multi_nqe_example https://example.com/ https://www.google.com/ https://www.cloudflare.com/
```

输出示例：
```
[NQE] rtt=25.3ms http=120 tr=18 ping=-1 throughput=5000kbps ECT=4G
[HTTP] TTFB(ms) https://example.com/ = 110.5
[THROUGHPUT] https://example.com/ = 5200 kbps (650000 bytes in 1.00 sec)
[NQE] rtt=23.1ms http=110 tr=20 ping=-1 throughput=5100kbps ECT=4G
.... 
```

- `http` = 聚合的 HTTP TTFB（上界）
- `tr` = 聚合的传输层 RTT（下界）
- `ping` = 聚合的 QUIC/H2 PING（如果集成）
- `rtt` = 单个组合的 RTT 估算值
- `throughput` = 下行吞吐量估算（kbps）
- `ECT` = 有效连接类型（网络质量分类）

### 扩展功能测试

```bash
./extended_features_test
```

演示并测试：
- 有效连接类型分类
- 吞吐量分析器
- RTT、吞吐量和 ECT 变化的观察者模式
- 包含所有功能的集成 NQE

### HTTP RTT 示例

```bash
./http_rtt_example
```

演示使用 `HttpRttSource` 手动追踪 HTTP TTFB。

### PING RTT 示例

```bash
./ping_rtt_example
```

演示使用 `QuicH2PingSource` 追踪 PING/PONG RTT。

### 功能测试

```bash
./feature_test
```

运行库所有功能的综合测试，包括验证、统计、日志记录和状态查询。

### 报告生成演示

```bash
./report_demo
```

演示报告生成功能，使用模拟的测试数据。此示例：
- 模拟 HTTP TTFB 和传输层 RTT 样本
- 生成 HTML 和文本报告（`nqe_demo_report.html` 和 `nqe_demo_report.txt`）
- 展示完整报告的样例输出

用途：
- 无需网络访问即可了解报告格式
- 测试报告生成功能
- 查看带有模拟数据的示例输出

## API 使用

### 基本设置

```cpp
#include "nqe/Nqe.h"
#include "nqe/Logger.h"

// 配置日志（可选）
nqe::Logger::instance().setMinLevel(nqe::LogLevel::INFO);
nqe::Logger::instance().setCallback([](nqe::LogLevel level, const std::string& msg) {
  std::cout << "[" << nqe::logLevelToString(level) << "] " << msg << std::endl;
});

// 创建并配置 NQE
nqe::Nqe::Options opts;
opts.decay_lambda_per_sec = 0.03;
opts.transport_sample_period = std::chrono::milliseconds(1000);
opts.combine_bias_to_lower = 0.65;
opts.freshness_threshold = std::chrono::seconds(60);

// Chrome NQE 高级功能（可选）
opts.enable_http_rtt_bounding = true;           // 通过传输层和端到端限制 HTTP RTT
opts.enable_http_rtt_adjustment = true;         // 当样本数 < 30 时调整 HTTP RTT
opts.http_rtt_adjustment_threshold = 30;        // 调整的样本数阈值
opts.enable_throughput_clamping = true;         // 基于 ECT 钳制吞吐量
opts.enable_signal_strength_weighting = false;  // 信号强度感知加权（需要平台支持）
opts.weight_multiplier_per_signal_level = 0.98; // 信号强度衰减因子

// 使用前验证选项
std::string error;
if (!nqe::Nqe::validateOptions(opts, &error)) {
  std::cerr << "无效选项: " << error << std::endl;
  return 1;
}

nqe::Nqe estimator(opts);
estimator.startTransportSampler();
```

### 获取估算值和统计信息

```cpp
// 获取当前 RTT 估算值
auto estimate = estimator.getEstimate();
std::cout << "组合 RTT: " << estimate.rtt_ms << "ms" << std::endl;
std::cout << "ECT: " << nqe::effectiveConnectionTypeToString(estimate.effective_type) << std::endl;

if (estimate.throughput_kbps) {
  std::cout << "吞吐量: " << *estimate.throughput_kbps << " kbps" << std::endl;
}

// 获取详细统计信息
auto stats = estimator.getStatistics();
std::cout << "总样本数: " << stats.total_samples << std::endl;
std::cout << "活动套接字: " << stats.active_sockets << std::endl;
std::cout << "当前 ECT: " << nqe::effectiveConnectionTypeToString(stats.effective_type) << std::endl;

if (stats.http.sample_count > 0) {
  std::cout << "HTTP: min=" << *stats.http.min_ms 
            << " p50=" << *stats.http.percentile_50th
            << " p95=" << *stats.http.percentile_95th
            << " max=" << *stats.http.max_ms << std::endl;
}
```

### 套接字注册（用于传输层 RTT）

```cpp
// 注册套接字以进行 TCP_INFO 采样
estimator.registerTcpSocket(socket_fd);

// 套接字关闭时取消注册
estimator.unregisterTcpSocket(socket_fd);
```

### 手动添加样本

```cpp
// 添加 HTTP TTFB 样本
estimator.addSample(nqe::Source::HTTP_TTFB, ttfb_ms);

// 添加传输层 RTT 样本
estimator.addSample(nqe::Source::TRANSPORT_RTT, rtt_ms);

// 添加 PING RTT 样本
estimator.addSample(nqe::Source::PING_RTT, ping_ms);

// 添加吞吐量样本（字节数、开始时间、结束时间）
auto start = nqe::Clock::now();
// ... 执行下载 ...
auto end = nqe::Clock::now();
estimator.addThroughputSample(bytes_received, start, end);
```

### 高级吞吐量追踪（基于请求）

为了获得更准确的吞吐量估算（遵循 Chromium 的方法），请使用详细的请求追踪 API：

```cpp
#include "nqe/ThroughputAnalyzer.h"

// 创建带自定义选项的吞吐量分析器
nqe::ThroughputAnalyzer::Options tp_opts;
tp_opts.min_transfer_size_bytes = 32000;  // 32KB 最小值（Chromium 默认）
tp_opts.throughput_min_requests_in_flight = 5;  // 观测窗口的最小并发请求数
tp_opts.decay_lambda_per_sec = 0.02;
tp_opts.freshness_threshold = std::chrono::seconds(60);

// 窗口级挂起检测（Chromium cwnd 启发式算法）
tp_opts.enable_hanging_window_detection = true;
tp_opts.hanging_window_cwnd_multiplier = 0.5;   // cwnd 阈值乘数
tp_opts.hanging_window_cwnd_size_kb = 10;       // 假定的 cwnd 大小（KB）

nqe::ThroughputAnalyzer analyzer(tp_opts);

// 追踪 HTTP 请求生命周期
void* request_id = /* 您的请求标识符 */;

// 1. 开始事务
nqe::ThroughputAnalyzer::Request req{
  .id = request_id,
  .method = "GET",  // 仅 GET 请求用于吞吐量计算
  .bytes_received = 0,
  .start_time = nqe::Clock::now(),
  .is_cached = false,  // 如果来自缓存则设为 true
  .is_hanging = false  // 如果请求停滞则设为 true
};
analyzer.notifyStartTransaction(req);

// 2. 当字节到达时更新
analyzer.notifyBytesRead(request_id, bytes_count);

// 3. 完成请求
analyzer.notifyRequestCompleted(request_id);

// 获取吞吐量估算
auto throughput = analyzer.getEstimate();
if (throughput) {
  std::cout << "吞吐量: " << *throughput << " kbps\n";
}

// 获取包含请求追踪的详细统计信息
auto stats = analyzer.getStatistics();
std::cout << "活动请求: " << stats.active_requests << "\n";
std::cout << "降低准确性的请求: " << stats.degrading_requests << "\n";

// 处理网络变化
analyzer.onConnectionTypeChanged();  // 重置观测窗口
```

**关键特性：**
- **请求过滤**：仅追踪 GET 请求（POST/PUT/等被忽略）
- **准确性保持**：缓存和挂起的请求分离到降低准确性集合
- **观测窗口**：仅当 ≥5 个并发有效请求时计算吞吐量
- **自动限制**：最多追踪 300 个请求，32KB 最小传输大小
- **网络变化感知**：连接类型变化时重置状态


### 使用观察者

```cpp
#include "nqe/NetworkQualityObserver.h"

// 创建自定义观察者
class MyRTTObserver : public nqe::RTTObserver {
public:
  void onRTTObservation(double rtt_ms, const char* source) override {
    std::cout << "新 RTT: " << rtt_ms << "ms 来自 " << source << std::endl;
  }
};

// 注册观察者
MyRTTObserver rtt_observer;
estimator.addRTTObserver(&rtt_observer);

// 稍后：取消注册
estimator.removeRTTObserver(&rtt_observer);
```

### 有效连接类型

```cpp
#include "nqe/EffectiveConnectionType.h"

// 获取当前 ECT
auto ect = estimator.getEffectiveConnectionType();
std::cout << "连接类型: " 
          << nqe::effectiveConnectionTypeToString(ect) << std::endl;

// 自定义 ECT 阈值
nqe::Nqe::Options opts;
opts.ect_thresholds.http_rtt_3g = 500;  // 3G 阈值
opts.ect_thresholds.downstream_throughput_3g = 600;  // 600 kbps
```

## 集成 QUIC/H2 PING

使用 `include/nqe/QuicH2PingSource.h`：
- 通过 `setPingImpl(...)` 提供 Ping 实现
- 收到 PONG 时调用 `onPong(authority)` 以记录 RTT 样本

## 注意事项和限制

- 套接字注册通过以下方式完成：
  - `CURLOPT_SOCKOPTFUNCTION`（注册）和
  - `CURLOPT_CLOSESOCKETFUNCTION`（取消注册）
- 某些平台限制对非拥有套接字的 TCP_INFO 访问；此采样器仅针对进程拥有的套接字（libcurl 创建的套接字）
- 在不支持 `SIO_TCP_INFO` 的旧版 Windows 上，可能无法收集传输层 RTT；HTTP TTFB 仍然可用
- 时间加权中位数参数（`decay_lambda_per_sec`）和偏向下界的偏置可通过 `Nqe::Options` 配置

## 项目结构

```
.
├── CMakeLists.txt
├── .gitignore
├── include/
│   └── nqe/
│       ├── Nqe.h                        # 主 NQE 接口
│       ├── EffectiveConnectionType.h    # ECT 分类
│       ├── ThroughputAnalyzer.h         # 吞吐量估算
│       ├── NetworkChangeNotifier.h      # 网络变化检测
│       ├── NetworkQualityObserver.h     # 观察者接口
│       ├── HttpRttSource.h              # HTTP RTT 辅助工具
│       ├── QuicH2PingSource.h           # QUIC/H2 PING 辅助工具
│       └── Logger.h                     # 日志框架
└── src/
    ├── Nqe.cpp
    ├── EffectiveConnectionType.cpp
    ├── ThroughputAnalyzer.cpp
    ├── NetworkChangeNotifier.cpp        # 跨平台网络监控
    ├── aggregators/
    │   ├── Aggregator.h             # 共享聚合器包装（最小值/最大值/百分位数）
    │   ├── WeightedMedian.h             # 时间加权中位数（基于 deque）
    │   └── Combiner.h                   # RTT 组合逻辑
    ├── libcurl_multi_nqe_example.cpp
    ├── extended_features_test.cpp
    ├── http_rtt_example.cpp
    └── ping_rtt_example.cpp
```

## 最佳实践

### 线程安全
- `Nqe`、`ThroughputAnalyzer` 的所有公共方法都是线程安全的
- `HttpRttSource` 和 `QuicH2PingSource` 是线程安全的
- `Nqe` 使用 `std::recursive_mutex` 以允许在持有锁时安全调用观察者回调
- `Nqe` 的拷贝/移动构造函数已被显式删除以防止不安全共享
- 观察者回调在锁外调用以防止死锁
- 您可以从不同线程调用 `addSample()`、`addThroughputSample()`、`getEstimate()` 和 `getStatistics()`
- 使用 `NetworkChangeNotifier::shutdown()` 在静态析构前进行显式清理

### 性能考虑
- 传输层采样器在单独的线程中运行，采样周期可配置
- 避免将 `transport_sample_period` 设置得太低（< 100ms）以减少开销
- 统计计算涉及排序，因此请谨慎调用 `getStatistics()`
- 观察者通知是同步的；保持观察者回调轻量级

### 错误处理
- 创建实例前始终使用 `Nqe::validateOptions()` 验证选项
- 当没有样本可用时，检查估算字段中的 `std::nullopt`
- 启用日志记录以在开发期间诊断问题
- 低于 `min_throughput_transfer_bytes` 的吞吐量样本会自动过滤

### 样本管理
- 当超过内部限制（每个源 512 个）时，样本会自动修剪
- 基于 `freshness_threshold` 自动丢弃旧样本
- 时间加权中位数自然地对较旧样本赋予较低权重

## 日志记录

该库包含一个灵活的日志框架：

```cpp
// 设置最小日志级别
nqe::Logger::instance().setMinLevel(nqe::LogLevel::DEBUG);  // DEBUG, INFO, WARNING, ERROR

// 设置自定义日志回调
nqe::Logger::instance().setCallback([](nqe::LogLevel level, const std::string& msg) {
  // 您的自定义日志实现
  std::cout << "[" << nqe::logLevelToString(level) << "] " << msg << std::endl;
});
```

可用的日志级别：
- `DEBUG`：用于调试的详细信息（样本添加、套接字注册）
- `INFO`：一般信息性消息（初始化、状态变化）
- `WARNING`：警告消息（当前未使用）
- `ERROR`：错误条件（验证失败）

## 配置选项

`Nqe::Options` 结构提供以下配置：

- `decay_lambda_per_sec`（默认：0.02）：时间加权样本的衰减率。更高的值更偏向近期样本。
- `transport_sample_period`（默认：1000ms）：轮询 TCP_INFO 获取传输层 RTT 的频率。
- `combine_bias_to_lower`（默认：0.6）：组合下界/上界时的偏置因子（0-1）。更高的值更偏向下界。
- `freshness_threshold`（默认：60s）：样本被认为过时之前的最大年龄。
- `min_throughput_transfer_bytes`（默认：10000）：用于吞吐量计算的最小传输大小。
- `ect_thresholds`：ECT 计算的阈值配置。

## 统计信息

该库追踪每个数据源的全面统计信息：

### RTT 数据源（HTTP、传输层、PING）
- **样本计数**：收集的样本总数
- **最小值/最大值**：观察到的最小和最大 RTT 值
- **百分位数**：第 50（中位数）、95、99 百分位值
- **最后样本时间**：最近样本的时间戳

### 吞吐量
- **样本计数**：吞吐量测量次数
- **最小值/最大值**：观察到的最小和最大吞吐量（kbps）
- **百分位数**：第 50、95、99 百分位吞吐量值
- **最后样本时间**：最近测量的时间戳

### 总体
- **活动套接字**：当前注册的 TCP 套接字数量
- **总样本数**：所有 RTT 和吞吐量样本的总和
- **有效连接类型**：当前网络质量分类

## 实现细节

本实现灵感来自 Chromium 的网络质量估算器（NQE）：
https://chromium.googlesource.com/chromium/src/+/HEAD/net/nqe

从 Chromium 的 NQE 实现的关键功能：
- **有效连接类型（ECT）**：基于 RTT 和吞吐量的网络质量分类
- **观测源**：12+ 细粒度源（HTTP、TCP、QUIC、H2_PINGS、H3_PINGS、CACHED_HTTP 等）
- **时间加权聚合**：指数衰减偏好近期样本
- **双因子加权**：时间衰减结合信号强度加权
- **端到端 RTT 追踪**：对完整请求-响应周期测量的单独类别
- **HTTP RTT 边界**：自动强制执行传输层 ≤ HTTP ≤ 端到端约束
- **HTTP RTT 调整**：当样本数 < 30 时与端到端 RTT 智能混合
- **基于 ECT 的钳制**：最大吞吐量受网络质量分类约束
- **挂起窗口检测**：Chromium cwnd 启发式算法过滤停滞的 TCP 观测
- **信号强度追踪**：信号感知观测加权的基础设施
- **观察者模式**：RTT、吞吐量和 ECT 变化的通知
- **全面的统计信息**：每个源的百分位数、最小/最大值追踪
- **可配置阈值**：可自定义的 ECT 阈值和估计参数
- **网络变化检测**：带自动样本管理的跨平台监控

与 Chromium 实现的差异：
- 为跨平台 C++17 简化（无 Chromium 依赖）
- 直接 libcurl 集成示例
- 独立库设计，便于集成

## 许可证

此示例仅供演示目的，生产使用可能需要额外的加固。
