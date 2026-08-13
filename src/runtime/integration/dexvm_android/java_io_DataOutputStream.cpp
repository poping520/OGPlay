#include <cstddef>
#include <utility>

#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_java_io_DataOutputStream(const Context& context) {
    dx::IntrinsicClassBuilder builder("Ljava/io/DataOutputStream;");
    builder.Super("Ljava/io/OutputStream;");
    builder.Virtual("<init>", "(Ljava/io/OutputStream;)V",
        [context](dx::IntrinsicContext& call) {
            // Chain: reuse the wrapped stream's output slot.
            const auto target = call.arguments[0].ref;
            const auto found = context->output_streams.find(target.Value());
            if (found == context->output_streams.end()) {
                throw dx::VmJavaThrow{"Ljava/io/IOException;",
                                      "DataOutputStream target is not open"};
            }
            // The wrapper owns the same stream state. Moving the record and
            // retiring the old handle makes the common dos.close();
            // fos.close() sequence idempotent instead of letting the stale
            // empty buffer truncate the file after DataOutputStream
            // published it.
            context->output_streams[call.receiver.Value()] =
                std::move(found->second);
            context->output_streams.erase(target.Value());
            return dx::VmValue::Void();
        });
    builder.Virtual("writeUTF", "(Ljava/lang/String;)V",
        [context](dx::IntrinsicContext& call) {
            auto found = context->output_streams.find(call.receiver.Value());
            if (found == context->output_streams.end() ||
                found->second.closed) {
                throw dx::VmJavaThrow{"Ljava/io/IOException;",
                                      "DataOutputStream is closed"};
            }
            const auto text = call.vm.StringUtf8(call.arguments[0].ref);
            auto& bytes = found->second.bytes;
            bytes.push_back(
                static_cast<std::byte>((text.size() >> 8U) & 0xffU));
            bytes.push_back(static_cast<std::byte>(text.size() & 0xffU));
            for (const auto character : text) {
                bytes.push_back(static_cast<std::byte>(character));
            }
            return dx::VmValue::Void();
        });
    builder.Virtual("close", "()V", [context](dx::IntrinsicContext& call) {
        FlushOutput(call, context, call.receiver.Value());
        return dx::VmValue::Void();
    });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
