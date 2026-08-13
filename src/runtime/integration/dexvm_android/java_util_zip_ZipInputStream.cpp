#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_java_util_zip_ZipInputStream(const Context& context) {
    const auto handlers = MakeAndroidHandlers(context);
    dx::IntrinsicClassBuilder builder("Ljava/util/zip/ZipInputStream;");
    builder.Super("Ljava/io/FilterInputStream;");
    builder.Virtual("<init>", "(Ljava/io/InputStream;)V", handlers.handler_android_zip_input_init);
    builder.Virtual("getNextEntry", "()Ljava/util/zip/ZipEntry;", handlers.handler_android_zip_input_get_next_entry);
    builder.Virtual("read", "([BII)I", handlers.handler_android_zip_input_read_range);
    builder.Virtual("closeEntry", "()V", handlers.handler_android_zip_input_close_entry);
    builder.Virtual("close", "()V", handlers.handler_android_zip_input_close);
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
