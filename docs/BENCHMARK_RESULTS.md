# Network Quality Estimation - Benchmark Results

## Test Overview

This document contains real-world HTTP RTT (Round-Trip Time) measurements using the NQE (Network Quality Estimator) library against 49 popular websites.

**Test Date:** November 5, 2025  
**Tool:** http_rtt_example  
**Library Version:** NQE 1.0  
**Test Location:** Windows Environment  
**Protocol:** HTTPS HEAD Requests  

## Configuration

```cpp
nqe::Nqe::Options opts;
opts.decay_lambda_per_sec = 0.02;
opts.sample_period = 1000ms;
opts.combine_bias_to_lower = 0.6;
```

## Overall Statistics

| Metric | Value |
|--------|-------|
| Total URLs Tested | 49 |
| Successful Requests | 47 (96%) |
| Failed Requests | 2 (4%) |
| Total Samples | 47 |
| Min RTT | 133.592ms |
| Median RTT (p50) | 1102.98ms |
| P95 RTT | 3626.97ms |
| Max RTT | 9013.21ms |
| Final ECT | 3G |

## Detailed Results

### Search Engines & Tech Giants

| Website | RTT (ms) | Status | Notes |
|---------|----------|--------|-------|
| google.com | 368.97 | ✅ Success | Initial 4G ECT |
| baidu.com | 133.59 | ✅ Success | Fastest response |
| bing.com | 951.89 | ✅ Success | - |
| yahoo.com | 1600.43 | ✅ Success | - |
| yandex.com | 9013.21 | ✅ Success | Slowest response |

### Social Media

| Website | RTT (ms) | Status | Notes |
|---------|----------|--------|-------|
| facebook.com | 921 | ✅ Success | - |
| twitter.com | 2687 | ✅ Success | - |
| instagram.com | 1852 | ✅ Success | - |
| linkedin.com | 594 | ✅ Success | - |
| reddit.com | 1102.98 | ✅ Success | - |
| tiktok.com | - | ❌ Failed | SSL connect error |
| weibo.com | 1162 | ✅ Success | - |

### E-commerce

| Website | RTT (ms) | Status | Notes |
|---------|----------|--------|-------|
| amazon.com (1st) | - | ❌ Failed | Timeout (10s) |
| amazon.com (2nd) | 4045 | ✅ Success | - |
| alibaba.com | 428 | ✅ Success | - |
| taobao.com | 203 | ✅ Success | 2nd fastest |
| ebay.com | 1573 | ✅ Success | - |
| jd.com | 380 | ✅ Success | - |

### Technology & Development

| Website | RTT (ms) | Status | Notes |
|---------|----------|--------|-------|
| github.com | 4753 | ✅ Success | - |
| stackoverflow.com | 3307 | ✅ Success | - |
| gitlab.com | 3066 | ✅ Success | - |
| npmjs.com | 631 | ✅ Success | - |
| pypi.org | 2672 | ✅ Success | - |

### Cloud & CDN

| Website | RTT (ms) | Status | Notes |
|---------|----------|--------|-------|
| cloudflare.com | 1505.82 | ✅ Success | - |
| microsoft.com | 537 | ✅ Success | - |
| aws.amazon.com | 1220.3 | ✅ Success | - |
| cloud.google.com | 892 | ✅ Success | - |

### Media & Entertainment

| Website | RTT (ms) | Status | Notes |
|---------|----------|--------|-------|
| youtube.com | 927 | ✅ Success | - |
| netflix.com | 970 | ✅ Success | - |
| twitch.tv | 1103.17 | ✅ Success | - |
| spotify.com | 1247 | ✅ Success | - |
| bilibili.com | 560 | ✅ Success | - |

### News & Information

| Website | RTT (ms) | Status | Notes |
|---------|----------|--------|-------|
| wikipedia.org | 545 | ✅ Success | - |
| bbc.com | 1307 | ✅ Success | - |
| cnn.com | 2073 | ✅ Success | - |
| nytimes.com | 630 | ✅ Success | - |
| xinhuanet.com | 496 | ✅ Success | - |

### Education & Research

| Website | RTT (ms) | Status | Notes |
|---------|----------|--------|-------|
| mit.edu | 641 | ✅ Success | - |
| stanford.edu | 1109 | ✅ Success | - |
| coursera.org | 1434 | ✅ Success | - |
| udemy.com | 1911 | ✅ Success | - |

