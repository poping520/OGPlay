// DVM-80: API-family translation unit. DVM-86 adds the high-frequency API 19
// geometry and drawable value classes here.

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <cctype>
#include <cmath>

// ---- migrated from android_graphics_Bitmap_Config.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

namespace {

constexpr std::string_view kConfigDescriptor =
    "Landroid/graphics/Bitmap$Config;";
constexpr std::array kConfigs{
    std::pair{"ALPHA_8", 1}, std::pair{"RGB_565", 3},
    std::pair{"ARGB_4444", 4}, std::pair{"ARGB_8888", 5}};

[[nodiscard]] dx::VmObjectRef NewConfigArray(
    dx::Interpreter& vm, const std::span<const dx::VmObjectRef> values) {
    const auto array_class = vm.Linker().ResolveDescriptor(
        "[Landroid/graphics/Bitmap$Config;");
    const auto element_class = vm.Linker().ResolveDescriptor(
        kConfigDescriptor);
    const auto array = vm.Model().NewObjectArray(
        array_class, element_class,
        static_cast<JniSize>(values.size()));
    for (std::size_t index = 0; index < values.size(); ++index) {
        vm.Model().SetObjectElement(
            array, static_cast<JniSize>(index), values[index]);
    }
    return array;
}

[[nodiscard]] std::int32_t ReadIntField(
    dx::Interpreter& vm, const dx::VmObjectRef object,
    const std::string_view name) {
    const auto field = vm.Linker().FindFieldRecursive(
        vm.Model().ObjectClass(object), std::string(name), "I");
    if (!field.has_value()) {
        throw dx::DexVmError(dx::DexVmErrorReason::internal_invariant,
                             "intrinsic int field is unavailable: " +
                                 std::string(name));
    }
    const auto& linked = vm.Linker().Field(*field);
    const auto slots = vm.Model().InstanceSlots(object);
    if (linked.is_ref || linked.is_wide || linked.slot >= slots.size()) {
        throw dx::DexVmError(dx::DexVmErrorReason::internal_invariant,
                             "intrinsic int field is invalid: " +
                                 std::string(name));
    }
    return static_cast<std::int32_t>(slots[linked.slot].bits);
}

}  // namespace

