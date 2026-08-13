#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_java_io_FilterInputStream(const Context& context) {
    dx::IntrinsicClassBuilder builder("Ljava/io/FilterInputStream;");
    builder.Super("Ljava/io/InputStream;");
    builder.Virtual("<init>", "(Ljava/io/InputStream;)V", ReaderAdoptStreamHandler(context));
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
