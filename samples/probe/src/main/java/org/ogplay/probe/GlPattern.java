package org.ogplay.probe;

import android.opengl.GLES20;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.FloatBuffer;
import javax.microedition.khronos.egl.EGL10;
import javax.microedition.khronos.egl.EGLConfig;
import javax.microedition.khronos.egl.EGLContext;
import javax.microedition.khronos.egl.EGLDisplay;
import javax.microedition.khronos.opengles.GL10;
import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import static org.ogplay.probe.Probe.obj;

final class GlPattern {
    private static final int[] COLORS = {0xffd02030, 0xff29b04a, 0xff204be0, 0xffa33cad};
    private static final int BACKGROUND = 0xff182432;
    private static final int TEXTURE = 0xfffaca16;

    static Probe.Outcome draw(GL10 gl, int es, int width, int height) throws JSONException {
        JSONObject configs = configs();
        JSONArray shaderLog = new JSONArray();
        if (es == 1) gl.glViewport(0, 0, width, height);
        else GLES20.glViewport(0, 0, width, height);
        clear(gl, es, BACKGROUND);
        if (es == 1) gl.glEnable(GL10.GL_SCISSOR_TEST);
        else GLES20.glEnable(GLES20.GL_SCISSOR_TEST);
        int insetX = Math.max(2, width / 16);
        int insetY = Math.max(2, height / 16);
        for (int i = 0; i < COLORS.length; i++) {
            int x = (i & 1) * (width / 2) + insetX;
            int y = (i >> 1) * (height / 2) + insetY;
            if (es == 1) gl.glScissor(x, y, width / 2 - 2 * insetX, height / 2 - 2 * insetY);
            else GLES20.glScissor(x, y, width / 2 - 2 * insetX, height / 2 - 2 * insetY);
            clear(gl, es, COLORS[i]);
        }
        if (es == 1) gl.glDisable(GL10.GL_SCISSOR_TEST);
        else GLES20.glDisable(GLES20.GL_SCISSOR_TEST);
        boolean textured;
        if (es == 1) {
            es1Texture(gl);
            textured = true;
        } else {
            textured = es2Texture(shaderLog);
        }

        int[] expected = {BACKGROUND, COLORS[0], COLORS[1], COLORS[2], COLORS[3], TEXTURE};
        int[][] positions = {{1, 1}, {width / 4, height / 4}, {width * 3 / 4, height / 4},
                {width / 4, height * 3 / 4}, {width * 3 / 4, height * 3 / 4}, {width / 2, height / 2}};
        JSONArray observed = new JSONArray();
        JSONArray wanted = new JSONArray();
        ByteBuffer pixel = ByteBuffer.allocateDirect(4);
        boolean good = textured;
        for (int i = 0; i < positions.length; i++) {
            pixel.position(0);
            int x = positions[i][0], y = positions[i][1];
            if (es == 1) gl.glReadPixels(x, y, 1, 1, GL10.GL_RGBA, GL10.GL_UNSIGNED_BYTE, pixel);
            else GLES20.glReadPixels(x, y, 1, 1, GLES20.GL_RGBA, GLES20.GL_UNSIGNED_BYTE, pixel);
            int color = (pixel.get(3) & 255) << 24 | (pixel.get(0) & 255) << 16
                    | (pixel.get(1) & 255) << 8 | pixel.get(2) & 255;
            good &= GraphicsProbes.near(color, expected[i], 5);
            observed.put(obj("x", x, "y", y, "argb", color));
            wanted.put(expected[i]);
        }
        int error = es == 1 ? gl.glGetError() : GLES20.glGetError();
        return Probe.check(good && error == 0, obj("samples", observed, "gl_error", error, "egl", configs,
                        "shader_log", shaderLog, "texture_drawn", textured),
                obj("sample_colors_argb", wanted, "channel_tolerance", 5, "gl_error", 0, "texture_drawn", true),
                "Read back the background, four scissored colors and the textured center quad");
    }

