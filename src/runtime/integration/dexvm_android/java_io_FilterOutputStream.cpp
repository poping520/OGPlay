#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_java_io_FilterOutputStream(const Context& context) {
    dx::IntrinsicClassBuilder builder("Ljava/io/FilterOutputStream;");
    builder.Super("Ljava/io/OutputStream;");
    builder.Virtual("<init>", "(Ljava/io/OutputStream;)V", OutputAdoptHandler(context));
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
