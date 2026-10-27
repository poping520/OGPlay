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
import android.util.SparseArray;

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
    boolean mFinished;
    private boolean mDestroyed;

    public Activity() { super(); }

    public Window getWindow() { return mWindow; } // integration supplies lazy Window ownership
    public android.view.LayoutInflater getLayoutInflater() { return getWindow().getLayoutInflater(); }

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
        mFinished = true;
    }

    // 原版：经 AMS 请求 WMS 用 enterAnim/exitAnim 替换紧随 startActivity/finish 的窗口切换动画；
    // 仅调用方处于 RESUMED/PAUSING 且已有挂起切换时生效，否则忽略，且无返回值、不抛异常。
    // 现在：OGPlay 无窗口切换动画，Activity 切换立即完成，等同原版无挂起切换分支，接受并忽略。
    public void overridePendingTransition(int enterAnim, int exitAnim) {
    }

    public boolean isFinishing() { return mFinished; }
    public boolean isDestroyed() { return mDestroyed; }

    final void performDestroy() {
        mDestroyed = true;
        onDestroy();
    }

    private static final class ManagedDialog {
        Dialog mDialog;
        Bundle mArgs;
    }
    private SparseArray<ManagedDialog> mManagedDialogs;

    protected Dialog onCreateDialog(int id) { return null; }
    protected Dialog onCreateDialog(int id, Bundle args) { return onCreateDialog(id); }
    protected void onPrepareDialog(int id, Dialog dialog) { dialog.setOwnerActivity(this); }
    protected void onPrepareDialog(int id, Dialog dialog, Bundle args) { onPrepareDialog(id, dialog); }

    public final void showDialog(int id) { showDialog(id, null); }
    public final boolean showDialog(int id, Bundle args) {
        if (mManagedDialogs == null) mManagedDialogs = new SparseArray<ManagedDialog>();
        ManagedDialog md = mManagedDialogs.get(id);
        if (md == null) {
            md = new ManagedDialog();
            md.mDialog = onCreateDialog(id, args);
            if (md.mDialog == null) return false;
            md.mDialog.dispatchOnCreate(null);
            mManagedDialogs.put(id, md);
        }
        md.mArgs = args;
        onPrepareDialog(id, md.mDialog, args);
        if (mManagedDialogs == null || mManagedDialogs.get(id) != md) return false;
        md.mDialog.show();
        return true;
    }
    public final void dismissDialog(int id) {
        ManagedDialog md = mManagedDialogs == null ? null : mManagedDialogs.get(id);
        if (md == null) throw new IllegalArgumentException("no dialog with id " + id + " was ever shown via Activity#showDialog");
        md.mDialog.dismiss();
    }
    public final void removeDialog(int id) {
        if (mManagedDialogs == null) return;
        ManagedDialog md = mManagedDialogs.get(id);
        if (md != null) {
            md.mDialog.dismiss();
            // A dismiss callback may replace the entry; do not remove its replacement.
            if (mManagedDialogs.get(id) == md) mManagedDialogs.remove(id);
        }
    }
    protected void onDestroy() {
        SparseArray<ManagedDialog> dialogs = mManagedDialogs;
        mManagedDialogs = null;
        if (dialogs != null) {
            for (int i = 0; i < dialogs.size(); i++) dialogs.valueAt(i).mDialog.dismiss();
        }
    }

    private native void nativeStartActivityForResult(Intent intent, int requestCode, Bundle options);
    private native void nativeFinish(int resultCode, Intent data);
}
