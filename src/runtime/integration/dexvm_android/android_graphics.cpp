// DVM-80: API-family translation unit. Physical consolidation only.

// ---- migrated from android_graphics_Bitmap_Config.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics::dvm80_android_graphics_Bitmap_Config {

Decl Declare_android_graphics_Bitmap_Config(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/graphics/Bitmap$Config;", "Ljava/lang/Object;");
    builder.StaticField("ARGB_4444", "Landroid/graphics/Bitmap$Config;");
    builder.StaticField("ARGB_8888", "Landroid/graphics/Bitmap$Config;");
    builder.ClassInitializer([](dx::IntrinsicContext& call) {
        auto& vm = call.vm;
        for (const char* name : {"ARGB_4444", "ARGB_8888"}) {
            vm.SetIntrinsicStaticRef(
                "Landroid/graphics/Bitmap$Config;", name,
                "Landroid/graphics/Bitmap$Config;",
                vm.NewIntrinsicInstance("Landroid/graphics/Bitmap$Config;"));
        }
        return dx::VmValue::Void();
    });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

namespace ogplay::runtime::android_intrinsics {
Decl Declare_android_graphics_Bitmap_Config(const Context& context) {
    return dvm80_android_graphics_Bitmap_Config::Declare_android_graphics_Bitmap_Config(context);
}
}  // namespace ogplay::runtime::android_intrinsics

// ---- migrated from android_graphics_Bitmap.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics::dvm80_android_graphics_Bitmap {

namespace {

[[nodiscard]] DexVmAndroidContext::BitmapState& BitmapOf(
    dx::IntrinsicContext& call, const Context& context) {
    const auto found = context->bitmaps.find(call.receiver.Value());
    if (found == context->bitmaps.end() || found->second.recycled) {
        throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;",
                              "bitmap is recycled or was never created"};
    }
    return found->second;
}

// Builds a Bitmap intrinsic instance from pixels copied out of a guest int
// array (offset/stride window per the createBitmap contract).
[[nodiscard]] dx::VmValue MakeBitmapFromArray(
    dx::IntrinsicContext& call, const Context& context,
    const dx::VmObjectRef array, const std::int32_t offset,
    const std::int32_t stride, const std::int32_t width,
    const std::int32_t height) {
    auto& model = call.vm.Model();
    if (!array.IsValid()) {
        throw dx::VmJavaThrow{"Ljava/lang/NullPointerException;",
                              "createBitmap colors array is null"};
    }
    if (width <= 0 || height <= 0 || stride < width) {
        throw dx::VmJavaThrow{
            "Ljava/lang/IllegalArgumentException;",
            "createBitmap dimensions are invalid: " + std::to_string(width) +
                "x" + std::to_string(height) + " stride " +
                std::to_string(stride)};
    }
    const auto length = static_cast<std::int64_t>(model.ArrayLength(array));
    const auto last = static_cast<std::int64_t>(offset) +
                      static_cast<std::int64_t>(stride) * (height - 1) +
                      width;
    if (offset < 0 || last > length) {
        throw dx::VmJavaThrow{
            "Ljava/lang/ArrayIndexOutOfBoundsException;",
            "createBitmap window exceeds the colors array"};
    }
    DexVmAndroidContext::BitmapState state;
    state.width = width;
    state.height = height;
    state.argb.resize(static_cast<std::size_t>(width) *
                      static_cast<std::size_t>(height));
    for (std::int32_t row = 0; row < height; ++row) {
        for (std::int32_t column = 0; column < width; ++column) {
            state.argb[static_cast<std::size_t>(row) *
                           static_cast<std::size_t>(width) +
                       static_cast<std::size_t>(column)] =
                static_cast<std::uint32_t>(model.GetPrimitiveElement(
                    array, offset + row * stride + column));
        }
    }
    const auto instance =
        call.vm.NewIntrinsicInstance("Landroid/graphics/Bitmap;");
    context->bitmaps[instance.Value()] = std::move(state);
    return dx::VmValue::Ref(instance);
}

}  // namespace

