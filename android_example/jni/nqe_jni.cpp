/**
 * JNI bridge for NQE (Network Quality Estimator)
 *
 * Exposes core NQE functionality to Java/Android:
 *  - Create/destroy NQE instance
 *  - Add RTT and throughput samples
 *  - Get estimates and effective connection type
 *  - Network change detection
 *  - Statistics
 */

#include "nqe/Nqe.h"
#include "nqe/NetworkChangeNotifier.h"
#include "nqe/EffectiveConnectionType.h"

#include <jni.h>
#include <android/log.h>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#define LOG_TAG "NQE-JNI"
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace {

std::mutex g_mutex;
std::unordered_map<jlong, std::unique_ptr<nqe::Nqe>> g_instances;
jlong g_next_handle = 1;

nqe::Nqe* getNqe(jlong handle) {
    auto it = g_instances.find(handle);
    if (it == g_instances.end()) return nullptr;
    return it->second.get();
}

nqe::Source mapSource(jint source) {
    switch (source) {
        case 0: return nqe::Source::HTTP_TTFB;
        case 1: return nqe::Source::TRANSPORT_RTT;
        case 2: return nqe::Source::PING_RTT;
        case 3: return nqe::Source::TCP;
        case 4: return nqe::Source::QUIC;
        case 5: return nqe::Source::H2_PINGS;
        case 6: return nqe::Source::H3_PINGS;
        default: return nqe::Source::HTTP_TTFB;
    }
}

} // anonymous namespace

extern "C" {

// ─── Lifecycle ───────────────────────────────────────────────────────────────

JNIEXPORT jlong JNICALL
Java_com_nqe_NqeManager_nativeCreate(JNIEnv* /*env*/, jclass /*clazz*/) {
    std::lock_guard<std::mutex> lock(g_mutex);
    jlong handle = g_next_handle++;
    g_instances[handle] = std::make_unique<nqe::Nqe>();
    return handle;
}

JNIEXPORT jlong JNICALL
Java_com_nqe_NqeManager_nativeCreateWithOptions(
        JNIEnv* /*env*/, jclass /*clazz*/,
        jdouble decayLambda,
        jlong transportSamplePeriodMs,
        jdouble combineBias,
        jlong freshnessThresholdSec,
        jboolean enableCaching,
        jint maxCacheSize) {
    std::lock_guard<std::mutex> lock(g_mutex);
    nqe::Nqe::Options opts;
    opts.decay_lambda_per_sec = decayLambda;
    opts.transport_sample_period = std::chrono::milliseconds(transportSamplePeriodMs);
    opts.combine_bias_to_lower = combineBias;
    opts.freshness_threshold = std::chrono::seconds(freshnessThresholdSec);
    opts.enable_caching = enableCaching;
    opts.max_cache_size = static_cast<size_t>(maxCacheSize);

    jlong handle = g_next_handle++;
    g_instances[handle] = std::make_unique<nqe::Nqe>(opts);
    return handle;
}

JNIEXPORT void JNICALL
Java_com_nqe_NqeManager_nativeDestroy(JNIEnv* /*env*/, jclass /*clazz*/, jlong handle) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_instances.erase(handle);
}

// ─── Samples ─────────────────────────────────────────────────────────────────

JNIEXPORT void JNICALL
Java_com_nqe_NqeManager_nativeAddRttSample(
        JNIEnv* /*env*/, jclass /*clazz*/,
        jlong handle, jint source, jdouble rttMs) {
    std::lock_guard<std::mutex> lock(g_mutex);
    nqe::Nqe* nqe = getNqe(handle);
    if (!nqe) return;
    nqe->addSample(mapSource(source), rttMs);
}

JNIEXPORT void JNICALL
Java_com_nqe_NqeManager_nativeAddThroughputSample(
        JNIEnv* /*env*/, jclass /*clazz*/,
        jlong handle, jlong bytes, jlong durationMs) {
    std::lock_guard<std::mutex> lock(g_mutex);
    nqe::Nqe* nqe = getNqe(handle);
    if (!nqe) return;
    auto now = nqe::Clock::now();
    auto start = now - std::chrono::milliseconds(durationMs);
    nqe->addThroughputSample(static_cast<size_t>(bytes), start, now);
}

// ─── Estimates ───────────────────────────────────────────────────────────────

/**
 * Returns a double[] with the following layout:
 *   [0] rtt_ms
 *   [1] http_ttfb_ms    (-1 if absent)
 *   [2] transport_rtt_ms (-1 if absent)
 *   [3] ping_rtt_ms      (-1 if absent)
 *   [4] end_to_end_rtt_ms(-1 if absent)
 *   [5] throughput_kbps   (-1 if absent)
 *   [6] effective_type (ordinal)
 */
JNIEXPORT jdoubleArray JNICALL
Java_com_nqe_NqeManager_nativeGetEstimate(JNIEnv* env, jclass /*clazz*/, jlong handle) {
    std::lock_guard<std::mutex> lock(g_mutex);
    nqe::Nqe* nqe = getNqe(handle);
    if (!nqe) return nullptr;

    nqe::Estimate est = nqe->getEstimate();
    jdouble buf[7];
    buf[0] = est.rtt_ms;
    buf[1] = est.http_ttfb_ms.value_or(-1.0);
    buf[2] = est.transport_rtt_ms.value_or(-1.0);
    buf[3] = est.ping_rtt_ms.value_or(-1.0);
    buf[4] = est.end_to_end_rtt_ms.value_or(-1.0);
    buf[5] = est.throughput_kbps.value_or(-1.0);
    buf[6] = static_cast<jdouble>(static_cast<int>(est.effective_type));

    jdoubleArray result = env->NewDoubleArray(7);
    if (result) {
        env->SetDoubleArrayRegion(result, 0, 7, buf);
    }
    return result;
}

