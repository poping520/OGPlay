#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_java_io_BufferedInputStream(const Context& context) {
    const auto handlers = MakeAndroidHandlers(context);
    dx::IntrinsicClassBuilder builder("Ljava/io/BufferedInputStream;");
    builder.Super("Ljava/io/FilterInputStream;");
    builder.Virtual("<init>", "(Ljava/io/InputStream;)V", handlers.handler_android_reader_adopt_stream);
    builder.Virtual("<init>", "(Ljava/io/InputStream;I)V", handlers.handler_android_reader_adopt_stream);
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
