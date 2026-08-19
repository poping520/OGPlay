#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_java_io_BufferedInputStream(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Ljava/io/BufferedInputStream;", "Ljava/io/FilterInputStream;");
    builder.Constructor("(Ljava/io/InputStream;)V", ReaderAdoptStreamHandler(context));
    builder.Constructor("(Ljava/io/InputStream;I)V", ReaderAdoptStreamHandler(context));
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
