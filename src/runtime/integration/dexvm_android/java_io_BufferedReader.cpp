#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_java_io_BufferedReader(const Context& context) {
    const auto handlers = MakeAndroidHandlers(context);
    dx::IntrinsicClassBuilder builder("Ljava/io/BufferedReader;");
    builder.Super("Ljava/io/Reader;");
    builder.Virtual("<init>", "(Ljava/io/Reader;)V", handlers.handler_android_reader_adopt_stream);
    builder.Virtual("readLine", "()Ljava/lang/String;", handlers.handler_android_reader_read_line);
    builder.Virtual("ready", "()Z", handlers.handler_android_reader_ready);
    builder.Virtual("close", "()V", handlers.handler_android_stream_close);
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