JNIEXPORT jint JNICALL
Java_com_nqe_NqeManager_nativeGetEffectiveConnectionType(
        JNIEnv* /*env*/, jclass /*clazz*/, jlong handle) {
    std::lock_guard<std::mutex> lock(g_mutex);
    nqe::Nqe* nqe = getNqe(handle);
    if (!nqe) return 0;
    return static_cast<jint>(nqe->getEffectiveConnectionType());
}

// ─── Network change detection ────────────────────────────────────────────────

JNIEXPORT void JNICALL
Java_com_nqe_NqeManager_nativeEnableNetworkChangeDetection(
        JNIEnv* /*env*/, jclass /*clazz*/, jlong handle, jboolean clearOnChange) {
    std::lock_guard<std::mutex> lock(g_mutex);
    nqe::Nqe* nqe = getNqe(handle);
    if (!nqe) return;
    nqe->enableNetworkChangeDetection(clearOnChange);
}

JNIEXPORT void JNICALL
Java_com_nqe_NqeManager_nativeDisableNetworkChangeDetection(
        JNIEnv* /*env*/, jclass /*clazz*/, jlong handle) {
    std::lock_guard<std::mutex> lock(g_mutex);
    nqe::Nqe* nqe = getNqe(handle);
    if (!nqe) return;
    nqe->disableNetworkChangeDetection();
}

// ─── Statistics ──────────────────────────────────────────────────────────────

/**
 * Returns a double[] with the following layout:
 *   [0] total_samples
 *   [1] http sample_count
 *   [2] http percentile_50th   (-1 if absent)
 *   [3] transport sample_count
 *   [4] transport percentile_50th (-1 if absent)
 *   [5] ping sample_count
 *   [6] ping percentile_50th   (-1 if absent)
 *   [7] throughput sample_count
 *   [8] throughput percentile_50th (-1 if absent)
 *   [9] effective_type (ordinal)
 */
JNIEXPORT jdoubleArray JNICALL
Java_com_nqe_NqeManager_nativeGetStatistics(JNIEnv* env, jclass /*clazz*/, jlong handle) {
    std::lock_guard<std::mutex> lock(g_mutex);
    nqe::Nqe* nqe = getNqe(handle);
    if (!nqe) return nullptr;

    nqe::Statistics stats = nqe->getStatistics();
    jdouble buf[10];
    buf[0] = static_cast<jdouble>(stats.total_samples);
    buf[1] = static_cast<jdouble>(stats.http.sample_count);
    buf[2] = stats.http.percentile_50th.value_or(-1.0);
    buf[3] = static_cast<jdouble>(stats.transport.sample_count);
    buf[4] = stats.transport.percentile_50th.value_or(-1.0);
    buf[5] = static_cast<jdouble>(stats.ping.sample_count);
    buf[6] = stats.ping.percentile_50th.value_or(-1.0);
    buf[7] = static_cast<jdouble>(stats.throughput.sample_count);
    buf[8] = stats.throughput.percentile_50th.value_or(-1.0);
    buf[9] = static_cast<jdouble>(static_cast<int>(stats.effective_type));

    jdoubleArray result = env->NewDoubleArray(10);
    if (result) {
        env->SetDoubleArrayRegion(result, 0, 10, buf);
    }
    return result;
}

// ─── Socket management ──────────────────────────────────────────────────────

JNIEXPORT void JNICALL
Java_com_nqe_NqeManager_nativeRegisterSocket(
        JNIEnv* /*env*/, jclass /*clazz*/, jlong handle, jint fd) {
    std::lock_guard<std::mutex> lock(g_mutex);
    nqe::Nqe* nqe = getNqe(handle);
    if (!nqe) return;
    nqe->registerTcpSocket(static_cast<int>(fd));
}

JNIEXPORT void JNICALL
Java_com_nqe_NqeManager_nativeUnregisterSocket(
        JNIEnv* /*env*/, jclass /*clazz*/, jlong handle, jint fd) {
    std::lock_guard<std::mutex> lock(g_mutex);
    nqe::Nqe* nqe = getNqe(handle);
    if (!nqe) return;
    nqe->unregisterTcpSocket(static_cast<int>(fd));
}

JNIEXPORT void JNICALL
Java_com_nqe_NqeManager_nativeStartTransportSampler(
        JNIEnv* /*env*/, jclass /*clazz*/, jlong handle) {
    std::lock_guard<std::mutex> lock(g_mutex);
    nqe::Nqe* nqe = getNqe(handle);
    if (!nqe) return;
    nqe->startTransportSampler();
}

JNIEXPORT void JNICALL
Java_com_nqe_NqeManager_nativeStopTransportSampler(
        JNIEnv* /*env*/, jclass /*clazz*/, jlong handle) {
    std::lock_guard<std::mutex> lock(g_mutex);
    nqe::Nqe* nqe = getNqe(handle);
    if (!nqe) return;
    nqe->stopTransportSampler();
}

} // extern "C"
