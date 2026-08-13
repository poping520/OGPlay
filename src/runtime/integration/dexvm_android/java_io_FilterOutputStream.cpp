#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_java_io_FilterOutputStream(const Context& context) {
    const auto handlers = MakeAndroidHandlers(context);
    dx::IntrinsicClassBuilder builder("Ljava/io/FilterOutputStream;");
    builder.Super("Ljava/io/OutputStream;");
    builder.Virtual("<init>", "(Ljava/io/OutputStream;)V", handlers.handler_android_output_adopt);
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
