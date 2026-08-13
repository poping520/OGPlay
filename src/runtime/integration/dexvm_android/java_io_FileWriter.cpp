#include <string>
#include <utility>

#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_java_io_FileWriter(const Context& context) {
    dx::IntrinsicClassBuilder builder("Ljava/io/FileWriter;");
    builder.Super("Ljava/io/Writer;");
    builder.Virtual("<init>", "(Ljava/io/File;Z)V",
        [context](dx::IntrinsicContext& call) {
            const auto path = FilePathOf(call, call.arguments[0].ref);
            DexVmAndroidContext::OutputStream output{path, {}, false};
            if (call.arguments[1].AsInt() != 0) {
                if (const auto existing = VfsReadAll(context, path)) {
                    output.bytes = *existing;
                }
            }
            context->output_streams[call.receiver.Value()] =
                std::move(output);
            return dx::VmValue::Void();
        });
    builder.Virtual("append", "(C)Ljava/io/Writer;",
        [context](dx::IntrinsicContext& call) {
            auto& output = OutputOf(call, context);
            // BMP code unit encoded as UTF-8 (ASCII fast path; otherwise a
            // string round-trip through the interpreter's UTF-8 rendering).
            const auto unit = static_cast<char16_t>(
                call.arguments[0].cat1 & 0xffffU);
            std::string encoded;
            if (unit < 0x80U) {
                encoded.push_back(static_cast<char>(unit));
            } else {
                encoded = call.vm.StringUtf8(
                    call.vm.Model().NewString(std::u16string(1, unit)));
            }
            for (const auto character : encoded) {
                output.bytes.push_back(static_cast<std::byte>(character));
            }
            return dx::VmValue::Ref(call.receiver);
        });
    builder.Virtual("append", "(Ljava/lang/CharSequence;)Ljava/io/Writer;",
        [context](dx::IntrinsicContext& call) {
            auto& output = OutputOf(call, context);
            const auto value = call.arguments[0].ref;
            const auto text = value.IsValid()
                                  ? call.vm.StringUtf8(value)
                                  : std::string("null");
            for (const auto character : text) {
                output.bytes.push_back(static_cast<std::byte>(character));
            }
            return dx::VmValue::Ref(call.receiver);
        });
    builder.Virtual("flush", "()V", FileOutputFlushHandler(context));
    builder.Virtual("close", "()V", FileOutputCloseHandler(context));
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
