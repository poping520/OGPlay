package org.ogplay.probe;

import android.app.Activity;
import android.content.Context;
import android.content.Intent;
import android.graphics.Canvas;
import android.opengl.GLSurfaceView;
import android.os.Bundle;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import java.util.concurrent.TimeUnit;
import javax.microedition.khronos.egl.EGLConfig;
import javax.microedition.khronos.opengles.GL10;

import static org.ogplay.probe.Probe.obj;

public final class GlProbeActivity extends Activity {
    private ActivityFlow flow;
    private GLSurfaceView gl;
    private boolean paused;

    static Probe.Outcome measure(ProbeActivity activity, int mode) throws Exception {
        ActivityFlow flow = ActivityFlow.create();
        flow.start(activity, new Intent(activity, GlProbeActivity.class).putExtra("mode", mode));
        try {
            boolean drew = flow.drawn.await(3400, TimeUnit.MILLISECONDS);
            // A launch failure also releases the draw latch.
            if (flow.error != null) return Probe.error(flow.error);
            if (!drew || flow.frame == null) {
                return Probe.check(false, obj("draw_callback", drew, "events", flow.trace()),
                        obj("draw_callback", true), "The test Activity has not reported a rendered frame");
            }
            flow.endDrawing(activity);
            if (!flow.awaitReturn(1500)) return Probe.check(false,
                    obj("frame", flow.frame.observed, "activity_result", false, "events", flow.trace()),
                    obj("activity_result", true), "The render Activity did not return control after rendering");
            if (flow.error != null) return Probe.error(flow.error);
            return flow.frame;
        } finally { flow.finish(activity); }
    }

    @Override protected void onCreate(Bundle state) {
        super.onCreate(state);
        flow = ActivityFlow.attach(this);
        if (flow == null) {
            finish();
            return;
        }
        flow.event("create");
        try {
            final int mode = getIntent().getIntExtra("mode", 0);
            if (mode == 0) {
                setContentView(new CanvasSurface(this, flow));
            } else {
                gl = new GLSurfaceView(this);
                if (mode == 2) gl.setEGLContextClientVersion(2);
                gl.setEGLConfigChooser(8, 8, 8, 8, 0, 0);
                gl.setRenderer(new GLSurfaceView.Renderer() {
                    private int width, height;
                    private boolean reported;

                    @Override public void onSurfaceCreated(GL10 gl, EGLConfig config) {
                        flow.event("surface_created");
                    }

                    @Override public void onSurfaceChanged(GL10 gl, int w, int h) {
                        width = w;
                        height = h;
                        flow.event("surface_changed");
                    }

                    @Override public void onDrawFrame(GL10 gl) {
                        if (reported || width <= 0 || height <= 0 || flow.cancelled) return;
                        reported = true;
                        try {
                            flow.frame = GlPattern.draw(gl, mode, width, height);
                        } catch (Throwable failure) {
                            flow.failed(failure);
                        } finally {
                            flow.drawn.countDown();
                        }
                    }
                });
                gl.setRenderMode(GLSurfaceView.RENDERMODE_WHEN_DIRTY);
                setContentView(gl);
            }
        } catch (Throwable failure) {
            flow.failed(failure);
            finish();
        }
    }

    @Override protected void onResume() {
        super.onResume();
        if (gl != null && paused) {
            try { gl.onResume(); } catch (Throwable failure) { flow.failed(failure); }
            paused = false;
        }
    }

    @Override protected void onPause() {
        if (gl != null && !paused) {
            try { gl.onPause(); } catch (Throwable failure) { flow.failed(failure); }
            paused = true;
        }
        super.onPause();
    }

    private static final class CanvasSurface extends SurfaceView implements SurfaceHolder.Callback {
        private final ActivityFlow flow;
        private boolean posted;
        private boolean signalled;

        CanvasSurface(Context context, ActivityFlow flow) {
            super(context);
            this.flow = flow;
            setWillNotDraw(false);
            getHolder().addCallback(this);
        }

        @Override public void surfaceCreated(SurfaceHolder holder) {
            flow.event("surface_created");
        }

        @Override public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
            if (posted || flow.cancelled) return;
            try {
                Canvas frame = holder.lockCanvas();
                if (frame == null) {
                    flow.frame = Probe.check(false, obj("canvas", null), obj("canvas_available", true),
                            "SurfaceHolder did not supply a writable Canvas");
                    flow.drawn.countDown();
                    return;
                }
                try { frame.drawColor(0xff278966); } finally { holder.unlockCanvasAndPost(frame); }
                flow.event("canvas_posted");
                posted = true;
                invalidate();
            } catch (Throwable failure) {
                flow.failed(failure);
            }
        }

        @Override public void surfaceDestroyed(SurfaceHolder holder) {
            flow.event("surface_destroyed");
        }

        @Override protected void onDraw(Canvas canvas) {
            super.onDraw(canvas);
            if (posted && !signalled && !flow.cancelled) {
                signalled = true;
                try {
                    flow.event("draw_after_post");
                    flow.frame = Probe.check(true, obj("posted", true, "draw_after_post", true, "events", flow.trace()),
                            obj("posted", true, "draw_after_post", true),
                            "SurfaceView posted a Canvas and received a subsequent draw");
                } catch (Throwable failure) {
                    flow.failed(failure);
                } finally {
                    flow.drawn.countDown();
                }
            }
        }
    }
}