Decl Declare_android_graphics_Bitmap_Config(const Context& context) {
    static_cast<void>(context);
    dx::IntrinsicEnumBuilder enum_builder(
        std::string(kConfigDescriptor),
        {"ALPHA_8", "RGB_565", "ARGB_4444", "ARGB_8888"});
    auto& builder = enum_builder.ClassBuilder();
    const auto configs = builder.BoundStaticField(
        "sConfigs", "[Landroid/graphics/Bitmap$Config;", dx::kAccPrivate);
    const auto native_int =
        builder.BoundInstanceField("nativeInt", "I", dx::kAccFinal);
    builder.Constructor("(Ljava/lang/String;II)V",
        [native_int](dx::IntrinsicContext& context) {
            const dx::IntrinsicCall call(context);
            dx::IntrinsicEnumBuilder::InitializeBase(
                context, context.receiver,
                context.vm.StringUtf8(call.NonNullRef(0, "name")),
                call.Int(1));
            call.SetInt(native_int, call.Int(2));
            return dx::VmValue::Void();
        }, dx::kAccPrivate);
    builder.StaticMethod(
        "nativeToConfig", "(I)Landroid/graphics/Bitmap$Config;",
        [configs](dx::IntrinsicContext& context) {
            const dx::IntrinsicCall call(context);
            const auto index = call.Int(0);
            if (index < 0 || index >= 6) {
                throw dx::VmJavaThrow{
                    "Ljava/lang/ArrayIndexOutOfBoundsException;",
                    "Bitmap.Config native index " + std::to_string(index)};
            }
            return dx::VmValue::Ref(
                context.vm.Model().GetObjectElement(call.GetRef(configs), index));
        }, dx::kAccStatic);
    enum_builder.WithConstantInitializer(
        [native_int](dx::IntrinsicContext& context,
                     const dx::VmObjectRef value, std::string_view,
                     const std::int32_t ordinal) {
            const dx::IntrinsicCall call(context);
            call.SetInt(native_int, value,
                        kConfigs[static_cast<std::size_t>(ordinal)].second);
        });
    enum_builder.AfterConstants(
        [configs](dx::IntrinsicContext& context,
                  const std::span<const dx::VmObjectRef> values) {
            const std::array<dx::VmObjectRef, 6> native_configs{
                dx::VmObjectRef{}, values[0], dx::VmObjectRef{}, values[1],
                values[2], values[3]};
            const dx::IntrinsicCall call(context);
            call.SetRef(configs, NewConfigArray(context.vm, native_configs));
        });
    return std::move(enum_builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics


// ---- migrated from android_graphics_Bitmap.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

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

[[nodiscard]] dx::VmValue MakeEmptyBitmap(
    dx::IntrinsicContext& call, const Context& context,
    const std::int32_t width, const std::int32_t height,
    const dx::VmObjectRef config) {
    if (!config.IsValid()) {
        throw dx::VmJavaThrow{"Ljava/lang/NullPointerException;",
                              "createBitmap config is null"};
    }
    const auto config_class = call.vm.Linker().ResolveDescriptor(
        "Landroid/graphics/Bitmap$Config;");
    if (!call.vm.Linker().IsAssignable(
            config_class, call.vm.Model().ObjectClass(config))) {
        throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;",
                              "createBitmap config has the wrong type"};
    }
    if (width <= 0 || height <= 0) {
        throw dx::VmJavaThrow{
            "Ljava/lang/IllegalArgumentException;",
            "createBitmap dimensions are invalid: " +
                std::to_string(width) + "x" + std::to_string(height)};
    }
    const auto pixel_count = static_cast<std::size_t>(width) *
                             static_cast<std::size_t>(height);
    DexVmAndroidContext::BitmapState state;
    state.width = width;
    state.height = height;
    state.argb.resize(pixel_count);
    const auto instance =
        call.vm.NewIntrinsicInstance("Landroid/graphics/Bitmap;");
    context->bitmaps[instance.Value()] = std::move(state);
    return dx::VmValue::Ref(instance);
}

}  // namespace