    private static void clear(GL10 gl, int es, int argb) {
        float r = (argb >>> 16 & 255) / 255f;
        float g = (argb >>> 8 & 255) / 255f;
        float b = (argb & 255) / 255f;
        if (es == 1) {
            gl.glClearColor(r, g, b, 1);
            gl.glClear(GL10.GL_COLOR_BUFFER_BIT);
        } else {
            GLES20.glClearColor(r, g, b, 1);
            GLES20.glClear(GLES20.GL_COLOR_BUFFER_BIT);
        }
    }

    private static void es1Texture(GL10 gl) {
        int[] texture = new int[1];
        gl.glGenTextures(1, texture, 0);
        try {
            gl.glEnable(GL10.GL_TEXTURE_2D);
            gl.glBindTexture(GL10.GL_TEXTURE_2D, texture[0]);
            gl.glTexParameterf(GL10.GL_TEXTURE_2D, GL10.GL_TEXTURE_MIN_FILTER, GL10.GL_NEAREST);
            gl.glTexParameterf(GL10.GL_TEXTURE_2D, GL10.GL_TEXTURE_MAG_FILTER, GL10.GL_NEAREST);
            gl.glTexImage2D(GL10.GL_TEXTURE_2D, 0, GL10.GL_RGBA, 1, 1, 0, GL10.GL_RGBA, GL10.GL_UNSIGNED_BYTE, texel());
            gl.glEnableClientState(GL10.GL_VERTEX_ARRAY);
            gl.glEnableClientState(GL10.GL_TEXTURE_COORD_ARRAY);
            FloatBuffer vertices = vertices();
            FloatBuffer coordinates = coordinates();
            gl.glVertexPointer(2, GL10.GL_FLOAT, 0, vertices);
            gl.glTexCoordPointer(2, GL10.GL_FLOAT, 0, coordinates);
            gl.glDrawArrays(GL10.GL_TRIANGLE_STRIP, 0, 4);
            gl.glDisableClientState(GL10.GL_TEXTURE_COORD_ARRAY);
            gl.glDisableClientState(GL10.GL_VERTEX_ARRAY);
            gl.glDisable(GL10.GL_TEXTURE_2D);
        } finally { gl.glDeleteTextures(1, texture, 0); }
    }

