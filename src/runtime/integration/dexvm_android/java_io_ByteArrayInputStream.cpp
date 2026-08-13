#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_java_io_ByteArrayInputStream(const Context& context) {
    const auto handlers = MakeAndroidHandlers(context);
    dx::IntrinsicClassBuilder builder("Ljava/io/ByteArrayInputStream;");
    builder.Super("Ljava/io/InputStream;");
    builder.Virtual("<init>", "([B)V", handlers.handler_android_byte_stream_init_input);
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