Decl Declare_android_graphics_Bitmap(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/graphics/Bitmap;", "Ljava/lang/Object;");
    builder.StaticMethod("createBitmap",
        "(IILandroid/graphics/Bitmap$Config;)Landroid/graphics/Bitmap;",
        [context](dx::IntrinsicContext& call) {
            return MakeEmptyBitmap(call, context, call.arguments[0].AsInt(),
                                   call.arguments[1].AsInt(),
                                   call.arguments[2].ref);
        });
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
    builder.FinalMethod("setPixels", "([IIIIIII)V",
        [context](dx::IntrinsicContext& call) {
            auto& model = call.vm.Model();
            auto& state = BitmapOf(call, context);
            const auto array = call.arguments[0].ref;
            const auto offset = call.arguments[1].AsInt();
            const auto stride = call.arguments[2].AsInt();
            const auto x = call.arguments[3].AsInt();
            const auto y = call.arguments[4].AsInt();
            const auto width = call.arguments[5].AsInt();
            const auto height = call.arguments[6].AsInt();
            if (!array.IsValid()) {
                throw dx::VmJavaThrow{"Ljava/lang/NullPointerException;",
                                      "setPixels source array is null"};
            }
            if (x < 0 || y < 0 || width < 0 || height < 0 ||
                x + width > state.width || y + height > state.height) {
                throw dx::VmJavaThrow{
                    "Ljava/lang/IllegalArgumentException;",
                    "setPixels region exceeds the bitmap"};
            }
            if (width == 0 || height == 0) return dx::VmValue::Void();
            const auto final_row = static_cast<std::int64_t>(offset) +
                static_cast<std::int64_t>(stride) * (height - 1);
            const auto first = std::min<std::int64_t>(offset, final_row);
            const auto last = std::max<std::int64_t>(offset, final_row) + width;
            const auto absolute_stride =
                std::abs(static_cast<std::int64_t>(stride));
            if (first < 0 || last > model.ArrayLength(array) ||
                absolute_stride < width) {
                throw dx::VmJavaThrow{
                    "Ljava/lang/ArrayIndexOutOfBoundsException;",
                    "setPixels window exceeds the source array"};
            }
            for (std::int32_t row = 0; row < height; ++row) {
                for (std::int32_t column = 0; column < width; ++column) {
                    state.argb[static_cast<std::size_t>(y + row) *
                                   static_cast<std::size_t>(state.width) +
                               static_cast<std::size_t>(x + column)] =
                        static_cast<std::uint32_t>(model.GetPrimitiveElement(
                            array, offset + row * stride + column));
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


// ---- migrated from android_graphics_BitmapFactory.cpp ----
#include "ogplay/runtime/integration/host_image_decode.h"

#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

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


// ---- migrated from android_graphics_Canvas.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

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
    builder.FinalMethod("drawColor", "(I)V",
        [context](dx::IntrinsicContext& call) {
            const auto found = context->canvases.find(call.receiver.Value());
            if (found == context->canvases.end() || !found->second.locked) {
                throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;",
                                      "Canvas is not locked"};
            }
            std::fill(found->second.argb.begin(), found->second.argb.end(),
                      static_cast<std::uint32_t>(call.arguments[0].AsInt()));
            return dx::VmValue::Void();
        });
    builder.FinalMethod(
        "drawBitmap",
        "(Landroid/graphics/Bitmap;FFLandroid/graphics/Paint;)V",
        [context](dx::IntrinsicContext& call) {
            const auto canvas = context->canvases.find(call.receiver.Value());
            const auto bitmap = context->bitmaps.find(call.arguments[0].ref.Value());
            if (canvas == context->canvases.end() || !canvas->second.locked) {
                throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;",
                                      "Canvas is not locked"};
            }
            if (!call.arguments[0].ref.IsValid() ||
                bitmap == context->bitmaps.end() || bitmap->second.recycled) {
                throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;",
                                      "drawBitmap source is invalid"};
            }
            const auto left_value = call.arguments[1].AsFloat();
            const auto top_value = call.arguments[2].AsFloat();
            if (!std::isfinite(left_value) || !std::isfinite(top_value)) {
                throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;",
                                      "drawBitmap coordinates are not finite"};
            }
            const auto left = static_cast<std::int32_t>(std::floor(left_value));
            const auto top = static_cast<std::int32_t>(std::floor(top_value));
            auto& target = canvas->second;
            const auto& source = bitmap->second;
            for (std::int32_t y = 0; y < source.height; ++y) {
                const auto destination_y = top + y;
                if (destination_y < 0 ||
                    destination_y >= static_cast<std::int32_t>(target.height)) {
                    continue;
                }
                for (std::int32_t x = 0; x < source.width; ++x) {
                    const auto destination_x = left + x;
                    if (destination_x < 0 || destination_x >=
                        static_cast<std::int32_t>(target.width)) {
                        continue;
                    }
                    target.argb[static_cast<std::size_t>(destination_y) *
                                    target.width + destination_x] =
                        source.argb[static_cast<std::size_t>(y) * source.width + x];
                }
            }
            return dx::VmValue::Void();
        });
    builder.FinalMethod("drawBitmap", "([IIIIIIIZLandroid/graphics/Paint;)V", GraphicsNoopHandler());
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics


// ---- migrated from android_graphics_drawable_Drawable.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_graphics_drawable_Drawable(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/graphics/drawable/Drawable;", "Ljava/lang/Object;");
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics


// ---- migrated from android_graphics_drawable_PaintDrawable.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_graphics_drawable_PaintDrawable(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/graphics/drawable/PaintDrawable;", "Landroid/graphics/drawable/Drawable;");
    builder.Constructor("(I)V", GraphicsNoopHandler());
    builder.FinalMethod("setCornerRadius", "(F)V", GraphicsNoopHandler());
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics


// ---- migrated from android_graphics_Matrix.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_graphics_Matrix(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/graphics/Matrix;", "Ljava/lang/Object;");
    builder.Constructor("()V", GraphicsNoopHandler());
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics


// ---- migrated from android_graphics_Paint.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

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


// ---- migrated from android_graphics_Rect.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

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


// ---- migrated from android_graphics_Region_Op.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_graphics_Region_Op(const Context& context) {
    static_cast<void>(context);
    return dx::IntrinsicEnumBuilder(
               "Landroid/graphics/Region$Op;", {"REPLACE"})
        .Build();
}

}  // namespace ogplay::runtime::android_intrinsics


