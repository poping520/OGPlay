package org.ogplay.fixture.window;

import android.app.Activity;
import android.graphics.Color;
import android.opengl.GLES20;
import android.opengl.GLSurfaceView;
import android.os.Bundle;
import android.view.View;
import android.widget.FrameLayout;

import javax.microedition.khronos.egl.EGLConfig;
import javax.microedition.khronos.opengles.GL10;

public class GlActivity extends Activity {
    public static volatile int contexts, draws;
    public static volatile int holdDraw, slowDraws;
    private GLSurfaceView gl;

    @Override public void onCreate(Bundle saved) {
        super.onCreate(saved);
        FrameLayout content = new FrameLayout(this);
        gl = new GLSurfaceView(this);
        gl.setEGLContextClientVersion(2);
        gl.setPreserveEGLContextOnPause(true);
        gl.setRenderer(new GLSurfaceView.Renderer() {
            public void onSurfaceCreated(GL10 gl, EGLConfig config) { ++contexts; }
            public void onSurfaceChanged(GL10 gl, int width, int height) { GLES20.glViewport(0, 0, width, height); }
            public void onDrawFrame(GL10 gl) {
                if (holdDraw != 0) {
                    ++slowDraws;
                    while (holdDraw != 0) { }
                }
                GLES20.glClearColor(0.125f, 0.625f, 0.25f, 1);
                GLES20.glClear(GLES20.GL_COLOR_BUFFER_BIT);
                ++draws;
            }
        });
        if (this instanceof DirtyGlActivity) gl.setRenderMode(GLSurfaceView.RENDERMODE_WHEN_DIRTY);
        content.addView(gl, new FrameLayout.LayoutParams(-1, -1));
        View overlay = new View(this);
        overlay.setBackgroundColor(Color.RED);
        content.addView(overlay, new FrameLayout.LayoutParams(32, 32));
        setContentView(content);
    }

    @Override protected void onPause() { gl.onPause(); super.onPause(); }
    @Override protected void onResume() { super.onResume(); gl.onResume(); }
}
