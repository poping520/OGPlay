#include "ogplay/runtime/integration/host_image_decode.h"

#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_graphics_BitmapFactory(const Context& context) {
    dx::IntrinsicClassBuilder builder("Landroid/graphics/BitmapFactory;");
    builder.Super("Ljava/lang/Object;");
    builder.Static("decodeByteArray", "([BII)Landroid/graphics/Bitmap;",
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
