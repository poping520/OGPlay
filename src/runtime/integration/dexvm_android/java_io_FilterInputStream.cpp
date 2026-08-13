#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_java_io_FilterInputStream(const Context& context) {
    const auto handlers = MakeAndroidHandlers(context);
    dx::IntrinsicClassBuilder builder("Ljava/io/FilterInputStream;");
    builder.Super("Ljava/io/InputStream;");
    builder.Virtual("<init>", "(Ljava/io/InputStream;)V", handlers.handler_android_reader_adopt_stream);
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
