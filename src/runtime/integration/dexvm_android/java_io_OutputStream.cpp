#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_java_io_OutputStream(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Ljava/io/OutputStream;", "Ljava/lang/Object;");
    builder.VirtualMethod("write", "([BII)V", ByteOutputWriteRangeHandler(context));
    builder.VirtualMethod("write", "([B)V", FileOutputWriteBytesHandler(context));
    builder.VirtualMethod("write", "(I)V",
        [context](dx::IntrinsicContext& call) {
            const auto found =
                context->output_streams.find(call.receiver.Value());
            if (found == context->output_streams.end() ||
                found->second.closed) {
                throw dx::VmJavaThrow{"Ljava/io/IOException;",
                                      "output stream is closed"};
            }
            found->second.bytes.push_back(static_cast<std::byte>(
                call.arguments[0].AsInt() & 0xff));
            return dx::VmValue::Void();
        });
    builder.VirtualMethod("flush", "()V", FileOutputFlushHandler(context));
    builder.VirtualMethod("close", "()V", FileOutputCloseHandler(context));
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
