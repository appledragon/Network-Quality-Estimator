package com.nqe;

/**
 * Java wrapper for the native NQE (Network Quality Estimator) library.
 *
 * <p>Usage:
 * <pre>
 *   // 1. Load native library (once, e.g. in Application.onCreate())
 *   System.loadLibrary("nqe");
 *
 *   // 2. Create manager
 *   NqeManager manager = new NqeManager();
 *
 *   // 3. Optionally enable network change detection
 *   NetworkChangeNotifier notifier = new NetworkChangeNotifier(context);
 *   notifier.start();
 *   manager.enableNetworkChangeDetection(true);
 *
 *   // 4. Feed samples (from your HTTP stack or other sources)
 *   manager.addRttSample(NqeManager.SOURCE_HTTP_TTFB, ttfbMs);
 *   manager.addThroughputSample(bytesTransferred, durationMs);
 *
 *   // 5. Query results
 *   NqeManager.Estimate est = manager.getEstimate();
 *   int ect = manager.getEffectiveConnectionType();
 *
 *   // 6. Cleanup
 *   manager.destroy();
 *   notifier.stop();
 * </pre>
 */
public class NqeManager {

    // ── RTT source constants (must match nqe::Source enum) ──────────────────
    public static final int SOURCE_HTTP_TTFB     = 0;
    public static final int SOURCE_TRANSPORT_RTT = 1;
    public static final int SOURCE_PING_RTT      = 2;
    public static final int SOURCE_TCP           = 3;
    public static final int SOURCE_QUIC          = 4;
    public static final int SOURCE_H2_PINGS      = 5;
    public static final int SOURCE_H3_PINGS      = 6;

    // ── Effective Connection Type constants (must match nqe::EffectiveConnectionType) ─
    public static final int ECT_UNKNOWN  = 0;
    public static final int ECT_OFFLINE  = 1;
    public static final int ECT_SLOW_2G  = 2;
    public static final int ECT_2G       = 3;
    public static final int ECT_3G       = 4;
    public static final int ECT_4G       = 5;

    /** Opaque handle to the native NQE instance. 0 means destroyed. */
    private long nativeHandle;

    /**
     * Simple data holder for network quality estimates returned by {@link #getEstimate()}.
     */
    public static class Estimate {
        public double rttMs;
        public double httpTtfbMs;       // -1 if unavailable
        public double transportRttMs;   // -1 if unavailable
        public double pingRttMs;        // -1 if unavailable
        public double endToEndRttMs;    // -1 if unavailable
        public double throughputKbps;   // -1 if unavailable
        public int    effectiveType;    // one of ECT_* constants
    }

    /**
     * Simple data holder for NQE statistics returned by {@link #getStatistics()}.
     */
    public static class Statistics {
        public long   totalSamples;
        public long   httpSamples;
        public double httpMedianMs;          // -1 if unavailable
        public long   transportSamples;
        public double transportMedianMs;     // -1 if unavailable
        public long   pingSamples;
        public double pingMedianMs;          // -1 if unavailable
        public long   throughputSamples;
        public double throughputMedianKbps;  // -1 if unavailable
        public int    effectiveType;
    }

    // ── Constructors ────────────────────────────────────────────────────────

    /** Create an NQE instance with default options. */
    public NqeManager() {
        nativeHandle = nativeCreate();
    }

    /**
     * Create an NQE instance with custom options.
     *
     * @param decayLambda              Sample decay rate (higher = favour recent samples)
     * @param transportSamplePeriodMs  TCP_INFO polling interval in ms
     * @param combineBias              Bias toward lower bound [0,1]
     * @param freshnessThresholdSec    Max sample age in seconds before considered stale
     * @param enableCaching            Enable network quality caching
     * @param maxCacheSize             Maximum number of cached networks
     */
    public NqeManager(double decayLambda,
                      long transportSamplePeriodMs,
                      double combineBias,
                      long freshnessThresholdSec,
                      boolean enableCaching,
                      int maxCacheSize) {
        nativeHandle = nativeCreateWithOptions(
                decayLambda, transportSamplePeriodMs, combineBias,
                freshnessThresholdSec, enableCaching, maxCacheSize);
    }

    // ── Public API ──────────────────────────────────────────────────────────

    /**
     * Release native resources. Must be called when the manager is no longer needed.
     * After this call, all other methods become no-ops.
     */
    public synchronized void destroy() {
        if (nativeHandle != 0) {
            nativeDestroy(nativeHandle);
            nativeHandle = 0;
        }
    }

    /**
     * Add an RTT observation.
     *
     * @param source One of {@code SOURCE_*} constants
     * @param rttMs  Round-trip time in milliseconds
     */
    public void addRttSample(int source, double rttMs) {
        if (nativeHandle != 0) {
            nativeAddRttSample(nativeHandle, source, rttMs);
        }
    }

