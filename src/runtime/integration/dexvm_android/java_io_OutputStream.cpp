#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_java_io_OutputStream(const Context& context) {
    const auto handlers = MakeAndroidHandlers(context);
    dx::IntrinsicClassBuilder builder("Ljava/io/OutputStream;");
    builder.Super("Ljava/lang/Object;");
    builder.Overridable("write", "([BII)V", handlers.handler_android_byte_output_write_range);
    builder.Overridable("write", "([B)V", handlers.handler_android_file_output_write_bytes);
    builder.Overridable("write", "(I)V", handlers.handler_android_output_write_one);
    builder.Overridable("flush", "()V", handlers.handler_android_file_output_flush);
    builder.Overridable("close", "()V", handlers.handler_android_file_output_close);
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
