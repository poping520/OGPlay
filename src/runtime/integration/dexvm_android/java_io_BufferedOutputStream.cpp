#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_java_io_BufferedOutputStream(const Context& context) {
    dx::IntrinsicClassBuilder builder("Ljava/io/BufferedOutputStream;");
    builder.Super("Ljava/io/FilterOutputStream;");
    builder.Virtual("<init>", "(Ljava/io/OutputStream;)V", OutputAdoptHandler(context));
    builder.Virtual("<init>", "(Ljava/io/OutputStream;I)V", OutputAdoptHandler(context));
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
