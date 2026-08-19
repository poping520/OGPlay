#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_java_io_FilterOutputStream(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Ljava/io/FilterOutputStream;", "Ljava/io/OutputStream;");
    builder.Constructor("(Ljava/io/OutputStream;)V", OutputAdoptHandler(context));
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
