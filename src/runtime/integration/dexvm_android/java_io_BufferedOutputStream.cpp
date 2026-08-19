#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_java_io_BufferedOutputStream(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Ljava/io/BufferedOutputStream;", "Ljava/io/FilterOutputStream;");
    builder.Constructor("(Ljava/io/OutputStream;)V", OutputAdoptHandler(context));
    builder.Constructor("(Ljava/io/OutputStream;I)V", OutputAdoptHandler(context));
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
