/*
 * Copyright (C) 2008 The Android Open Source Project
 * Copyright (C) 2026 OGPlay contributors
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *     http://www.apache.org/licenses/LICENSE-2.0
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
package android.net.wifi;

/**
 * API19 Wi-Fi lock client state, adapted from AOSP WifiManager.java.
 * Binder/radio operations become bounded process-local native leases.
 * Public nested class names preserve the API19 Wi-Fi lock ABI.
 */
public class WifiManager {
    public static final int WIFI_MODE_FULL = 1;
    public static final int WIFI_MODE_SCAN_ONLY = 2;
    public static final int WIFI_MODE_FULL_HIGH_PERF = 3;
    public static final int WIFI_STATE_DISABLING = 0;
    public static final int WIFI_STATE_DISABLED = 1;
    public static final int WIFI_STATE_ENABLING = 2;
    public static final int WIFI_STATE_ENABLED = 3;
    public static final int WIFI_STATE_UNKNOWN = 4;

    public native boolean isWifiEnabled();
    public native int getWifiState();
    public native boolean setWifiEnabled(boolean enabled);
    public native WifiInfo getConnectionInfo();

    public WifiLock createWifiLock(int lockType, String tag) {
        return new WifiLock(lockType, tag);
    }
    public WifiLock createWifiLock(String tag) { return new WifiLock(WIFI_MODE_FULL, tag); }

    public MulticastLock createMulticastLock(String tag) { return new MulticastLock(tag); }

    public class WifiLock {
        private final int mLockType;
        private final String mTag;
        private final Object mMonitor = new Object();
        private int mRefCount;
        private boolean mRefCounted = true;
        private boolean mHeld;

        private WifiLock(int lockType, String tag) {
            mLockType = lockType;
            mTag = tag;
        }

        public void acquire() {
            synchronized (mMonitor) {
                if (mRefCounted ? (++mRefCount == 1) : (!mHeld)) {
                    nativeAcquire(WifiManager.this, mLockType);
                    mHeld = true;
                }
            }
        }

        public void release() {
            synchronized (mMonitor) {
                if (mRefCounted ? (--mRefCount == 0) : mHeld) {
                    nativeRelease();
                    mHeld = false;
                }
                if (mRefCount < 0) throw new RuntimeException("WifiLock under-locked " + mTag);
            }
        }

        public void setReferenceCounted(boolean refCounted) {
            mRefCounted = refCounted;
        }

        public boolean isHeld() {
            synchronized (mMonitor) { return mHeld; }
        }

        public String toString() {
            synchronized (mMonitor) {
                return "WifiLock{ " + Integer.toHexString(System.identityHashCode(this)) +
                    "; " + (mHeld ? "held; " : "") +
                    (mRefCounted ? "refcounted: refcount = " + mRefCount : "not refcounted") + " }";
            }
        }

        protected void finalize() throws Throwable {
            super.finalize();
            synchronized (mMonitor) {
                if (mHeld) { nativeRelease(); mHeld = false; }
            }
        }

        private native void nativeAcquire(WifiManager manager, int lockType);
        private native void nativeRelease();
    }

    public class MulticastLock {
        private final String mTag;
        private final Object mMonitor = new Object();
        private int mRefCount;
        private boolean mRefCounted = true;
        private boolean mHeld;

        private MulticastLock(String tag) { mTag = tag; }

        public void acquire() {
            synchronized (mMonitor) {
                if (mRefCounted ? (++mRefCount == 1) : (!mHeld)) {
                    nativeAcquire(WifiManager.this);
                    mHeld = true;
                }
            }
        }

        public void release() {
            synchronized (mMonitor) {
                if (mRefCounted ? (--mRefCount == 0) : mHeld) {
                    nativeRelease();
                    mHeld = false;
                }
                if (mRefCount < 0) throw new RuntimeException("MulticastLock under-locked " + mTag);
            }
        }

        public void setReferenceCounted(boolean refCounted) { mRefCounted = refCounted; }

        public boolean isHeld() {
            synchronized (mMonitor) { return mHeld; }
        }

        public String toString() {
            synchronized (mMonitor) {
                return "MulticastLock{ " + Integer.toHexString(System.identityHashCode(this)) +
                    "; " + (mHeld ? "held; " : "") +
                    (mRefCounted ? "refcounted: refcount = " + mRefCount : "not refcounted") + " }";
            }
        }

        protected void finalize() throws Throwable {
            super.finalize();
            setReferenceCounted(false);
            release();
        }

        private native void nativeAcquire(WifiManager manager);
        private native void nativeRelease();
    }

}
