#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_java_io_BufferedOutputStream(const Context& context) {
    const auto handlers = MakeAndroidHandlers(context);
    dx::IntrinsicClassBuilder builder("Ljava/io/BufferedOutputStream;");
    builder.Super("Ljava/io/FilterOutputStream;");
    builder.Virtual("<init>", "(Ljava/io/OutputStream;)V", handlers.handler_android_output_adopt);
    builder.Virtual("<init>", "(Ljava/io/OutputStream;I)V", handlers.handler_android_output_adopt);
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
