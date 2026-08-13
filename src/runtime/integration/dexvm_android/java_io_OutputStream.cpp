#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_java_io_OutputStream(const Context& context) {
    dx::IntrinsicClassBuilder builder("Ljava/io/OutputStream;");
    builder.Super("Ljava/lang/Object;");
    builder.Overridable("write", "([BII)V", ByteOutputWriteRangeHandler(context));
    builder.Overridable("write", "([B)V", FileOutputWriteBytesHandler(context));
    builder.Overridable("write", "(I)V",
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
    builder.Overridable("flush", "()V", FileOutputFlushHandler(context));
    builder.Overridable("close", "()V", FileOutputCloseHandler(context));
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