    private static boolean es2Texture(JSONArray logs) throws JSONException {
        int program = GLES20.glCreateProgram();
        int vertex = 0;
        int fragment = 0;
        int[] texture = new int[1];
        try {
            vertex = shader(GLES20.GL_VERTEX_SHADER,
                    "attribute vec2 p; attribute vec2 uv; varying vec2 v; void main(){v=uv; gl_Position=vec4(p,0.,1.);}", logs);
            fragment = shader(GLES20.GL_FRAGMENT_SHADER,
                    "precision mediump float; varying vec2 v; uniform sampler2D t; void main(){gl_FragColor=texture2D(t,v);}", logs);
            if (vertex == 0 || fragment == 0 || program == 0) return false;
            GLES20.glAttachShader(program, vertex);
            GLES20.glAttachShader(program, fragment);
            GLES20.glLinkProgram(program);
            int[] status = new int[1];
            GLES20.glGetProgramiv(program, GLES20.GL_LINK_STATUS, status, 0);
            logs.put(obj("phase", "link", "ok", status[0] != 0, "log", GLES20.glGetProgramInfoLog(program)));
            if (status[0] == 0) return false;
            GLES20.glUseProgram(program);
            GLES20.glGenTextures(1, texture, 0);
            GLES20.glActiveTexture(GLES20.GL_TEXTURE0);
            GLES20.glBindTexture(GLES20.GL_TEXTURE_2D, texture[0]);
            GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_MIN_FILTER, GLES20.GL_NEAREST);
            GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_MAG_FILTER, GLES20.GL_NEAREST);
            GLES20.glTexImage2D(GLES20.GL_TEXTURE_2D, 0, GLES20.GL_RGBA, 1, 1, 0,
                    GLES20.GL_RGBA, GLES20.GL_UNSIGNED_BYTE, texel());
            int p = GLES20.glGetAttribLocation(program, "p");
            int uv = GLES20.glGetAttribLocation(program, "uv");
            GLES20.glUniform1i(GLES20.glGetUniformLocation(program, "t"), 0);
            FloatBuffer vertices = vertices();
            FloatBuffer coordinates = coordinates();
            GLES20.glVertexAttribPointer(p, 2, GLES20.GL_FLOAT, false, 0, vertices);
            GLES20.glVertexAttribPointer(uv, 2, GLES20.GL_FLOAT, false, 0, coordinates);
            GLES20.glEnableVertexAttribArray(p);
            GLES20.glEnableVertexAttribArray(uv);
            GLES20.glDrawArrays(GLES20.GL_TRIANGLE_STRIP, 0, 4);
            GLES20.glDisableVertexAttribArray(uv);
            GLES20.glDisableVertexAttribArray(p);
            return true;
        } finally {
            if (vertex != 0) GLES20.glDeleteShader(vertex);
            if (fragment != 0) GLES20.glDeleteShader(fragment);
            if (program != 0) GLES20.glDeleteProgram(program);
            if (texture[0] != 0) GLES20.glDeleteTextures(1, texture, 0);
        }
    }

    private static int shader(int type, String source, JSONArray logs) throws JSONException {
        int shader = GLES20.glCreateShader(type);
        if (shader == 0) return 0;
        GLES20.glShaderSource(shader, source);
        GLES20.glCompileShader(shader);
        int[] status = new int[1];
        GLES20.glGetShaderiv(shader, GLES20.GL_COMPILE_STATUS, status, 0);
        logs.put(obj("phase", type == GLES20.GL_VERTEX_SHADER ? "vertex" : "fragment",
                "ok", status[0] != 0, "log", GLES20.glGetShaderInfoLog(shader)));
        if (status[0] != 0) return shader;
        GLES20.glDeleteShader(shader);
        return 0;
    }

    private static JSONObject configs() throws JSONException {
        EGL10 egl = (EGL10) EGLContext.getEGL();
        EGLDisplay display = egl.eglGetCurrentDisplay();
        int[] count = new int[1];
        if (!egl.eglGetConfigs(display, null, 0, count)) {
            return obj("error", egl.eglGetError());
        }
        int available = count[0];
        EGLConfig[] configs = new EGLConfig[Math.min(available, 64)];
        if (!egl.eglGetConfigs(display, configs, configs.length, count)) {
            return obj("total", available, "error", egl.eglGetError());
        }
        JSONArray ids = new JSONArray();
        int[] value = new int[1];
        for (int i = 0; i < Math.min(count[0], configs.length); i++) {
            if (egl.eglGetConfigAttrib(display, configs[i], EGL10.EGL_CONFIG_ID, value)) ids.put(value[0]);
        }
        return obj("total", available, "config_ids", ids);
    }

    private static FloatBuffer vertices() {
        return floats(-0.20f, -0.20f, 0.20f, -0.20f, -0.20f, 0.20f, 0.20f, 0.20f);
    }

    private static FloatBuffer coordinates() {
        return floats(0, 0, 1, 0, 0, 1, 1, 1);
    }

    private static FloatBuffer floats(float... values) {
        FloatBuffer buffer = ByteBuffer.allocateDirect(values.length * 4).order(ByteOrder.nativeOrder()).asFloatBuffer();
        buffer.put(values).position(0);
        return buffer;
    }

    private static ByteBuffer texel() {
        ByteBuffer buffer = ByteBuffer.allocateDirect(4);
        buffer.put((byte) (TEXTURE >> 16)).put((byte) (TEXTURE >> 8)).put((byte) TEXTURE).put((byte) 255).position(0);
        return buffer;
    }
}
