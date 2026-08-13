#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_java_io_DataOutputStream(const Context& context) {
    const auto handlers = MakeAndroidHandlers(context);
    dx::IntrinsicClassBuilder builder("Ljava/io/DataOutputStream;");
    builder.Super("Ljava/io/OutputStream;");
    builder.Virtual("<init>", "(Ljava/io/OutputStream;)V", handlers.handler_android_data_output_init);
    builder.Virtual("writeUTF", "(Ljava/lang/String;)V", handlers.handler_android_data_output_write_utf);
    builder.Virtual("close", "()V", handlers.handler_android_data_output_close);
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