namespace ogplay::runtime::android_intrinsics {
namespace {

[[nodiscard]] std::uint32_t ParseColorValue(const std::string& value) {
    if (!value.empty() && value.front() == '#') {
        const auto digits = value.substr(1);
        if (digits.size() != 6U && digits.size() != 8U) {
            throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "Unknown color"};
        }
        std::uint32_t parsed{};
        const auto result = std::from_chars(digits.data(), digits.data() + digits.size(), parsed, 16);
        if (result.ec != std::errc{} || result.ptr != digits.data() + digits.size())
            throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "Unknown color"};
        return digits.size() == 6U ? (0xff000000U | parsed) : parsed;
    }
    auto normalized = value;
    std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                   [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
    constexpr std::array names{
        std::pair{"black", 0xff000000U}, std::pair{"darkgray", 0xff444444U},
        std::pair{"gray", 0xff888888U}, std::pair{"lightgray", 0xffccccccU},
        std::pair{"white", 0xffffffffU}, std::pair{"red", 0xffff0000U},
        std::pair{"green", 0xff00ff00U}, std::pair{"blue", 0xff0000ffU},
        std::pair{"yellow", 0xffffff00U}, std::pair{"cyan", 0xff00ffffU},
        std::pair{"magenta", 0xffff00ffU}, std::pair{"transparent", 0x00000000U}};
    const auto found = std::find_if(names.begin(), names.end(), [&normalized](const auto& item) {
        return item.first == normalized;
    });
    if (found == names.end())
        throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "Unknown color"};
    return found->second;
}

[[nodiscard]] dx::VmValue FloatBits(const float value) {
    return dx::VmValue::Float(value);
}

}  // namespace

