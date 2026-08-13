#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_java_io_DataInputStream(const Context& context) {
    const auto handlers = MakeAndroidHandlers(context);
    dx::IntrinsicClassBuilder builder("Ljava/io/DataInputStream;");
    builder.Super("Ljava/io/InputStream;");
    builder.Virtual("<init>", "(Ljava/io/InputStream;)V", handlers.handler_android_reader_adopt_stream);
    builder.Virtual("readFully", "([B)V", handlers.handler_android_data_input_read_fully);
    builder.Virtual("skipBytes", "(I)I", handlers.handler_android_data_input_skip_bytes);
    builder.Virtual("readInt", "()I", handlers.handler_android_data_input_read_int);
    builder.Virtual("readLong", "()J", handlers.handler_android_data_input_read_long);
    builder.Virtual("readUTF", "()Ljava/lang/String;", handlers.handler_android_data_input_read_utf);
    builder.Virtual("close", "()V", handlers.handler_android_stream_close);
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
