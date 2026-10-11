/*
 * Copyright (C) 2008 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

package android.net;

/** Bounded API19 client for explicitly configured offline guest topology. */
public class ConnectivityManager {
    public static final int TYPE_MOBILE = 0;
    public static final int TYPE_WIFI = 1;
    public static final int TYPE_MOBILE_MMS = 2;
    public static final int TYPE_MOBILE_SUPL = 3;
    public static final int TYPE_MOBILE_DUN = 4;
    public static final int TYPE_MOBILE_HIPRI = 5;
    public static final int TYPE_WIMAX = 6;
    public static final int TYPE_BLUETOOTH = 7;
    public static final int TYPE_DUMMY = 8;
    public static final int TYPE_ETHERNET = 9;
    public static final int TYPE_MOBILE_FOTA = 10;
    public static final int TYPE_MOBILE_IMS = 11;
    public static final int TYPE_MOBILE_CBS = 12;
    public static final int TYPE_WIFI_P2P = 13;
    public static final int TYPE_MOBILE_IA = 14;
    public static final int MAX_NETWORK_TYPE = TYPE_MOBILE_IA;

    // Original API19 type-range algorithm; valid does not imply supported.
    public static boolean isNetworkTypeValid(int networkType) {
        return networkType >= 0 && networkType <= MAX_NETWORK_TYPE;
    }

    public NetworkInfo getActiveNetworkInfo() {
        nativeRequireOffline();
        return null;
    }

    public NetworkInfo getNetworkInfo(int networkType) {
        return nativeGetNetworkInfo(networkType);
    }

    private native void nativeRequireOffline();
    private native NetworkInfo nativeGetNetworkInfo(int networkType);
}