Decl Declare_android_graphics_Color(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/graphics/Color;", "Ljava/lang/Object;");
    for (const auto [name, value] : std::array{
             std::pair{"BLACK", 0xff000000U}, std::pair{"DKGRAY", 0xff444444U},
             std::pair{"GRAY", 0xff888888U}, std::pair{"LTGRAY", 0xffccccccU},
             std::pair{"WHITE", 0xffffffffU}, std::pair{"RED", 0xffff0000U},
             std::pair{"GREEN", 0xff00ff00U}, std::pair{"BLUE", 0xff0000ffU},
             std::pair{"YELLOW", 0xffffff00U}, std::pair{"CYAN", 0xff00ffffU},
             std::pair{"MAGENTA", 0xffff00ffU}, std::pair{"TRANSPARENT", 0x00000000U}}) {
        builder.ConstantInt(
            name, "I", static_cast<std::int32_t>(value),
            dx::kAccPublic | dx::kAccStatic | dx::kAccFinal);
    }
    builder.StaticMethod("alpha", "(I)I", [](dx::IntrinsicContext& call) {
        return dx::VmValue::Int(static_cast<std::uint32_t>(call.arguments[0].AsInt()) >> 24U);
    }).StaticMethod("red", "(I)I", [](dx::IntrinsicContext& call) {
        return dx::VmValue::Int((static_cast<std::uint32_t>(call.arguments[0].AsInt()) >> 16U) & 0xffU);
    }).StaticMethod("green", "(I)I", [](dx::IntrinsicContext& call) {
        return dx::VmValue::Int((static_cast<std::uint32_t>(call.arguments[0].AsInt()) >> 8U) & 0xffU);
    }).StaticMethod("blue", "(I)I", [](dx::IntrinsicContext& call) {
        return dx::VmValue::Int(static_cast<std::uint32_t>(call.arguments[0].AsInt()) & 0xffU);
    }).StaticMethod("rgb", "(III)I", [](dx::IntrinsicContext& call) {
        return dx::VmValue::Int(static_cast<std::int32_t>(0xff000000U |
            ((call.arguments[0].AsInt() & 0xffU) << 16U) |
            ((call.arguments[1].AsInt() & 0xffU) << 8U) |
            (call.arguments[2].AsInt() & 0xffU)));
    }).StaticMethod("argb", "(IIII)I", [](dx::IntrinsicContext& call) {
        return dx::VmValue::Int(static_cast<std::int32_t>(
            ((call.arguments[0].AsInt() & 0xffU) << 24U) |
            ((call.arguments[1].AsInt() & 0xffU) << 16U) |
            ((call.arguments[2].AsInt() & 0xffU) << 8U) |
            (call.arguments[3].AsInt() & 0xffU)));
    }).StaticMethod("parseColor", "(Ljava/lang/String;)I", [](dx::IntrinsicContext& call) {
        if (!call.arguments[0].ref.IsValid())
            throw dx::VmJavaThrow{"Ljava/lang/NullPointerException;", "colorString"};
        return dx::VmValue::Int(static_cast<std::int32_t>(
            ParseColorValue(call.vm.StringUtf8(call.arguments[0].ref))));
    });
    return std::move(builder).Build();
}

Decl Declare_android_graphics_RectF(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/graphics/RectF;", "Ljava/lang/Object;");
    builder.InstanceField("left", "F").InstanceField("top", "F")
        .InstanceField("right", "F").InstanceField("bottom", "F");
    builder.Constructor("()V", GraphicsNoopHandler());
    const auto set = [](dx::IntrinsicContext& call) {
        const auto slots = call.vm.Model().InstanceSlots(call.receiver);
        for (std::size_t index = 0; index < 4U; ++index)
            slots[index] = {std::bit_cast<std::uint32_t>(call.arguments[index].AsFloat()), dx::SlotTag::cat1};
        return dx::VmValue::Void();
    };
    builder.Constructor("(FFFF)V", set).FinalMethod("set", "(FFFF)V", set);
    builder.FinalMethod("width", "()F", [](dx::IntrinsicContext& call) {
        const auto slots = call.vm.Model().InstanceSlots(call.receiver);
        return FloatBits(std::bit_cast<float>(slots[2].bits) - std::bit_cast<float>(slots[0].bits));
    }).FinalMethod("height", "()F", [](dx::IntrinsicContext& call) {
        const auto slots = call.vm.Model().InstanceSlots(call.receiver);
        return FloatBits(std::bit_cast<float>(slots[3].bits) - std::bit_cast<float>(slots[1].bits));
    }).FinalMethod("centerX", "()F", [](dx::IntrinsicContext& call) {
        const auto s = call.vm.Model().InstanceSlots(call.receiver);
        return FloatBits((std::bit_cast<float>(s[0].bits) + std::bit_cast<float>(s[2].bits)) * 0.5F);
    }).FinalMethod("centerY", "()F", [](dx::IntrinsicContext& call) {
        const auto s = call.vm.Model().InstanceSlots(call.receiver);
        return FloatBits((std::bit_cast<float>(s[1].bits) + std::bit_cast<float>(s[3].bits)) * 0.5F);
    }).FinalMethod("isEmpty", "()Z", [](dx::IntrinsicContext& call) {
        const auto s = call.vm.Model().InstanceSlots(call.receiver);
        return dx::VmValue::Int(std::bit_cast<float>(s[0].bits) >= std::bit_cast<float>(s[2].bits) ||
                                std::bit_cast<float>(s[1].bits) >= std::bit_cast<float>(s[3].bits));
    }).FinalMethod("contains", "(FF)Z", [](dx::IntrinsicContext& call) {
        const auto s = call.vm.Model().InstanceSlots(call.receiver);
        const auto x = call.arguments[0].AsFloat();
        const auto y = call.arguments[1].AsFloat();
        return dx::VmValue::Int(x >= std::bit_cast<float>(s[0].bits) &&
            x < std::bit_cast<float>(s[2].bits) && y >= std::bit_cast<float>(s[1].bits) &&
            y < std::bit_cast<float>(s[3].bits));
    }).FinalMethod("offset", "(FF)V", [](dx::IntrinsicContext& call) {
        const auto s = call.vm.Model().InstanceSlots(call.receiver);
        const auto dxv = call.arguments[0].AsFloat();
        const auto dyv = call.arguments[1].AsFloat();
        for (const auto index : {0U, 2U}) s[index].bits = std::bit_cast<std::uint32_t>(std::bit_cast<float>(s[index].bits) + dxv);
        for (const auto index : {1U, 3U}) s[index].bits = std::bit_cast<std::uint32_t>(std::bit_cast<float>(s[index].bits) + dyv);
        return dx::VmValue::Void();
    });
    return std::move(builder).Build();
}

