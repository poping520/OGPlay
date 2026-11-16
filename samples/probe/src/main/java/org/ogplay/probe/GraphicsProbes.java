package org.ogplay.probe;

import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.Typeface;
import java.io.ByteArrayOutputStream;
import java.io.InputStream;
import java.util.Map;

import static org.ogplay.probe.PhaseOne.add;
import static org.ogplay.probe.Probe.obj;

final class GraphicsProbes {
    static void register(Map<String, Probe> probes) {
        add(probes, new Probe("graphics.canvas.software_pixels", "in_scope", false,
                "Landroid/graphics/Bitmap;.createBitmap(IILandroid/graphics/Bitmap$Config;)Landroid/graphics/Bitmap;",
                "Landroid/graphics/Canvas;.drawRect(FFFFLandroid/graphics/Paint;)V",
                "Landroid/graphics/Canvas;.drawCircle(FFFLandroid/graphics/Paint;)V",
                "Landroid/graphics/Canvas;.drawText(Ljava/lang/String;FFLandroid/graphics/Paint;)V",
                "Landroid/graphics/Paint;.measureText(Ljava/lang/String;)F", "Landroid/graphics/Bitmap;.getPixel(II)I") {
            @Override Outcome run(ProbeSession session) throws Exception {
                Bitmap bitmap = Bitmap.createBitmap(64, 50, Bitmap.Config.ARGB_8888);
                try {
                    Canvas canvas = new Canvas(bitmap);
                    Paint paint = new Paint();
                    canvas.drawColor(0xff111820);
                    paint.setColor(0xff00c680);
                    canvas.drawRect(2, 2, 15, 15, paint);
                    paint.setColor(0xff4f88ff);
                    canvas.drawCircle(33, 12, 7, paint);
                    paint.setColor(0xffeeeeee);
                    paint.setTypeface(Typeface.DEFAULT);
                    paint.setTextSize(15);
                    float width = paint.measureText("OG19");
                    canvas.drawText("OG19", 2, 40, paint);
                    int foreground = 0;
                    for (int y = 24; y < 43; y++) for (int x = 0; x < 60; x++) {
                        if (bitmap.getPixel(x, y) != 0xff111820) foreground++;
                    }
                    int rect = bitmap.getPixel(6, 6);
                    int circle = bitmap.getPixel(33, 12);
                    int background = bitmap.getPixel(63, 49);
                    return check(rect == 0xff00c680 && circle == 0xff4f88ff && background == 0xff111820
                                    && width > 0 && foreground > 0,
                            obj("rect_pixel", rect, "circle_pixel", circle, "background_pixel", background,
                                    "text_width", width, "changed_text_pixels", foreground),
                            obj("rect_pixel", 0xff00c680, "circle_pixel", 0xff4f88ff, "background_pixel", 0xff111820,
                                    "text_width_positive", true, "text_changes_pixels", true),
                            "Software shapes and default-font text change the intended bitmap pixels");
                } finally { bitmap.recycle(); }
            }
        });
        add(probes, new Probe("graphics.bitmap.decode_compress", "in_scope", false,
                "Landroid/graphics/BitmapFactory;.decodeStream(Ljava/io/InputStream;)Landroid/graphics/Bitmap;",
                "Landroid/graphics/Bitmap;.compress(Landroid/graphics/Bitmap$CompressFormat;ILjava/io/OutputStream;)Z",
                "Landroid/graphics/BitmapFactory;.decodeByteArray([BII)Landroid/graphics/Bitmap;") {
            @Override Outcome run(ProbeSession session) throws Exception {
                Bitmap png = decode(session, "pixels.png");
                Bitmap jpg = decode(session, "pixels.jpg");
                Bitmap restored = null;
                try {
                    if (png == null || jpg == null) return check(false, obj("png", png != null, "jpg", jpg != null),
                            obj("png", true, "jpg", true), "Both packaged image fixtures should decode");
                    int first = png.getPixel(2, 2);
                    int other = png.getPixel(14, 14);
                    int jpeg = jpg.getPixel(4, 4);
                    ByteArrayOutputStream output = new ByteArrayOutputStream();
                    boolean compressed = png.compress(Bitmap.CompressFormat.PNG, 100, output);
                    byte[] data = output.toByteArray();
                    restored = BitmapFactory.decodeByteArray(data, 0, data.length);
                    boolean lossless = restored != null && restored.getPixel(2, 2) == first && restored.getPixel(14, 14) == other;
                    return check(png.getWidth() == 16 && png.getHeight() == 16 && jpg.getWidth() == 16 && jpg.getHeight() == 16
                                    && first == 0xff088658 && other == 0xffe15219 && near(jpeg, 0xff4060a0, 5) && compressed && lossless,
                            obj("png_width", png.getWidth(), "png_height", png.getHeight(), "png_key_pixel", first,
                                    "png_other_pixel", other, "jpeg_pixel", jpeg, "compressed", compressed, "lossless", lossless),
                            obj("width", 16, "height", 16, "png_key_pixel", 0xff088658, "png_other_pixel", 0xffe15219,
                                    "jpeg_pixel", 0xff4060a0, "jpeg_channel_tolerance", 5, "lossless", true),
                            "PNG and JPEG decode known colors and PNG compression preserves the pixels");
                } finally {
                    if (png != null) png.recycle();
                    if (jpg != null) jpg.recycle();
                    if (restored != null) restored.recycle();
                }
            }
        });
        add(probes, drawProbe("graphics.gles.es1", 1,
                "Ljavax/microedition/khronos/opengles/GL10;.glReadPixels(IIIIIILjava/nio/Buffer;)V",
                "Ljavax/microedition/khronos/opengles/GL10;.glTexImage2D(IIIIIIIILjava/nio/Buffer;)V",
                "Ljavax/microedition/khronos/opengles/GL10;.glDrawArrays(III)V",
                "Ljavax/microedition/khronos/opengles/GL10;.glGetError()I"));
        add(probes, drawProbe("graphics.gles.es2", 2,
                "Landroid/opengl/GLES20;.glReadPixels(IIIIIILjava/nio/Buffer;)V",
                "Landroid/opengl/GLES20;.glCompileShader(I)V", "Landroid/opengl/GLES20;.glLinkProgram(I)V",
                "Landroid/opengl/GLES20;.glDrawArrays(III)V", "Landroid/opengl/GLES20;.glGetError()I"));
        add(probes, drawProbe("graphics.surface.canvas", 0,
                "Landroid/view/SurfaceHolder;.lockCanvas()Landroid/graphics/Canvas;",
                "Landroid/view/SurfaceHolder;.unlockCanvasAndPost(Landroid/graphics/Canvas;)V",
                "Landroid/view/SurfaceView;.onDraw(Landroid/graphics/Canvas;)V"));
    }

    private static Probe drawProbe(String id, final int mode, String... api) {
        return new Probe(id, "graphics", "in_scope", false, 7000, api) {
            @Override Outcome run(ProbeSession session) throws Exception {
                return GlProbeActivity.measure(session.activity, mode);
            }
        };
    }

    private static Bitmap decode(ProbeSession session, String name) throws Exception {
        InputStream data = session.activity.getAssets().open("fixtures/" + name);
        try { return BitmapFactory.decodeStream(data); } finally { data.close(); }
    }

    static boolean near(int actual, int expected, int tolerance) {
        for (int shift = 0; shift < 32; shift += 8) {
            if (Math.abs((actual >>> shift & 255) - (expected >>> shift & 255)) > tolerance) return false;
        }
        return true;
    }
}