    /**
     * Add a throughput observation.
     *
     * @param bytes      Number of bytes transferred
     * @param durationMs Transfer duration in milliseconds
     */
    public void addThroughputSample(long bytes, long durationMs) {
        if (nativeHandle != 0) {
            nativeAddThroughputSample(nativeHandle, bytes, durationMs);
        }
    }

    /**
     * Get the current combined network quality estimate.
     *
     * @return Estimate object, or {@code null} if the native instance is destroyed
     */
    public Estimate getEstimate() {
        if (nativeHandle == 0) return null;
        double[] raw = nativeGetEstimate(nativeHandle);
        if (raw == null) return null;
        Estimate e = new Estimate();
        e.rttMs          = raw[0];
        e.httpTtfbMs     = raw[1];
        e.transportRttMs = raw[2];
        e.pingRttMs      = raw[3];
        e.endToEndRttMs  = raw[4];
        e.throughputKbps = raw[5];
        e.effectiveType  = (int) raw[6];
        return e;
    }

    /**
     * Get the current Effective Connection Type.
     *
     * @return One of {@code ECT_*} constants
     */
    public int getEffectiveConnectionType() {
        if (nativeHandle == 0) return ECT_UNKNOWN;
        return nativeGetEffectiveConnectionType(nativeHandle);
    }

    /**
     * Get human-readable label for an ECT constant.
     */
    public static String ectToString(int ect) {
        switch (ect) {
            case ECT_UNKNOWN:  return "Unknown";
            case ECT_OFFLINE:  return "Offline";
            case ECT_SLOW_2G:  return "Slow-2G";
            case ECT_2G:       return "2G";
            case ECT_3G:       return "3G";
            case ECT_4G:       return "4G";
            default:           return "Invalid";
        }
    }

    /**
     * Enable automatic network change detection.
     *
     * @param clearOnChange If {@code true}, clear all accumulated samples when the network changes
     */
    public void enableNetworkChangeDetection(boolean clearOnChange) {
        if (nativeHandle != 0) {
            nativeEnableNetworkChangeDetection(nativeHandle, clearOnChange);
        }
    }

    /** Disable automatic network change detection. */
    public void disableNetworkChangeDetection() {
        if (nativeHandle != 0) {
            nativeDisableNetworkChangeDetection(nativeHandle);
        }
    }

    /**
     * Get detailed statistics across all RTT sources and throughput.
     *
     * @return Statistics object, or {@code null} if the native instance is destroyed
     */
    public Statistics getStatistics() {
        if (nativeHandle == 0) return null;
        double[] raw = nativeGetStatistics(nativeHandle);
        if (raw == null) return null;
        Statistics s = new Statistics();
        s.totalSamples       = (long) raw[0];
        s.httpSamples        = (long) raw[1];
        s.httpMedianMs       = raw[2];
        s.transportSamples   = (long) raw[3];
        s.transportMedianMs  = raw[4];
        s.pingSamples        = (long) raw[5];
        s.pingMedianMs       = raw[6];
        s.throughputSamples  = (long) raw[7];
        s.throughputMedianKbps = raw[8];
        s.effectiveType      = (int) raw[9];
        return s;
    }

    // ── Socket / transport sampler ──────────────────────────────────────────

    /** Register a TCP socket file descriptor for transport-layer RTT sampling. */
    public void registerSocket(int fd) {
        if (nativeHandle != 0) nativeRegisterSocket(nativeHandle, fd);
    }

    /** Unregister a previously registered TCP socket. */
    public void unregisterSocket(int fd) {
        if (nativeHandle != 0) nativeUnregisterSocket(nativeHandle, fd);
    }

    /** Start the periodic transport RTT sampler (reads TCP_INFO). */
    public void startTransportSampler() {
        if (nativeHandle != 0) nativeStartTransportSampler(nativeHandle);
    }

    /** Stop the periodic transport RTT sampler. */
    public void stopTransportSampler() {
        if (nativeHandle != 0) nativeStopTransportSampler(nativeHandle);
    }

    // ── Native methods ──────────────────────────────────────────────────────

    private static native long   nativeCreate();
    private static native long   nativeCreateWithOptions(
            double decayLambda, long transportSamplePeriodMs,
            double combineBias, long freshnessThresholdSec,
            boolean enableCaching, int maxCacheSize);
    private static native void   nativeDestroy(long handle);

    private static native void   nativeAddRttSample(long handle, int source, double rttMs);
    private static native void   nativeAddThroughputSample(long handle, long bytes, long durationMs);

    private static native double[] nativeGetEstimate(long handle);
    private static native int    nativeGetEffectiveConnectionType(long handle);

    private static native void   nativeEnableNetworkChangeDetection(long handle, boolean clearOnChange);
    private static native void   nativeDisableNetworkChangeDetection(long handle);

    private static native double[] nativeGetStatistics(long handle);

    private static native void   nativeRegisterSocket(long handle, int fd);
    private static native void   nativeUnregisterSocket(long handle, int fd);
    private static native void   nativeStartTransportSampler(long handle);
    private static native void   nativeStopTransportSampler(long handle);
}