Decl Declare_android_graphics_Point(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/graphics/Point;", "Ljava/lang/Object;");
    builder.InstanceField("x", "I").InstanceField("y", "I");
    builder.Constructor("()V", GraphicsNoopHandler());
    const auto set = [](dx::IntrinsicContext& call) {
        const auto s = call.vm.Model().InstanceSlots(call.receiver);
        s[0] = {static_cast<std::uint32_t>(call.arguments[0].AsInt()), dx::SlotTag::cat1};
        s[1] = {static_cast<std::uint32_t>(call.arguments[1].AsInt()), dx::SlotTag::cat1};
        return dx::VmValue::Void();
    };
    builder.Constructor("(II)V", set).FinalMethod("set", "(II)V", set);
    builder.FinalMethod("offset", "(II)V", [](dx::IntrinsicContext& call) {
        const auto s = call.vm.Model().InstanceSlots(call.receiver);
        s[0].bits += static_cast<std::uint32_t>(call.arguments[0].AsInt());
        s[1].bits += static_cast<std::uint32_t>(call.arguments[1].AsInt());
        return dx::VmValue::Void();
    });
    return std::move(builder).Build();
}

Decl Declare_android_graphics_PointF(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/graphics/PointF;", "Ljava/lang/Object;");
    builder.InstanceField("x", "F").InstanceField("y", "F");
    builder.Constructor("()V", GraphicsNoopHandler());
    const auto set = [](dx::IntrinsicContext& call) {
        const auto s = call.vm.Model().InstanceSlots(call.receiver);
        s[0] = {std::bit_cast<std::uint32_t>(call.arguments[0].AsFloat()), dx::SlotTag::cat1};
        s[1] = {std::bit_cast<std::uint32_t>(call.arguments[1].AsFloat()), dx::SlotTag::cat1};
        return dx::VmValue::Void();
    };
    builder.Constructor("(FF)V", set).FinalMethod("set", "(FF)V", set);
    builder.FinalMethod("length", "()F", [](dx::IntrinsicContext& call) {
        const auto s = call.vm.Model().InstanceSlots(call.receiver);
        return FloatBits(std::hypot(std::bit_cast<float>(s[0].bits), std::bit_cast<float>(s[1].bits)));
    }).StaticMethod("length", "(FF)F", [](dx::IntrinsicContext& call) {
        return FloatBits(std::hypot(call.arguments[0].AsFloat(), call.arguments[1].AsFloat()));
    });
    return std::move(builder).Build();
}

