package com.nqe;

import android.content.Context;
import android.net.ConnectivityManager;
import android.net.Network;
import android.net.NetworkCapabilities;
import android.net.NetworkRequest;
import android.os.Build;
import android.telephony.TelephonyManager;

/**
 * Android Java wrapper for NQE NetworkChangeNotifier JNI integration.
 * 
 * This class bridges Android's ConnectivityManager with the native C++ NetworkChangeNotifier.
 * It provides network type detection and change callbacks to the native layer.
 * 
 * Usage:
 * 1. Load the native library: System.loadLibrary("nqe");
 * 2. Create instance: NetworkChangeNotifier notifier = new NetworkChangeNotifier(context);
 * 3. Start monitoring: notifier.start();
 * 4. Stop when done: notifier.stop();
 */
public class NetworkChangeNotifier {
    
    // Connection type constants - must match C++ ConnectionType enum
    public static final int CONNECTION_UNKNOWN = 0;
    public static final int CONNECTION_ETHERNET = 1;
    public static final int CONNECTION_WIFI = 2;
    public static final int CONNECTION_CELLULAR_2G = 3;
    public static final int CONNECTION_CELLULAR_3G = 4;
    public static final int CONNECTION_CELLULAR_4G = 5;
    public static final int CONNECTION_CELLULAR_5G = 6;
    public static final int CONNECTION_BLUETOOTH = 7;
    public static final int CONNECTION_NONE = 8;
    
    private Context context;
    private ConnectivityManager connectivityManager;
    private TelephonyManager telephonyManager;
    private NetworkCallback networkCallback;
    
    static {
        // Load native library - uncomment when using in production
        // Users must load the library before using this class, typically in Application.onCreate()
        // or in the Activity that uses NetworkChangeNotifier
        // System.loadLibrary("nqe");
    }
    
    public NetworkChangeNotifier(Context context) {
        this.context = context.getApplicationContext();
        this.connectivityManager = (ConnectivityManager) 
            context.getSystemService(Context.CONNECTIVITY_SERVICE);
        this.telephonyManager = (TelephonyManager)
            context.getSystemService(Context.TELEPHONY_SERVICE);
    }
    
