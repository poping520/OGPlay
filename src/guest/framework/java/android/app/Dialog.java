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

import android.content.Context;
import android.content.DialogInterface;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.view.KeyEvent;
import android.view.View;
import android.view.Window;

/** Bounded API19 dialog client. Ordinary state and callbacks remain guest-owned. */
public class Dialog implements DialogInterface {
    protected final Context mContext;
    protected CharSequence mTitle;
    protected CharSequence mMessage;
    protected final CharSequence[] mButtonLabels = new CharSequence[3];
    protected final DialogInterface.OnClickListener[] mButtonListeners = new DialogInterface.OnClickListener[3];
    private Activity mOwnerActivity;
    private final Handler mHandler;
    private boolean mCreated;
    private boolean mShowing;
    private boolean mStarting;
    private boolean mCancelable = true;
    private boolean mCanceled;
    private boolean mBackTracking;
    private long mGeneration;
    private long mShowGeneration;
    private int mAcceptButton;
    private long mAcceptGeneration;
    private DialogInterface.OnKeyListener mKeyListener;
    private DialogInterface.OnCancelListener mCancelListener;
    private DialogInterface.OnDismissListener mDismissListener;
    private DialogInterface.OnShowListener mShowListener;

    public Dialog(Context context) { this(context, 0); }
    public Dialog(Context context, int theme) {
        if (context == null) throw new NullPointerException("context == null");
        if (theme != 0) nativeUnsupported("Dialog theme");
        mContext = context;
        mHandler = new Handler(Looper.myLooper());
    }
    protected Dialog(Context context, boolean cancelable, DialogInterface.OnCancelListener listener) {
        this(context);
        mCancelable = cancelable;
        mCancelListener = listener;
    }
    public final Context getContext() { return mContext; }
    public final void setOwnerActivity(Activity activity) { mOwnerActivity = activity; }
    public final Activity getOwnerActivity() { return mOwnerActivity; }
    public boolean isShowing() { return mShowing; }
    protected void onCreate(Bundle state) { }
    protected void onStart() { }
    protected void onStop() { }
    final void dispatchOnCreate(Bundle state) {
        if (!mCreated) {
            mCreated = true;
            onCreate(state);
        }
    }
    private void present() {
        nativeShow(mContext, mOwnerActivity, mTitle, mMessage, mButtonLabels, mGeneration);
    }
    public void show() {
        if (mShowing) { nativeVisible(true); return; }
        if (mStarting) return;
        mStarting = true;
        long generation = ++mGeneration;
        ++mShowGeneration;
        try {
            dispatchOnCreate(null);
            if (mGeneration != generation) return;
            onStart();
            if (mGeneration != generation) return;
            present();
            if (mGeneration != generation) return;
            mShowing = true;
            mCanceled = false;
            mBackTracking = false;
            final DialogInterface.OnShowListener listener = mShowListener;
            if (listener != null) mHandler.post(new Runnable() {
                public void run() { listener.onShow(Dialog.this); }
            });
        } finally { mStarting = false; }
    }
    public void hide() { mBackTracking = false; mAcceptButton = 0; if (mShowing) nativeVisible(false); }
    public void dismiss() {
        ++mGeneration;
        ++mShowGeneration;
        mAcceptButton = 0;
        mBackTracking = false;
        if (!mShowing) return;
        nativeDismiss();
        mShowing = false;
        onStop();
        final DialogInterface.OnDismissListener listener = mDismissListener;
        if (listener != null) mHandler.post(new Runnable() {
            public void run() { listener.onDismiss(Dialog.this); }
        });
    }
    public void cancel() {
        if (!mCanceled && mCancelListener != null) {
            mCanceled = true;
            final DialogInterface.OnCancelListener listener = mCancelListener;
            mHandler.post(new Runnable() { public void run() { listener.onCancel(Dialog.this); } });
        }
        dismiss();
    }
    public void setCancelable(boolean cancelable) { mCancelable = cancelable; }
    public void setCanceledOnTouchOutside(boolean cancel) { if (cancel) nativeUnsupported("outside-touch cancellation"); }
    public void setOnCancelListener(DialogInterface.OnCancelListener listener) { mCancelListener = listener; }
    public void setOnDismissListener(DialogInterface.OnDismissListener listener) { mDismissListener = listener; }
    public void setOnShowListener(DialogInterface.OnShowListener listener) { mShowListener = listener; }
    public void setOnKeyListener(DialogInterface.OnKeyListener listener) { mKeyListener = listener; }
    public void setTitle(CharSequence title) { mTitle = title; changed(); }
    public void setTitle(int title) { setTitle(mContext.getResources().getText(title)); }
    public Window getWindow() { nativeUnsupported("Dialog Window facade"); return null; }
    public void setContentView(View view) { nativeUnsupported("custom Dialog content"); }
    public void setContentView(int id) { nativeUnsupported("custom Dialog layout"); }
    public boolean dispatchKeyEvent(KeyEvent event) {
        if (mKeyListener != null && mKeyListener.onKey(this, event.getKeyCode(), event)) return true;
        if (event.getAction() == KeyEvent.ACTION_DOWN) return onKeyDown(event.getKeyCode(), event);
        if (event.getAction() == KeyEvent.ACTION_UP) return onKeyUp(event.getKeyCode(), event);
        return false;
    }
    public boolean onKeyDown(int code, KeyEvent event) {
        if (code == KeyEvent.KEYCODE_BACK || code == KeyEvent.KEYCODE_ESCAPE) {
            if (event.getRepeatCount() == 0) mBackTracking = true;
            return true;
        }
        if (code == KeyEvent.KEYCODE_TAB) { nativeFocus(event.isShiftPressed() ? -1 : 1); return true; }
        if (code == KeyEvent.KEYCODE_ENTER || code == KeyEvent.KEYCODE_DPAD_CENTER) {
            if (event.getRepeatCount() == 0) { mAcceptButton = nativeFocus(0); mAcceptGeneration = mShowGeneration; }
            return true;
        }
        return false;
    }
    public boolean onKeyUp(int code, KeyEvent event) {
        if (code == KeyEvent.KEYCODE_BACK || code == KeyEvent.KEYCODE_ESCAPE) {
            boolean tracked = mBackTracking;
            mBackTracking = false;
            if (tracked && !event.isCanceled() && mCancelable) cancel();
            return true;
        }
        if (code == KeyEvent.KEYCODE_ENTER || code == KeyEvent.KEYCODE_DPAD_CENTER) {
            int which = mAcceptButton;
            mAcceptButton = 0;
            if (!event.isCanceled() && mAcceptGeneration == mShowGeneration && which != 0 && which == nativeFocus(0)) performButtonClick(which);
            return true;
        }
        return false;
    }
    // Native input enters this protocol; callbacks remain queued on the owning Looper.
    public final void performButtonClick(final int which) {
        int index = -which - 1;
        if (index < 0 || index >= 3) throw new IllegalArgumentException("unknown Dialog button");
        if (!mShowing || mButtonLabels[index] == null) return;
        final long generation = mShowGeneration;
        final DialogInterface.OnClickListener listener = mButtonListeners[index];
        if (listener != null) mHandler.post(new Runnable() {
            public void run() { listener.onClick(Dialog.this, which); }
        });
        mHandler.post(new Runnable() {
            public void run() { if (mShowGeneration == generation && mShowing) dismiss(); }
        });
    }
    protected final void changed() { if (mShowing) { ++mGeneration; present(); } }
    static native void nativeUnsupported(String operation);
    private native void nativeShow(Context context, Activity owner, CharSequence title,
                                   CharSequence message, CharSequence[] buttons, long generation);
    private native void nativeDismiss();
    private native void nativeVisible(boolean visible);
    private native int nativeFocus(int direction);
}