Decl Declare_android_graphics_Path_Direction(const Context& context) {
    static_cast<void>(context);
    dx::IntrinsicEnumBuilder enum_builder(
        "Landroid/graphics/Path$Direction;", {"CW", "CCW"});
    auto& builder = enum_builder.ClassBuilder();
    const auto native_int = builder.BoundInstanceField(
        "nativeInt", "I", dx::kAccPublic | dx::kAccFinal);
    builder.Constructor("(I)V", [native_int](dx::IntrinsicContext& context) {
        const dx::IntrinsicCall call(context);
        call.SetInt(native_int, call.Int(0));
        return dx::VmValue::Void();
    }, dx::kAccPrivate);
    enum_builder.WithConstantInitializer(
        [native_int](dx::IntrinsicContext& context,
                     const dx::VmObjectRef value, std::string_view,
                     const std::int32_t ordinal) {
            const dx::IntrinsicCall call(context);
            call.SetInt(native_int, value, ordinal);
        });
    return std::move(enum_builder).Build();
}

Decl Declare_android_graphics_Path(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/graphics/Path;", "Ljava/lang/Object;");
    builder.Constructor("()V", [context](dx::IntrinsicContext& call) {
        context->paths.try_emplace(call.receiver.Value());
        return dx::VmValue::Void();
    });
    builder.Constructor("(Landroid/graphics/Path;)V", [context](dx::IntrinsicContext& call) {
        if (!call.arguments[0].ref.IsValid())
            throw dx::VmJavaThrow{"Ljava/lang/NullPointerException;", "src"};
        context->paths[call.receiver.Value()] = context->paths[call.arguments[0].ref.Value()];
        return dx::VmValue::Void();
    });
    builder.FinalMethod("reset", "()V", [context](dx::IntrinsicContext& call) {
        context->paths[call.receiver.Value()].commands.clear(); return dx::VmValue::Void();
    }).FinalMethod("rewind", "()V", [context](dx::IntrinsicContext& call) {
        context->paths[call.receiver.Value()].commands.clear(); return dx::VmValue::Void();
    }).FinalMethod("isEmpty", "()Z", [context](dx::IntrinsicContext& call) {
        return dx::VmValue::Int(context->paths[call.receiver.Value()].commands.empty());
    });
    const auto point = [context](const auto verb) {
        return dx::IntrinsicHandler([context, verb](dx::IntrinsicContext& call) {
            context->paths[call.receiver.Value()].commands.push_back(
                {verb, call.arguments[0].AsFloat(), call.arguments[1].AsFloat()});
            return dx::VmValue::Void();
        });
    };
    builder.FinalMethod("moveTo", "(FF)V", point(DexVmAndroidContext::PathState::Verb::move))
        .FinalMethod("lineTo", "(FF)V", point(DexVmAndroidContext::PathState::Verb::line));
    builder.FinalMethod("close", "()V", [context](dx::IntrinsicContext& call) {
        context->paths[call.receiver.Value()].commands.push_back(
            {DexVmAndroidContext::PathState::Verb::close});
        return dx::VmValue::Void();
    }).FinalMethod("addRect", "(FFFFLandroid/graphics/Path$Direction;)V",
        [context](dx::IntrinsicContext& call) {
            if (!call.arguments[4].ref.IsValid())
                throw dx::VmJavaThrow{"Ljava/lang/NullPointerException;", "direction"};
            const auto direction =
                ReadIntField(call.vm, call.arguments[4].ref, "nativeInt");
            context->paths[call.receiver.Value()].commands.push_back({
                DexVmAndroidContext::PathState::Verb::rect,
                call.arguments[0].AsFloat(), call.arguments[1].AsFloat(),
                call.arguments[2].AsFloat(), call.arguments[3].AsFloat(), direction});
            return dx::VmValue::Void();
        });
    return std::move(builder).Build();
}

