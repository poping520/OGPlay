#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_java_io_InputStreamReader(const Context& context) {
    const auto handlers = MakeAndroidHandlers(context);
    dx::IntrinsicClassBuilder builder("Ljava/io/InputStreamReader;");
    builder.Super("Ljava/io/Reader;");
    builder.Virtual("<init>", "(Ljava/io/InputStream;)V", handlers.handler_android_reader_adopt_stream);
    builder.Virtual("<init>", "(Ljava/io/InputStream;Ljava/nio/charset/Charset;)V", handlers.handler_android_reader_adopt_stream);
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
