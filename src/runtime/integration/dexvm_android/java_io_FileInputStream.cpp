#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_java_io_FileInputStream(const Context& context) {
    const auto handlers = MakeAndroidHandlers(context);
    dx::IntrinsicClassBuilder builder("Ljava/io/FileInputStream;");
    builder.Super("Ljava/io/InputStream;");
    builder.Virtual("<init>", "(Ljava/io/File;)V", handlers.handler_android_file_stream_init_file);
    builder.Virtual("<init>", "(Ljava/lang/String;)V", handlers.handler_android_file_stream_init_path);
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
