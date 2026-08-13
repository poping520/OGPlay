#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_java_io_FileReader(const Context& context) {
    const auto handlers = MakeAndroidHandlers(context);
    dx::IntrinsicClassBuilder builder("Ljava/io/FileReader;");
    builder.Super("Ljava/io/Reader;");
    builder.Virtual("<init>", "(Ljava/lang/String;)V", handlers.handler_android_file_stream_init_path);
    builder.Virtual("<init>", "(Ljava/io/File;)V", handlers.handler_android_file_stream_init_file);
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
