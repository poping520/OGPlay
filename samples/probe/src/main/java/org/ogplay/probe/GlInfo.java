package org.ogplay.probe;

import android.opengl.GLES20;
import javax.microedition.khronos.egl.EGL10;
import javax.microedition.khronos.egl.EGLConfig;
import javax.microedition.khronos.egl.EGLContext;
import javax.microedition.khronos.egl.EGLDisplay;
import javax.microedition.khronos.egl.EGLSurface;
import org.ogplay.probe.Probe.Checks;
import org.json.JSONException;
import org.json.JSONObject;

import static org.ogplay.probe.Probe.obj;

final class GlInfo {
    static Probe.Outcome inspect(int declared) throws JSONException {
        int major = declared >> 16;
        if (major < 1) {
            return new Checks().require("gles_major_at_least_one", false)
                    .outcome(obj("declared_gles", declared), obj("declared_gles_major_min", 1),
                            "ActivityManager must declare an OpenGL ES version");
        }
        EGL10 egl = (EGL10) EGLContext.getEGL();
        EGLDisplay display = egl.eglGetDisplay(EGL10.EGL_DEFAULT_DISPLAY);
        EGLContext context = EGL10.EGL_NO_CONTEXT;
        EGLSurface surface = EGL10.EGL_NO_SURFACE;
        if (display == EGL10.EGL_NO_DISPLAY || !egl.eglInitialize(display, new int[2])) return failed(egl, "eglInitialize");
        try {
            int renderableBit = major >= 3 ? 0x40 : major == 2 ? 4 : 1;
            int[] attrs = {EGL10.EGL_SURFACE_TYPE, EGL10.EGL_PBUFFER_BIT, 0x3040, renderableBit,
                    EGL10.EGL_RED_SIZE, 8, EGL10.EGL_GREEN_SIZE, 8, EGL10.EGL_BLUE_SIZE, 8, EGL10.EGL_NONE};
            EGLConfig[] configs = new EGLConfig[1];
            int[] count = new int[1];
            if (!egl.eglChooseConfig(display, attrs, configs, configs.length, count) || count[0] == 0) {
                return failed(egl, "eglChooseConfig");
            }
            context = egl.eglCreateContext(display, configs[0], EGL10.EGL_NO_CONTEXT,
                    new int[] {0x3098, major, EGL10.EGL_NONE});
            if (context == EGL10.EGL_NO_CONTEXT) return failed(egl, "eglCreateContext");
            surface = egl.eglCreatePbufferSurface(display, configs[0],
                    new int[] {EGL10.EGL_WIDTH, 16, EGL10.EGL_HEIGHT, 16, EGL10.EGL_NONE});
            if (surface == EGL10.EGL_NO_SURFACE) return failed(egl, "eglCreatePbufferSurface");
            if (!egl.eglMakeCurrent(display, surface, surface, context)) return failed(egl, "eglMakeCurrent");
            String version = GLES20.glGetString(GLES20.GL_VERSION);
            String vendor = GLES20.glGetString(GLES20.GL_VENDOR);
            String renderer = GLES20.glGetString(GLES20.GL_RENDERER);
            String extensions = GLES20.glGetString(GLES20.GL_EXTENSIONS);
            int[] maximum = new int[1];
            GLES20.glGetIntegerv(GLES20.GL_MAX_TEXTURE_SIZE, maximum, 0);
            int error = GLES20.glGetError();
            int actualVersion = parse(version);
            return new Checks()
                    .require("gles_version_matches_declared", actualVersion == declared)
                    .require("vendor_reported", vendor != null)
                    .require("renderer_reported", renderer != null)
                    .require("extensions_reported", extensions != null)
                    .require("texture_size_positive", maximum[0] > 0)
                    .require("no_gl_error", error == GLES20.GL_NO_ERROR)
                    .outcome(obj("version", version, "parsed_version", actualVersion, "vendor", vendor, "renderer", renderer,
                                    "extensions", extensions, "max_texture_size", maximum[0], "gl_error", error),
                            obj("declared_version", declared, "texture_size_positive", true, "gl_error", 0),
                            "OpenGL ES context identity and version agree with ActivityManager");
        } finally {
            egl.eglMakeCurrent(display, EGL10.EGL_NO_SURFACE, EGL10.EGL_NO_SURFACE, EGL10.EGL_NO_CONTEXT);
            if (surface != EGL10.EGL_NO_SURFACE) egl.eglDestroySurface(display, surface);
            if (context != EGL10.EGL_NO_CONTEXT) egl.eglDestroyContext(display, context);
            egl.eglTerminate(display);
        }
    }

    private static int parse(String version) {
        if (version == null) return 0;
        java.util.regex.Matcher matcher = java.util.regex.Pattern.compile("OpenGL ES(?:-[A-Z]+)? (\\d+)\\.(\\d+)").matcher(version);
        if (!matcher.find()) return 0;
        return (Integer.parseInt(matcher.group(1)) << 16) | Integer.parseInt(matcher.group(2));
    }

    private static Probe.Outcome failed(EGL10 egl, String call) throws JSONException {
        return new Checks().require(call, false)
                .outcome(obj("call", call, "egl_error", egl.eglGetError()), obj("egl_context_current", true),
                        "Could not make an API 19 EGL pbuffer context current");
    }
}
