/*
 * Copyright (C) 2013 The Android Open Source Project
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0
 * Unless required by applicable law or agreed to in writing, software distributed
 * under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
 * CONDITIONS OF ANY KIND, either express or implied. See the License for the
 * specific language governing permissions and limitations under the License.
 */
package android.view;

import android.graphics.Canvas;
import android.graphics.Rect;
import android.graphics.SurfaceTexture;

/** Bounded API19 client: managed holders and process-local stream producers.
 * Public ownership/locking follows AOSP Surface.java. Native adapters receive
 * the owner explicitly so the process can retire endpoints on Java GC.
 * Managed holders also provide a software Canvas.
 */
public class Surface {
    public static final int ROTATION_0 = 0;
    public static final int ROTATION_90 = 1;
    public static final int ROTATION_180 = 2;
    public static final int ROTATION_270 = 3;
    private final Object mLock = new Object();
    private int mNativeObject;
    private int mGenerationId;

    public Surface() {}
    public Surface(SurfaceTexture surfaceTexture) {
        if (surfaceTexture == null) throw new IllegalArgumentException("surfaceTexture must not be null");
        synchronized (mLock) {
            setNativeObjectLocked(nativeCreateFromSurfaceTexture(this, surfaceTexture));
        }
    }
    public void release() {
        synchronized (mLock) {
            if (mNativeObject != 0) {
                nativeRelease(this, mNativeObject);
                setNativeObjectLocked(0);
            }
        }
    }
    public void destroy() { release(); }
    public boolean isValid() {
        synchronized (mLock) {
            return mNativeObject != 0 && nativeIsValid(this, mNativeObject);
        }
    }
    public Canvas lockCanvas(Rect dirty) {
        synchronized (mLock) {
            checkNotReleasedLocked();
            return nativeLockCanvas(this, mNativeObject, dirty);
        }
    }
    public void unlockCanvasAndPost(Canvas canvas) {
        final int token;
        synchronized (mLock) {
            checkNotReleasedLocked();
            token = mNativeObject;
        }
        // The native post revalidates the endpoint and may wait for the next frame slot.
        nativeUnlockCanvasAndPost(this, token, canvas);
    }
    private void checkNotReleasedLocked() {
        if (mNativeObject == 0) throw new IllegalStateException("Surface has been released");
    }
    public int getGenerationId() { synchronized (mLock) { return mGenerationId; } }
    private void setNativeObjectLocked(int value) {
        if (mNativeObject != value) { mNativeObject = value; ++mGenerationId; }
    }
    @Override protected void finalize() throws Throwable {
        try { release(); } finally { super.finalize(); }
    }
    private static native int nativeCreateFromSurfaceTexture(Surface owner, SurfaceTexture texture);
    private static native void nativeRelease(Surface owner, int token);
    private static native Canvas nativeLockCanvas(Surface owner, int token, Rect dirty);
    private static native void nativeUnlockCanvasAndPost(Surface owner, int token, Canvas canvas);
    private static native boolean nativeIsValid(Surface owner, int token);
}
