#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_java_io_FilterInputStream(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Ljava/io/FilterInputStream;", "Ljava/io/InputStream;");
    builder.Constructor("(Ljava/io/InputStream;)V", ReaderAdoptStreamHandler(context));
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
