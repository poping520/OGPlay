// Catalog batch: Paint/Typeface/Rect/Bitmap/BitmapFactory/Canvas.

#include "shared.h"

namespace ogplay::runtime::android_intrinsics {

void AppendGraphicsClasses(std::vector<Decl>& catalog) {
    {
        // Canvas-drawn installer views create Paint state objects in their
        // constructors. Paint holds pure drawing state; state setters are
        // no-ops until a canvas surface actually consumes them.
        Decl paint;
        paint.descriptor = "Landroid/graphics/Paint;";
        paint.superclass = "Ljava/lang/Object;";
        paint.methods = {
            {"<init>", "()V", false, false, "android.graphics.noop"},
            {"<init>", "(I)V", false, false, "android.graphics.noop"},
            {"setColor", "(I)V", false, false, "android.graphics.noop"},
            {"setAntiAlias", "(Z)V", false, false, "android.graphics.noop"},
            {"setTextSize", "(F)V", false, false, "android.graphics.noop"},
            {"setTypeface",
             "(Landroid/graphics/Typeface;)Landroid/graphics/Typeface;",
             false, false, "android.paint.set_typeface"},
        };
        catalog.push_back(std::move(paint));
        Decl typeface;
        typeface.descriptor = "Landroid/graphics/Typeface;";
        typeface.superclass = "Ljava/lang/Object;";
        typeface.methods = {
            {"defaultFromStyle", "(I)Landroid/graphics/Typeface;", true,
             false, "android.typeface.default_from_style"},
        };
        typeface.fields = {
            {"SERIF", "Landroid/graphics/Typeface;", true, false, 0, ""},
        };
        typeface.clinit_handler = "android.typeface.clinit";
        catalog.push_back(std::move(typeface));
        Decl matrix;
        matrix.descriptor = "Landroid/graphics/Matrix;";
        matrix.superclass = "Ljava/lang/Object;";
        matrix.methods = {
            {"<init>", "()V", false, false, "android.graphics.noop"},
        };
        catalog.push_back(std::move(matrix));
        Decl rect;
        rect.descriptor = "Landroid/graphics/Rect;";
        rect.superclass = "Ljava/lang/Object;";
        rect.fields = {
            {"left", "I", false, false, 0, ""},
            {"top", "I", false, false, 0, ""},
            {"right", "I", false, false, 0, ""},
            {"bottom", "I", false, false, 0, ""},
        };
        rect.methods = {
            {"<init>", "()V", false, false, "android.graphics.noop"},
            {"width", "()I", false, false, "android.rect.width"},
            {"height", "()I", false, false, "android.rect.height"},
        };
        catalog.push_back(std::move(rect));
        Decl paint_drawable;
        paint_drawable.descriptor =
            "Landroid/graphics/drawable/PaintDrawable;";
        paint_drawable.superclass = "Landroid/graphics/drawable/Drawable;";
        paint_drawable.methods = {
            {"<init>", "(I)V", false, false, "android.graphics.noop"},
            {"setCornerRadius", "(F)V", false, false,
             "android.graphics.noop"},
        };
        catalog.push_back(std::move(paint_drawable));
        // Bitmaps carry real host-side ARGB8888 pixel stores; decode uses
        // the vendored stb_image (BitmapFactory returns the documented null
        // on undecodable data). Config selects storage precision on device;
        // this platform always keeps full 8888 precision, which the
        // getPixels contract (packed ARGB ints) makes observationally
        // equivalent.
        Decl bitmap_config;
        bitmap_config.descriptor = "Landroid/graphics/Bitmap$Config;";
        bitmap_config.superclass = "Ljava/lang/Object;";
        bitmap_config.fields = {
            {"ARGB_4444", "Landroid/graphics/Bitmap$Config;", true, false, 0,
             ""},
            {"ARGB_8888", "Landroid/graphics/Bitmap$Config;", true, false, 0,
             ""},
        };
        bitmap_config.clinit_handler = "android.bitmap_config.clinit";
        catalog.push_back(std::move(bitmap_config));
        Decl bitmap;
        bitmap.descriptor = "Landroid/graphics/Bitmap;";
        bitmap.superclass = "Ljava/lang/Object;";
        bitmap.methods = {
            {"createBitmap",
             "([IIILandroid/graphics/Bitmap$Config;)"
             "Landroid/graphics/Bitmap;",
             true, false, "android.bitmap.create"},
            {"createBitmap",
             "([IIIIILandroid/graphics/Bitmap$Config;)"
             "Landroid/graphics/Bitmap;",
             true, false, "android.bitmap.create_offset"},
            {"getWidth", "()I", false, false, "android.bitmap.get_width"},
            {"getHeight", "()I", false, false, "android.bitmap.get_height"},
            {"getPixels", "([IIIIIII)V", false, false,
             "android.bitmap.get_pixels"},
            {"prepareToDraw", "()V", false, false, "android.graphics.noop"},
            {"recycle", "()V", false, false, "android.bitmap.recycle"},
        };
        catalog.push_back(std::move(bitmap));
        Decl bitmap_factory;
        bitmap_factory.descriptor = "Landroid/graphics/BitmapFactory;";
        bitmap_factory.superclass = "Ljava/lang/Object;";
        bitmap_factory.methods = {
            {"decodeByteArray", "([BII)Landroid/graphics/Bitmap;", true,
             false, "android.bitmap_factory.decode_byte_array"},
        };
        catalog.push_back(std::move(bitmap_factory));
        // Canvas instances only come from the framework; the dex_activity
        // lifecycle never dispatches View.onDraw, so draw calls are pure
        // presentation with no consumer. State queries answer with the real
        // surface geometry.
        Decl region_op;
        region_op.descriptor = "Landroid/graphics/Region$Op;";
        region_op.superclass = "Ljava/lang/Object;";
        region_op.fields = {
            {"REPLACE", "Landroid/graphics/Region$Op;", true, false, 0, ""},
        };
        region_op.clinit_handler = "android.region_op.clinit";
        catalog.push_back(std::move(region_op));
        Decl canvas;
        canvas.descriptor = "Landroid/graphics/Canvas;";
        canvas.superclass = "Ljava/lang/Object;";
        canvas.methods = {
            {"save", "(I)I", false, false, "android.canvas.save"},
            {"restore", "()V", false, false, "android.graphics.noop"},
            {"clipRect", "(FFFFLandroid/graphics/Region$Op;)Z", false, false,
             "android.canvas.clip_rect"},
            {"getClipBounds", "()Landroid/graphics/Rect;", false, false,
             "android.canvas.get_clip_bounds"},
            {"drawColor", "(I)V", false, false, "android.graphics.noop"},
            {"drawBitmap",
             "(Landroid/graphics/Bitmap;FFLandroid/graphics/Paint;)V", false,
             false, "android.graphics.noop"},
            {"drawBitmap", "([IIIIIIIZLandroid/graphics/Paint;)V", false,
             false, "android.graphics.noop"},
        };
        catalog.push_back(std::move(canvas));
    }
}

}  // namespace ogplay::runtime::android_intrinsics
