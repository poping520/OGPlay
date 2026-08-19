#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_java_io_ByteArrayInputStream(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Ljava/io/ByteArrayInputStream;", "Ljava/io/InputStream;");
    builder.Constructor("([B)V",
        [context](dx::IntrinsicContext& call) {
            auto& model = call.vm.Model();
            const auto array = call.arguments[0].ref;
            context->streams[call.receiver.Value()] =
                DexVmAndroidContext::Stream{
                    model.ReadByteRegion(array, 0, model.ArrayLength(array)),
                    0, false};
            return dx::VmValue::Void();
        });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
