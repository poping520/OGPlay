#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_java_io_ByteArrayOutputStream(const Context& context) {
    const auto handlers = MakeAndroidHandlers(context);
    dx::IntrinsicClassBuilder builder("Ljava/io/ByteArrayOutputStream;");
    builder.Super("Ljava/io/OutputStream;");
    builder.Virtual("<init>", "()V", handlers.handler_android_byte_output_init);
    builder.Virtual("write", "([BII)V", handlers.handler_android_byte_output_write_range);
    builder.Virtual("write", "([B)V", handlers.handler_android_file_output_write_bytes);
    builder.Virtual("toByteArray", "()[B", handlers.handler_android_byte_output_to_byte_array);
    builder.Virtual("size", "()I", handlers.handler_android_byte_output_size);
    builder.Virtual("toString", "()Ljava/lang/String;", handlers.handler_android_byte_output_to_string);
    builder.Virtual("close", "()V", handlers.handler_android_graphics_noop);
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
