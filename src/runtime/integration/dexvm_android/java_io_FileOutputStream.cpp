#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_java_io_FileOutputStream(const Context& context) {
    const auto handlers = MakeAndroidHandlers(context);
    dx::IntrinsicClassBuilder builder("Ljava/io/FileOutputStream;");
    builder.Super("Ljava/io/OutputStream;");
    builder.Virtual("<init>", "(Ljava/lang/String;)V", handlers.handler_android_file_output_init_path);
    builder.Virtual("<init>", "(Ljava/io/File;)V", handlers.handler_android_file_output_init_file);
    builder.Virtual("write", "([B)V", handlers.handler_android_file_output_write_bytes);
    builder.Virtual("flush", "()V", handlers.handler_android_file_output_flush);
    builder.Virtual("close", "()V", handlers.handler_android_file_output_close);
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
