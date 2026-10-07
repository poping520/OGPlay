/*
 * Copyright (C) 2010 The Android Open Source Project
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

package android.nfc;

import android.app.Activity;
import android.content.Context;
import android.util.Log;

/** API19 discovery facade for a process with no NFC hardware or transport. */
public final class NfcAdapter {
    private static final String TAG = "NFC";

    public interface CreateNdefMessageCallback {
        /**
         * Called to provide a {@link NdefMessage} to push.
         *
         * <p>This callback is usually made on a binder thread (not the UI thread).
         *
         * <p>Called when this device is in range of another device
         * that might support NDEF push. It allows the application to
         * create the NDEF message only when it is required.
         *
         * <p>NDEF push cannot occur until this method returns, so do not
         * block for too long.
         *
         * <p>The Android operating system will usually show a system UI
         * on top of your activity during this time, so do not try to request
         * input from the user to complete the callback, or provide custom NDEF
         * push UI. The user probably will not see it.
         *
         * @param event {@link NfcEvent} with the {@link NfcEvent#nfcAdapter} field set
         * @return NDEF message to push, or null to not provide a message
         */
        public NdefMessage createNdefMessage(NfcEvent event);
    }

    public interface OnNdefPushCompleteCallback {
        /**
         * Called on successful NDEF push.
         *
         * <p>This callback is usually made on a binder thread (not the UI thread).
         *
         * @param event {@link NfcEvent} with the {@link NfcEvent#nfcAdapter} field set
         * @see #setNdefPushMessageCallback
         */
        public void onNdefPushComplete(NfcEvent event);
    }

    public static NfcAdapter getDefaultAdapter(Context context) {
        if (context == null) {
            throw new IllegalArgumentException("context cannot be null");
        }
        context = context.getApplicationContext();
        if (context == null) {
            throw new IllegalArgumentException(
                    "context not associated with any application (using a mock context?)");
        }
        /* use getSystemService() for consistency */
        NfcManager manager = (NfcManager) context.getSystemService(Context.NFC_SERVICE);
        if (manager == null) {
            // NFC not available
            return null;
        }
        return manager.getDefaultAdapter();
    }

    @Deprecated
    public static NfcAdapter getDefaultAdapter() {
        // introduced in API version 9 (GB 2.3)
        // deprecated in API version 10 (GB 2.3.3)
        // removed from public API in version 16 (ICS MR2)
        // should maintain as a hidden API for binary compatibility for a little longer
        Log.w(TAG, "WARNING: NfcAdapter.getDefaultAdapter() is deprecated, use " +
                "NfcAdapter.getDefaultAdapter(Context) instead", new Exception());

        return NfcAdapter.getNfcAdapter(null);
    }

    /** API19 hidden factory; absence is reported before any adapter is created. */
    public static synchronized NfcAdapter getNfcAdapter(Context context) {
        return nativeGetAdapter();
    }

    NfcAdapter(Context context) {
        nativeUnsupported("constructor");
    }

    public void setNdefPushMessageCallback(CreateNdefMessageCallback callback,
            Activity activity, Activity... activities) {
        nativeUnsupported("setNdefPushMessageCallback");
    }

    public void setOnNdefPushCompleteCallback(OnNdefPushCompleteCallback callback,
            Activity activity, Activity... activities) {
        nativeUnsupported("setOnNdefPushCompleteCallback");
    }

    private static native NfcAdapter nativeGetAdapter();
    private static native void nativeUnsupported(String operation);
}
