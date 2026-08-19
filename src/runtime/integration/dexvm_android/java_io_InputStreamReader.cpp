#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_java_io_InputStreamReader(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Ljava/io/InputStreamReader;", "Ljava/io/Reader;");
    builder.Constructor("(Ljava/io/InputStream;)V", ReaderAdoptStreamHandler(context));
    builder.Constructor("(Ljava/io/InputStream;Ljava/nio/charset/Charset;)V", ReaderAdoptStreamHandler(context));
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
