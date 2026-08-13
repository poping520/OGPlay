#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_java_io_InputStream(const Context& context) {
    const auto handlers = MakeAndroidHandlers(context);
    dx::IntrinsicClassBuilder builder("Ljava/io/InputStream;");
    builder.Super("Ljava/lang/Object;");
    builder.Overridable("read", "([BII)I", handlers.handler_android_stream_read_range);
    builder.Overridable("read", "([B)I", handlers.handler_android_stream_read_full);
    builder.Overridable("read", "()I", handlers.handler_android_stream_read_one);
    builder.Overridable("available", "()I", handlers.handler_android_stream_available);
    builder.Overridable("close", "()V", handlers.handler_android_stream_close);
    builder.Overridable("skip", "(J)J", handlers.handler_android_stream_skip);
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