### Chinese Popular Sites

| Website | RTT (ms) | Status | Notes |
|---------|----------|--------|-------|
| qq.com | 183 | ✅ Success | 3rd fastest |
| 163.com | 467 | ✅ Success | - |
| sina.com.cn | 477 | ✅ Success | - |
| sohu.com | 372 | ✅ Success | - |
| zhihu.com | 257 | ✅ Success | 4th fastest |

### Other Sites

| Website | RTT (ms) | Status | Notes |
|---------|----------|--------|-------|
| apple.com | 1516 | ✅ Success | - |
| adobe.com | 3626.97 | ✅ Success | - |
| wordpress.com | 2180 | ✅ Success | - |

## Performance Ranking

### Top 10 Fastest Responses

1. **baidu.com** - 133.59ms
2. **taobao.com** - 203ms
3. **qq.com** - 183ms
4. **zhihu.com** - 257ms
5. **google.com** - 368.97ms
6. **sohu.com** - 372ms
7. **jd.com** - 380ms
8. **alibaba.com** - 428ms
9. **163.com** - 467ms
10. **sina.com.cn** - 477ms

### Top 10 Slowest Responses

1. **yandex.com** - 9013.21ms
2. **github.com** - 4753ms
3. **amazon.com** - 4045ms
4. **adobe.com** - 3626.97ms
5. **stackoverflow.com** - 3307ms
6. **gitlab.com** - 3066ms
7. **twitter.com** - 2687ms
8. **pypi.org** - 2672ms
9. **wordpress.com** - 2180ms
10. **cnn.com** - 2073ms

## ECT (Effective Connection Type) Transitions

The NQE library dynamically adjusted the ECT during testing based on observed RTTs:

```
Unknown → 4G (after google.com: 368.97ms)
4G → 3G (after yandex.com: 9013.21ms)
3G → 2G (after instagram.com: 1852ms)
2G → 3G (after alibaba.com: 428ms)
3G → 2G (after cloudflare.com: 1505.82ms)
2G → 3G (after bbc.com: 1307ms)
Final: 3G
```

## Failure Analysis

### Failed Requests (2)

1. **tiktok.com** - SSL connect error
   - Possible cause: SSL/TLS handshake failure or certificate verification issue
   
2. **amazon.com (first attempt)** - Timeout (10s)
   - Possible cause: Network congestion or server-side rate limiting
   - Note: Second attempt to amazon.com succeeded with 4045ms RTT

## HTTP TTFB Statistics

```
Sample count: 47
Min: 133.592ms (baidu.com)
Median (p50): 1102.98ms
p95: 3626.97ms (adobe.com)
Max: 9013.21ms (yandex.com)
```

## Geographic Distribution

Based on the results, we can observe:

- **Domestic Chinese sites** (baidu, taobao, qq, zhihu, etc.) showed consistently low latencies (133-560ms)
- **North American sites** varied widely (537ms - 4753ms)
- **European sites** (yandex.com) showed the highest latency at 9013ms
- **CDN-backed sites** (cloudflare, youtube, netflix) showed moderate latencies (892-1505ms)

## Conclusions

1. **Library Performance**: The NQE library successfully measured and analyzed real-world HTTP RTT from 49 diverse websites with a 96% success rate.

2. **ECT Accuracy**: The automatic ECT classification correctly identified network quality degradation and improvement, settling on 3G as the final effective connection type based on the median RTT of ~1.1 seconds.

3. **Geographic Patterns**: Clear correlation between geographic proximity and RTT, with domestic Chinese sites showing the best performance.

4. **Reliability**: High success rate (96%) demonstrates robust error handling and timeout management.

## Test Command

```bash
cd build_windows/Release
./http_rtt_example.exe
```

## Sample Output

```
[INFO] NQE initialized with decay_lambda=0.02, sample_period=1000ms, bias=0.6

Request 0: https://www.google.com
  ✓ Response received (actual: 368ms)
  NQE RTT estimate: 368.967ms (HTTP TTFB: 368.967ms)
  ECT: 4G

...

Final Network Quality:
  RTT: 1102.98ms
  HTTP TTFB: 1102.98ms
  ECT: 3G
```

---

*Generated from actual test run on November 5, 2025*
