/*
 * Copyright (C) 2006 The Android Open Source Project
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

package android.app;

import android.content.ComponentName;
import android.content.Intent;
import android.os.Bundle;
import android.view.ContextThemeWrapper;
import android.view.Window;

/** Bounded API19 client; platform methods are supplied by integration overlays. */
public class Activity extends ContextThemeWrapper {
    public static final int RESULT_CANCELED = 0;
    public static final int RESULT_OK = -1;
    public static final int RESULT_FIRST_USER = 1;
    private ComponentName mComponent;
    Intent mIntent;
    private Window mWindow;
    private int mResultCode = RESULT_CANCELED;
    private Intent mResultData;

    public Activity() { super(); }

    public void startActivityForResult(Intent intent, int requestCode) {
        startActivityForResult(intent, requestCode, null);
    }

    public final void setResult(int resultCode) {
        synchronized (this) {
            mResultCode = resultCode;
            mResultData = null;
        }
    }

    public final void setResult(int resultCode, Intent data) {
        synchronized (this) {
            mResultCode = resultCode;
            mResultData = data;
        }
    }

    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
    }

    public void startActivityForResult(Intent intent, int requestCode, Bundle options) {
        nativeStartActivityForResult(intent, requestCode, options);
    }

    public void finish() {
        int code;
        Intent data;
        synchronized (this) {
            code = mResultCode;
            data = mResultData;
        }
        nativeFinish(code, data == null ? null : new Intent(data));
    }

    private native void nativeStartActivityForResult(Intent intent, int requestCode, Bundle options);
    private native void nativeFinish(int resultCode, Intent data);
}
