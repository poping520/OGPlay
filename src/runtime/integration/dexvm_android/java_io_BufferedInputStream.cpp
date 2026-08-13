#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_java_io_BufferedInputStream(const Context& context) {
    dx::IntrinsicClassBuilder builder("Ljava/io/BufferedInputStream;");
    builder.Super("Ljava/io/FilterInputStream;");
    builder.Virtual("<init>", "(Ljava/io/InputStream;)V", ReaderAdoptStreamHandler(context));
    builder.Virtual("<init>", "(Ljava/io/InputStream;I)V", ReaderAdoptStreamHandler(context));
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
