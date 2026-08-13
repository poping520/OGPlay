#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_java_io_InputStreamReader(const Context& context) {
    dx::IntrinsicClassBuilder builder("Ljava/io/InputStreamReader;");
    builder.Super("Ljava/io/Reader;");
    builder.Virtual("<init>", "(Ljava/io/InputStream;)V", ReaderAdoptStreamHandler(context));
    builder.Virtual("<init>", "(Ljava/io/InputStream;Ljava/nio/charset/Charset;)V", ReaderAdoptStreamHandler(context));
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