    /**
     * Start monitoring network changes.
     * Initializes JNI and registers network callbacks.
     */
    public void start() {
        // Initialize JNI with this instance as callback
        nativeInit(this);
        
        // Register Android network callback
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.LOLLIPOP) {
            networkCallback = new NetworkCallback();
            NetworkRequest request = new NetworkRequest.Builder()
                .addCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET)
                .build();
            connectivityManager.registerNetworkCallback(request, networkCallback);
        }
    }
    
    /**
     * Stop monitoring network changes.
     * Cleans up JNI and unregisters callbacks.
     */
    public void stop() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.LOLLIPOP && networkCallback != null) {
            connectivityManager.unregisterNetworkCallback(networkCallback);
            networkCallback = null;
        }
        
        nativeCleanup();
    }
    
    /**
     * Get current connection type.
     * Called from native C++ code via JNI.
     * 
     * @return Connection type constant
     */
    public int getConnectionType() {
        if (connectivityManager == null) {
            return CONNECTION_UNKNOWN;
        }
        
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
            Network network = connectivityManager.getActiveNetwork();
            if (network == null) {
                return CONNECTION_NONE;
            }
            
            NetworkCapabilities capabilities = 
                connectivityManager.getNetworkCapabilities(network);
            if (capabilities == null) {
                return CONNECTION_UNKNOWN;
            }
            
            if (capabilities.hasTransport(NetworkCapabilities.TRANSPORT_WIFI)) {
                return CONNECTION_WIFI;
            } else if (capabilities.hasTransport(NetworkCapabilities.TRANSPORT_CELLULAR)) {
                return getCellularConnectionType();
            } else if (capabilities.hasTransport(NetworkCapabilities.TRANSPORT_ETHERNET)) {
                return CONNECTION_ETHERNET;
            } else if (capabilities.hasTransport(NetworkCapabilities.TRANSPORT_BLUETOOTH)) {
                return CONNECTION_BLUETOOTH;
            }
        } else {
            // Fallback for older Android versions
            android.net.NetworkInfo activeNetwork = 
                connectivityManager.getActiveNetworkInfo();
            if (activeNetwork == null || !activeNetwork.isConnected()) {
                return CONNECTION_NONE;
            }
            
            int type = activeNetwork.getType();
            switch (type) {
                case ConnectivityManager.TYPE_WIFI:
                    return CONNECTION_WIFI;
                case ConnectivityManager.TYPE_MOBILE:
                    return getCellularConnectionType();
                case ConnectivityManager.TYPE_ETHERNET:
                    return CONNECTION_ETHERNET;
                case ConnectivityManager.TYPE_BLUETOOTH:
                    return CONNECTION_BLUETOOTH;
            }
        }
        
        return CONNECTION_UNKNOWN;
    }
    
    /**
     * Determine cellular connection generation (2G/3G/4G/5G).
     * 
     * @return Cellular connection type constant
     */
    private int getCellularConnectionType() {
        if (telephonyManager == null) {
            return CONNECTION_CELLULAR_4G; // Default assumption
        }
        
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.N) {
            int dataNetworkType = telephonyManager.getDataNetworkType();
            return getCellularTypeFromNetworkType(dataNetworkType);
        } else {
            int networkType = telephonyManager.getNetworkType();
            return getCellularTypeFromNetworkType(networkType);
        }
    }
    
    /**
     * Map Android network type to our connection type constants.
     */
    private int getCellularTypeFromNetworkType(int networkType) {
        switch (networkType) {
            case TelephonyManager.NETWORK_TYPE_GPRS:
            case TelephonyManager.NETWORK_TYPE_EDGE:
            case TelephonyManager.NETWORK_TYPE_CDMA:
            case TelephonyManager.NETWORK_TYPE_1xRTT:
            case TelephonyManager.NETWORK_TYPE_IDEN:
                return CONNECTION_CELLULAR_2G;
                
            case TelephonyManager.NETWORK_TYPE_UMTS:
            case TelephonyManager.NETWORK_TYPE_EVDO_0:
            case TelephonyManager.NETWORK_TYPE_EVDO_A:
            case TelephonyManager.NETWORK_TYPE_HSDPA:
            case TelephonyManager.NETWORK_TYPE_HSUPA:
            case TelephonyManager.NETWORK_TYPE_HSPA:
            case TelephonyManager.NETWORK_TYPE_EVDO_B:
            case TelephonyManager.NETWORK_TYPE_EHRPD:
            case TelephonyManager.NETWORK_TYPE_HSPAP:
                return CONNECTION_CELLULAR_3G;
                
            case TelephonyManager.NETWORK_TYPE_LTE:
            case TelephonyManager.NETWORK_TYPE_IWLAN:
                return CONNECTION_CELLULAR_4G;
                
            // 5G types (API 29+)
            // Note: Using magic number 20 for NETWORK_TYPE_NR to support compilation
            // on older Android SDK versions that don't have this constant
            case 20: // TelephonyManager.NETWORK_TYPE_NR (added in API 29)
                return CONNECTION_CELLULAR_5G;
                
            default:
                return CONNECTION_CELLULAR_4G; // Default to 4G for unknown types
        }
    }
    
    /**
     * Android NetworkCallback to receive network change events.
     */
    private class NetworkCallback extends ConnectivityManager.NetworkCallback {
        @Override
        public void onAvailable(Network network) {
            notifyNetworkChanged();
        }
        
        @Override
        public void onLost(Network network) {
            notifyNetworkChanged();
        }
        
        @Override
        public void onCapabilitiesChanged(Network network, NetworkCapabilities capabilities) {
            notifyNetworkChanged();
        }
        
        private void notifyNetworkChanged() {
            int type = getConnectionType();
            nativeOnNetworkChanged(type);
        }
    }
    
    // Native JNI methods
    
    /**
     * Initialize native NetworkChangeNotifier with JNI callback.
     * 
     * @param callback Java object with getConnectionType() method
     */
    private static native void nativeInit(Object callback);
    
    /**
     * Cleanup native NetworkChangeNotifier JNI resources.
     */
    private static native void nativeCleanup();
    
    /**
     * Notify native code of network change.
     * 
     * @param networkType Current network connection type
     */
    private static native void nativeOnNetworkChanged(int networkType);
}
