#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_java_io_FileInputStream(const Context& context) {
    dx::IntrinsicClassBuilder builder("Ljava/io/FileInputStream;");
    builder.Super("Ljava/io/InputStream;");
    builder.Virtual("<init>", "(Ljava/io/File;)V", FileStreamInitFileHandler(context));
    builder.Virtual("<init>", "(Ljava/lang/String;)V", FileStreamInitPathHandler(context));
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
