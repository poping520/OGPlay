#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_java_io_BufferedReader(const Context& context) {
    dx::IntrinsicClassBuilder builder("Ljava/io/BufferedReader;");
    builder.Super("Ljava/io/Reader;");
    builder.Virtual("<init>", "(Ljava/io/Reader;)V", ReaderAdoptStreamHandler(context));
    builder.Virtual("readLine", "()Ljava/lang/String;",
        [context](dx::IntrinsicContext& call) {
            auto& stream = StreamOf(call, context);
            if (stream.cursor >= stream.bytes.size()) {
                return dx::VmValue::Ref(dx::VmObjectRef{});  // EOF is null
            }
            // Line terminators: \n, \r\n or \r; decoded as UTF-8 (the
            // platform default; explicit charsets are not tracked yet).
            std::string line;
            while (stream.cursor < stream.bytes.size()) {
                const auto byte =
                    static_cast<char>(stream.bytes[stream.cursor++]);
                if (byte == '\n') break;
                if (byte == '\r') {
                    if (stream.cursor < stream.bytes.size() &&
                        static_cast<char>(stream.bytes[stream.cursor]) ==
                            '\n') {
                        ++stream.cursor;
                    }
                    break;
                }
                line.push_back(byte);
            }
            return dx::VmValue::Ref(call.vm.NewStringUtf8(line));
        });
    builder.Virtual("ready", "()Z",
        [context](dx::IntrinsicContext& call) {
            auto& stream = StreamOf(call, context);
            return dx::VmValue::Int(
                stream.cursor < stream.bytes.size() ? 1 : 0);
        });
    builder.Virtual("close", "()V", StreamCloseHandler(context));
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
