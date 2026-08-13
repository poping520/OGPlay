#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_java_io_FileWriter(const Context& context) {
    const auto handlers = MakeAndroidHandlers(context);
    dx::IntrinsicClassBuilder builder("Ljava/io/FileWriter;");
    builder.Super("Ljava/io/Writer;");
    builder.Virtual("<init>", "(Ljava/io/File;Z)V", handlers.handler_android_file_writer_init_file_append);
    builder.Virtual("append", "(C)Ljava/io/Writer;", handlers.handler_android_file_writer_append_char);
    builder.Virtual("append", "(Ljava/lang/CharSequence;)Ljava/io/Writer;", handlers.handler_android_file_writer_append_sequence);
    builder.Virtual("flush", "()V", handlers.handler_android_file_output_flush);
    builder.Virtual("close", "()V", handlers.handler_android_file_output_close);
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
