#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_java_io_ByteArrayOutputStream(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Ljava/io/ByteArrayOutputStream;", "Ljava/io/OutputStream;");
    builder.Constructor("()V",
        [context](dx::IntrinsicContext& call) {
            // No path: bytes stay in memory and never publish to a file.
            context->output_streams[call.receiver.Value()] =
                DexVmAndroidContext::OutputStream{{}, {}, false};
            return dx::VmValue::Void();
        });
    builder.FinalMethod("write", "([BII)V", ByteOutputWriteRangeHandler(context));
    builder.FinalMethod("write", "([B)V", FileOutputWriteBytesHandler(context));
    builder.FinalMethod("toByteArray", "()[B",
        [context](dx::IntrinsicContext& call) {
            auto& output = OutputOf(call, context);
            auto& vm = call.vm;
            const auto array_class = vm.Linker().ResolveDescriptor("[B");
            const auto array = vm.Model().NewPrimitiveArray(
                array_class, JniPrimitiveKind::byte,
                static_cast<JniSize>(output.bytes.size()));
            if (!output.bytes.empty()) {
                vm.Model().WriteByteRegion(array, 0, output.bytes);
            }
            return dx::VmValue::Ref(array);
        });
    builder.FinalMethod("size", "()I",
        [context](dx::IntrinsicContext& call) {
            return dx::VmValue::Int(static_cast<std::int32_t>(
                OutputOf(call, context).bytes.size()));
        });
    builder.FinalMethod("toString", "()Ljava/lang/String;",
        [context](dx::IntrinsicContext& call) {
            auto& output = OutputOf(call, context);
            return dx::VmValue::Ref(call.vm.NewStringUtf8(std::string(
                reinterpret_cast<const char*>(output.bytes.data()),
                output.bytes.size())));
        });
    builder.FinalMethod("close", "()V", GraphicsNoopHandler());
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