Decl Declare_android_graphics_PorterDuff_Mode(const Context& context) {
    static_cast<void>(context);
    constexpr std::array modes{
        std::pair{"CLEAR", 0}, std::pair{"SRC", 1}, std::pair{"DST", 2},
        std::pair{"SRC_OVER", 3}, std::pair{"DST_OVER", 4},
        std::pair{"SRC_IN", 5}, std::pair{"DST_IN", 6}, std::pair{"SRC_OUT", 7},
        std::pair{"DST_OUT", 8}, std::pair{"SRC_ATOP", 9}, std::pair{"DST_ATOP", 10},
        std::pair{"XOR", 11}, std::pair{"DARKEN", 12}, std::pair{"LIGHTEN", 13},
        std::pair{"MULTIPLY", 14}, std::pair{"SCREEN", 15}, std::pair{"ADD", 16},
        std::pair{"OVERLAY", 17}};
    dx::IntrinsicEnumBuilder enum_builder(
        "Landroid/graphics/PorterDuff$Mode;",
        {"CLEAR", "SRC", "DST", "SRC_OVER", "DST_OVER", "SRC_IN",
         "DST_IN", "SRC_OUT", "DST_OUT", "SRC_ATOP", "DST_ATOP", "XOR",
         "DARKEN", "LIGHTEN", "MULTIPLY", "SCREEN", "ADD", "OVERLAY"});
    const auto native_int = enum_builder.ClassBuilder().BoundInstanceField(
        "nativeInt", "I", dx::kAccPublic | dx::kAccFinal);
    enum_builder.WithConstantInitializer(
        [native_int, modes](dx::IntrinsicContext& context,
                            const dx::VmObjectRef value, std::string_view,
                            const std::int32_t ordinal) {
            const dx::IntrinsicCall call(context);
            call.SetInt(native_int, value,
                        modes[static_cast<std::size_t>(ordinal)].second);
        });
    return std::move(enum_builder).Build();
}

Decl Declare_android_graphics_PorterDuff(const Context& context) {
    static_cast<void>(context);
    return dx::IntrinsicClassBuilder::Class("Landroid/graphics/PorterDuff;", "Ljava/lang/Object;").Build();
}

Decl Declare_android_graphics_drawable_BitmapDrawable(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class(
        "Landroid/graphics/drawable/BitmapDrawable;", "Landroid/graphics/drawable/Drawable;");
    builder.InstanceField("bitmap", "Landroid/graphics/Bitmap;",
                          dx::kAccPrivate | dx::kAccFinal);
    const auto bitmap_ctor = [](const std::size_t index) {
        return dx::IntrinsicHandler([index](dx::IntrinsicContext& call) {
            call.vm.Model().InstanceSlots(call.receiver)[0] = {
                call.arguments[index].ref.Value(), dx::SlotTag::ref};
            return dx::VmValue::Void();
        });
    };
    builder.Constructor("(Landroid/graphics/Bitmap;)V", bitmap_ctor(0))
        .Constructor("(Landroid/content/res/Resources;Landroid/graphics/Bitmap;)V", bitmap_ctor(1));
    builder.FinalMethod("getBitmap", "()Landroid/graphics/Bitmap;", [](dx::IntrinsicContext& call) {
        return dx::VmValue::Ref(dx::VmObjectRef(call.vm.Model().InstanceSlots(call.receiver)[0].bits));
    });
    return std::move(builder).Build();
}

Decl Declare_android_graphics_drawable_ColorDrawable(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class(
        "Landroid/graphics/drawable/ColorDrawable;", "Landroid/graphics/drawable/Drawable;");
    builder.InstanceField("color", "I", dx::kAccPrivate);
    builder.Constructor("()V", GraphicsNoopHandler());
    const auto set = [](dx::IntrinsicContext& call) {
        call.vm.Model().InstanceSlots(call.receiver)[0] = {
            static_cast<std::uint32_t>(call.arguments[0].AsInt()), dx::SlotTag::cat1};
        return dx::VmValue::Void();
    };
    builder.Constructor("(I)V", set).FinalMethod("setColor", "(I)V", set);
    builder.FinalMethod("getColor", "()I", [](dx::IntrinsicContext& call) {
        return dx::VmValue::Int(static_cast<std::int32_t>(
            call.vm.Model().InstanceSlots(call.receiver)[0].bits));
    });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

// ---- migrated from android_graphics_Typeface.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

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
