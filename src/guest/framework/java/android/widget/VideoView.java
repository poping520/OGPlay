/*
 * Copyright (C) 2006 The Android Open Source Project
 * Copyright (C) 2026 OGPlay contributors
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy at http://www.apache.org/licenses/LICENSE-2.0
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
package android.widget;

import android.content.Context;
import android.content.res.AssetFileDescriptor;
import android.media.MediaPlayer;
import android.net.Uri;
import android.view.SurfaceView;
import java.util.List;

/** Bounded API19 local video protocol; no Binder, subtitle or network services. */
public class VideoView extends SurfaceView {
    private Uri mUri;
    private MediaPlayer mPlayer;
    private MediaPlayer.OnPreparedListener mPreparedListener;
    private MediaPlayer.OnCompletionListener mCompletionListener;
    private MediaPlayer.OnErrorListener mErrorListener;
    private int mGeneration;
    private int mSeekWhenPrepared;
    private boolean mPrepared;
    private boolean mTargetPlaying;

    public VideoView(Context context) { super(context); }
    public void setOnPreparedListener(MediaPlayer.OnPreparedListener listener) { mPreparedListener = listener; }
    public void setOnCompletionListener(MediaPlayer.OnCompletionListener listener) { mCompletionListener = listener; }
    public void setOnErrorListener(MediaPlayer.OnErrorListener listener) { mErrorListener = listener; }
    public void setVideoPath(String path) { setVideoURI(Uri.parse(path)); }
    public void setVideoURI(Uri uri) {
        if (uri == null) throw new NullPointerException("video uri is null");
        mUri = uri;
        mSeekWhenPrepared = 0;
        openVideo();
        requestLayout();
        invalidate();
    }
    private void releasePlayer() {
        ++mGeneration;
        mPrepared = false;
        MediaPlayer old = mPlayer;
        mPlayer = null;
        if (old != null) old.release();
    }
    private void openVideo() {
        releasePlayer();
        if (mUri == null) return;
        int generation = mGeneration;
        mPlayer = new VideoMediaPlayer(this, generation);
        try {
            String scheme = mUri.getScheme();
            if ("android.resource".equals(scheme)) {
                if (!getContext().getPackageName().equals(mUri.getAuthority()))
                    throw new IllegalArgumentException("video resource belongs to another package");
                List<String> parts = mUri.getPathSegments();
                int id;
                if (parts.size() == 1) id = Integer.parseInt(parts.get(0));
                else if (parts.size() == 2)
                    id = getContext().getResources().getIdentifier(parts.get(1), parts.get(0), mUri.getAuthority());
                else throw new IllegalArgumentException("invalid resource URI");
                if (id == 0) throw new IllegalArgumentException("video resource not found");
                AssetFileDescriptor asset = getContext().getResources().openRawResourceFd(id);
                try {
                    nativeOpenFd(asset.getFileDescriptor(), asset.getStartOffset(), asset.getLength(), generation);
                } finally { asset.close(); }
            } else if (scheme == null || "file".equals(scheme)) {
                if ("file".equals(scheme) && mUri.getAuthority() != null && mUri.getAuthority().length() != 0)
                    throw new IllegalArgumentException("file URI authority is unsupported");
                nativeOpenPath(mUri.getPath(), generation);
            } else {
                nativeUnsupported("uri_scheme");
            }
        } catch (Exception error) {
            nativeOpenError(generation, error.toString());
        }
    }
    // Called at the guest main-thread video pump, never from a decoder/audio worker.
    private void dispatchEvent(int generation, int event) {
        if (generation != mGeneration || mPlayer == null) return;
        MediaPlayer player = mPlayer;
        if (event == 1) {
            mPrepared = true;
            if (mPreparedListener != null) mPreparedListener.onPrepared(player);
            if (generation != mGeneration || player != mPlayer) return;
            if (mSeekWhenPrepared != 0) { player.seekTo(mSeekWhenPrepared); mSeekWhenPrepared = 0; }
            if (mTargetPlaying) player.start();
        } else if (event == 2) {
            mTargetPlaying = false;
            if (mCompletionListener != null) mCompletionListener.onCompletion(player);
        } else if (event == 100) {
            mPrepared = false;
            mTargetPlaying = false;
            if (mErrorListener == null || !mErrorListener.onError(player, 1, -1))
                nativeUnhandledError();
        }
    }
    public void start() { mTargetPlaying = true; if (mPrepared && mPlayer != null) mPlayer.start(); }
    public void pause() { mTargetPlaying = false; if (mPrepared && mPlayer != null) mPlayer.pause(); }
    public void stopPlayback() { releasePlayer(); mTargetPlaying = false; }
    public void suspend() { releasePlayer(); }
    public void resume() { openVideo(); }
    public void seekTo(int milliseconds) {
        if (mPrepared && mPlayer != null) mPlayer.seekTo(milliseconds);
        else mSeekWhenPrepared = milliseconds;
    }
    public int getDuration() { return mPrepared && mPlayer != null ? mPlayer.getDuration() : -1; }
    public int getCurrentPosition() { return mPrepared && mPlayer != null ? mPlayer.getCurrentPosition() : 0; }
    public boolean isPlaying() { return mPrepared && mPlayer != null && mPlayer.isPlaying(); }
    public boolean canPause() { return mPrepared; }
    public boolean canSeekForward() { return mPrepared; }
    public boolean canSeekBackward() { return mPrepared; }

