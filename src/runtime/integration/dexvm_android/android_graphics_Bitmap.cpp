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