Decl Declare_android_graphics_Bitmap(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/graphics/Bitmap;", "Ljava/lang/Object;");
    builder.StaticMethod("createBitmap",
        "([IIILandroid/graphics/Bitmap$Config;)Landroid/graphics/Bitmap;",
        [context](dx::IntrinsicContext& call) {
            const auto array = call.arguments[0].ref;
            const auto width = call.arguments[1].AsInt();
            const auto height = call.arguments[2].AsInt();
            return MakeBitmapFromArray(call, context, array, 0, width, width,
                                       height);
        });
    builder.StaticMethod("createBitmap",
        "([IIIIILandroid/graphics/Bitmap$Config;)Landroid/graphics/Bitmap;",
        [context](dx::IntrinsicContext& call) {
            const auto array = call.arguments[0].ref;
            const auto offset = call.arguments[1].AsInt();
            const auto stride = call.arguments[2].AsInt();
            const auto width = call.arguments[3].AsInt();
            const auto height = call.arguments[4].AsInt();
            return MakeBitmapFromArray(call, context, array, offset, stride,
                                       width, height);
        });
    builder.FinalMethod("getWidth", "()I",
        [context](dx::IntrinsicContext& call) {
            return dx::VmValue::Int(BitmapOf(call, context).width);
        });
    builder.FinalMethod("getHeight", "()I",
        [context](dx::IntrinsicContext& call) {
            return dx::VmValue::Int(BitmapOf(call, context).height);
        });
    builder.FinalMethod("getPixels", "([IIIIIII)V",
        [context](dx::IntrinsicContext& call) {
            auto& model = call.vm.Model();
            const auto& state = BitmapOf(call, context);
            const auto array = call.arguments[0].ref;
            const auto offset = call.arguments[1].AsInt();
            const auto stride = call.arguments[2].AsInt();
            const auto x = call.arguments[3].AsInt();
            const auto y = call.arguments[4].AsInt();
            const auto width = call.arguments[5].AsInt();
            const auto height = call.arguments[6].AsInt();
            if (!array.IsValid()) {
                throw dx::VmJavaThrow{"Ljava/lang/NullPointerException;",
                                      "getPixels target array is null"};
            }
            if (x < 0 || y < 0 || width < 0 || height < 0 ||
                x + width > state.width || y + height > state.height) {
                throw dx::VmJavaThrow{
                    "Ljava/lang/IllegalArgumentException;",
                    "getPixels region exceeds the bitmap"};
            }
            if (width == 0 || height == 0) return dx::VmValue::Void();
            const auto length =
                static_cast<std::int64_t>(model.ArrayLength(array));
            const auto last = static_cast<std::int64_t>(offset) +
                              static_cast<std::int64_t>(stride) *
                                  (height - 1) +
                              width;
            if (offset < 0 || stride < width || last > length) {
                throw dx::VmJavaThrow{
                    "Ljava/lang/ArrayIndexOutOfBoundsException;",
                    "getPixels window exceeds the target array"};
            }
            for (std::int32_t row = 0; row < height; ++row) {
                for (std::int32_t column = 0; column < width; ++column) {
                    const auto pixel =
                        state.argb[static_cast<std::size_t>(y + row) *
                                       static_cast<std::size_t>(
                                           state.width) +
                                   static_cast<std::size_t>(x + column)];
                    model.SetPrimitiveElement(array,
                                              offset + row * stride + column,
                                              pixel);
                }
            }
            return dx::VmValue::Void();
        });
    builder.FinalMethod("prepareToDraw", "()V", GraphicsNoopHandler());
    builder.FinalMethod("recycle", "()V",
        [context](dx::IntrinsicContext& call) {
            const auto found = context->bitmaps.find(call.receiver.Value());
            if (found != context->bitmaps.end()) {
                found->second.recycled = true;
                found->second.argb.clear();
                found->second.argb.shrink_to_fit();
            }
            return dx::VmValue::Void();
        });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

namespace ogplay::runtime::android_intrinsics {
Decl Declare_android_graphics_Bitmap(const Context& context) {
    return dvm80_android_graphics_Bitmap::Declare_android_graphics_Bitmap(context);
}
}  // namespace ogplay::runtime::android_intrinsics

// ---- migrated from android_graphics_BitmapFactory.cpp ----
#include "ogplay/runtime/integration/host_image_decode.h"

#include "catalog.h"

namespace ogplay::runtime::android_intrinsics::dvm80_android_graphics_BitmapFactory {

Decl Declare_android_graphics_BitmapFactory(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/graphics/BitmapFactory;", "Ljava/lang/Object;");
    builder.StaticMethod("decodeByteArray", "([BII)Landroid/graphics/Bitmap;",
        [context](dx::IntrinsicContext& call) {
            auto& model = call.vm.Model();
            const auto array = call.arguments[0].ref;
            const auto offset = call.arguments[1].AsInt();
            const auto length = call.arguments[2].AsInt();
            if (!array.IsValid()) {
                throw dx::VmJavaThrow{"Ljava/lang/NullPointerException;",
                                      "decodeByteArray data is null"};
            }
            const auto array_length =
                static_cast<std::int64_t>(model.ArrayLength(array));
            if (offset < 0 || length < 0 ||
                static_cast<std::int64_t>(offset) + length > array_length) {
                throw dx::VmJavaThrow{
                    "Ljava/lang/ArrayIndexOutOfBoundsException;",
                    "decodeByteArray range exceeds the data array"};
            }
            const auto bytes = model.ReadByteRegion(array, offset, length);
            const auto decoded = DecodeImageToArgb(bytes);
            if (!decoded.has_value()) {
                // Documented decode-failure result is null, not a throw.
                GuestLog(call, core::LogLevel::warn,
                         "BitmapFactory.decodeByteArray: undecodable image (" +
                             std::to_string(length) + " bytes)");
                return dx::VmValue::Ref(dx::VmObjectRef{});
            }
            DexVmAndroidContext::BitmapState state;
            state.width = decoded->width;
            state.height = decoded->height;
            state.argb = std::move(decoded->argb);
            const auto instance =
                call.vm.NewIntrinsicInstance("Landroid/graphics/Bitmap;");
            context->bitmaps[instance.Value()] = std::move(state);
            return dx::VmValue::Ref(instance);
        });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

namespace ogplay::runtime::android_intrinsics {
Decl Declare_android_graphics_BitmapFactory(const Context& context) {
    return dvm80_android_graphics_BitmapFactory::Declare_android_graphics_BitmapFactory(context);
}
}  // namespace ogplay::runtime::android_intrinsics

// ---- migrated from android_graphics_Canvas.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics::dvm80_android_graphics_Canvas {

Decl Declare_android_graphics_Canvas(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/graphics/Canvas;", "Ljava/lang/Object;");
    builder.FinalMethod("save", "(I)I", [](dx::IntrinsicContext&) {
        return dx::VmValue::Int(1);
    });
    builder.FinalMethod("restore", "()V", GraphicsNoopHandler());
    builder.FinalMethod("clipRect", "(FFFFLandroid/graphics/Region$Op;)Z",
        [](dx::IntrinsicContext&) {
            return dx::VmValue::Int(1);
        });
    builder.FinalMethod("getClipBounds", "()Landroid/graphics/Rect;",
        [context](dx::IntrinsicContext& call) {
            const auto rect =
                call.vm.NewIntrinsicInstance("Landroid/graphics/Rect;");
            const auto slots = call.vm.Model().InstanceSlots(rect);
            slots[0] = {0, dx::SlotTag::cat1};
            slots[1] = {0, dx::SlotTag::cat1};
            slots[2] = {context->surface_width, dx::SlotTag::cat1};
            slots[3] = {context->surface_height, dx::SlotTag::cat1};
            return dx::VmValue::Ref(rect);
        });
    builder.FinalMethod("drawColor", "(I)V", GraphicsNoopHandler());
    builder.FinalMethod("drawBitmap", "(Landroid/graphics/Bitmap;FFLandroid/graphics/Paint;)V", GraphicsNoopHandler());
    builder.FinalMethod("drawBitmap", "([IIIIIIIZLandroid/graphics/Paint;)V", GraphicsNoopHandler());
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

namespace ogplay::runtime::android_intrinsics {
Decl Declare_android_graphics_Canvas(const Context& context) {
    return dvm80_android_graphics_Canvas::Declare_android_graphics_Canvas(context);
}
}  // namespace ogplay::runtime::android_intrinsics

// ---- migrated from android_graphics_drawable_Drawable.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics::dvm80_android_graphics_drawable_Drawable {

Decl Declare_android_graphics_drawable_Drawable(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/graphics/drawable/Drawable;", "Ljava/lang/Object;");
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

namespace ogplay::runtime::android_intrinsics {
Decl Declare_android_graphics_drawable_Drawable(const Context& context) {
    return dvm80_android_graphics_drawable_Drawable::Declare_android_graphics_drawable_Drawable(context);
}
}  // namespace ogplay::runtime::android_intrinsics

// ---- migrated from android_graphics_drawable_PaintDrawable.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics::dvm80_android_graphics_drawable_PaintDrawable {

Decl Declare_android_graphics_drawable_PaintDrawable(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/graphics/drawable/PaintDrawable;", "Landroid/graphics/drawable/Drawable;");
    builder.Constructor("(I)V", GraphicsNoopHandler());
    builder.FinalMethod("setCornerRadius", "(F)V", GraphicsNoopHandler());
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

namespace ogplay::runtime::android_intrinsics {
Decl Declare_android_graphics_drawable_PaintDrawable(const Context& context) {
    return dvm80_android_graphics_drawable_PaintDrawable::Declare_android_graphics_drawable_PaintDrawable(context);
}
}  // namespace ogplay::runtime::android_intrinsics

// ---- migrated from android_graphics_Matrix.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics::dvm80_android_graphics_Matrix {

Decl Declare_android_graphics_Matrix(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/graphics/Matrix;", "Ljava/lang/Object;");
    builder.Constructor("()V", GraphicsNoopHandler());
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

namespace ogplay::runtime::android_intrinsics {
Decl Declare_android_graphics_Matrix(const Context& context) {
    return dvm80_android_graphics_Matrix::Declare_android_graphics_Matrix(context);
}
}  // namespace ogplay::runtime::android_intrinsics

// ---- migrated from android_graphics_Paint.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics::dvm80_android_graphics_Paint {

Decl Declare_android_graphics_Paint(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/graphics/Paint;", "Ljava/lang/Object;");
    builder.Constructor("()V", GraphicsNoopHandler());
    builder.Constructor("(I)V", GraphicsNoopHandler());
    builder.FinalMethod("setColor", "(I)V", GraphicsNoopHandler());
    builder.FinalMethod("setAntiAlias", "(Z)V", GraphicsNoopHandler());
    builder.FinalMethod("setTextSize", "(F)V", GraphicsNoopHandler());
    builder.FinalMethod("setTypeface",
        "(Landroid/graphics/Typeface;)Landroid/graphics/Typeface;",
        [](dx::IntrinsicContext& call) {
            // Returns the typeface that was set, per the platform contract.
            return dx::VmValue::Ref(call.arguments[0].ref);
        });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

namespace ogplay::runtime::android_intrinsics {
Decl Declare_android_graphics_Paint(const Context& context) {
    return dvm80_android_graphics_Paint::Declare_android_graphics_Paint(context);
}
}  // namespace ogplay::runtime::android_intrinsics

// ---- migrated from android_graphics_Rect.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics::dvm80_android_graphics_Rect {

Decl Declare_android_graphics_Rect(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/graphics/Rect;", "Ljava/lang/Object;");
    builder.InstanceField("left", "I");
    builder.InstanceField("top", "I");
    builder.InstanceField("right", "I");
    builder.InstanceField("bottom", "I");
    builder.Constructor("()V", GraphicsNoopHandler());
    builder.FinalMethod("width", "()I", [](dx::IntrinsicContext& call) {
        const auto slots = call.vm.Model().InstanceSlots(call.receiver);
        return dx::VmValue::Int(static_cast<std::int32_t>(slots[2].bits) -
                                static_cast<std::int32_t>(slots[0].bits));
    });
    builder.FinalMethod("height", "()I", [](dx::IntrinsicContext& call) {
        const auto slots = call.vm.Model().InstanceSlots(call.receiver);
        return dx::VmValue::Int(static_cast<std::int32_t>(slots[3].bits) -
                                static_cast<std::int32_t>(slots[1].bits));
    });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

namespace ogplay::runtime::android_intrinsics {
Decl Declare_android_graphics_Rect(const Context& context) {
    return dvm80_android_graphics_Rect::Declare_android_graphics_Rect(context);
}
}  // namespace ogplay::runtime::android_intrinsics

// ---- migrated from android_graphics_Region_Op.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics::dvm80_android_graphics_Region_Op {

Decl Declare_android_graphics_Region_Op(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/graphics/Region$Op;", "Ljava/lang/Object;");
    builder.StaticField("REPLACE", "Landroid/graphics/Region$Op;");
    builder.ClassInitializer([](dx::IntrinsicContext& call) {
        auto& vm = call.vm;
        vm.SetIntrinsicStaticRef(
            "Landroid/graphics/Region$Op;", "REPLACE",
            "Landroid/graphics/Region$Op;",
            vm.NewIntrinsicInstance("Landroid/graphics/Region$Op;"));
        return dx::VmValue::Void();
    });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

namespace ogplay::runtime::android_intrinsics {
Decl Declare_android_graphics_Region_Op(const Context& context) {
    return dvm80_android_graphics_Region_Op::Declare_android_graphics_Region_Op(context);
}
}  // namespace ogplay::runtime::android_intrinsics

// ---- migrated from android_graphics_Typeface.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics::dvm80_android_graphics_Typeface {

Decl Declare_android_graphics_Typeface(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/graphics/Typeface;", "Ljava/lang/Object;");
    builder.StaticField("SERIF", "Landroid/graphics/Typeface;");
    builder.StaticMethod("defaultFromStyle", "(I)Landroid/graphics/Typeface;",
        [](dx::IntrinsicContext& call) {
            return dx::VmValue::Ref(
                call.vm.NewIntrinsicInstance("Landroid/graphics/Typeface;"));
        });
    builder.ClassInitializer([](dx::IntrinsicContext& call) {
        auto& vm = call.vm;
        vm.SetIntrinsicStaticRef(
            "Landroid/graphics/Typeface;", "SERIF",
            "Landroid/graphics/Typeface;",
            vm.NewIntrinsicInstance("Landroid/graphics/Typeface;"));
        return dx::VmValue::Void();
    });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

namespace ogplay::runtime::android_intrinsics {
Decl Declare_android_graphics_Typeface(const Context& context) {
    return dvm80_android_graphics_Typeface::Declare_android_graphics_Typeface(context);
}
}  // namespace ogplay::runtime::android_intrinsics