    private native void nativeOpenPath(String path, int generation);
    private native void nativeOpenFd(java.io.FileDescriptor fd, long offset, long length, int generation);
    private native void nativeOpenError(int generation, String message);
    private native void nativeUnhandledError();
    private native void nativeUnsupported(String operation);
    private native void nativeStart(int generation);
    private native void nativePause(int generation);
    private native void nativeStop(int generation);
    private native void nativeRelease(int generation);
    private native void nativeSeek(int generation, int milliseconds);
    private native void nativeVolume(int generation, float left, float right);
    private native int nativeDuration(int generation);
    private native int nativePosition(int generation);
    private native int nativeWidth(int generation);
    private native int nativeHeight(int generation);
    private native boolean nativeIsPlaying(int generation);

    // A real, initialized MediaPlayer identity whose required controls operate on
    // the same video backend. The base player's empty audio handle is released normally.
    private static final class VideoMediaPlayer extends MediaPlayer {
        private final VideoView owner;
        private final int generation;
        private boolean released;
        VideoMediaPlayer(VideoView view, int value) { owner = view; generation = value; }
        private void requireLive() { if (released) throw new IllegalStateException("video player released"); }
        @Override public void start() { requireLive(); owner.nativeStart(generation); }
        @Override public void pause() { requireLive(); owner.nativePause(generation); }
        @Override public void stop() { requireLive(); owner.nativeStop(generation); }
        @Override public void seekTo(int value) { requireLive(); owner.nativeSeek(generation, value); }
        @Override public void setVolume(float left, float right) { requireLive(); owner.nativeVolume(generation, left, right); }
        @Override public int getDuration() { requireLive(); return owner.nativeDuration(generation); }
        @Override public int getCurrentPosition() {
            if (owner == null) return super.getCurrentPosition();
            requireLive(); return owner.nativePosition(generation);
        }
        @Override public int getVideoWidth() { requireLive(); return owner.nativeWidth(generation); }
        @Override public int getVideoHeight() { requireLive(); return owner.nativeHeight(generation); }
        @Override public boolean isPlaying() {
            if (owner == null) return super.isPlaying();
            requireLive(); return owner.nativeIsPlaying(generation);
        }
        @Override public void setLooping(boolean looping) { requireLive(); owner.nativeUnsupported("looping"); }
        @Override public void reset() { requireLive(); owner.nativeUnsupported("reset"); }
        @Override public void release() {
            if (released) return;
            released = true;
            if (owner.mPlayer == this) {
                owner.mPlayer = null;
                owner.mPrepared = false;
                ++owner.mGeneration;
            }
            owner.nativeRelease(generation);
            super.release();
        }
    }
}
