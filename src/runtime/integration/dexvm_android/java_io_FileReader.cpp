#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_java_io_FileReader(const Context& context) {
    dx::IntrinsicClassBuilder builder("Ljava/io/FileReader;");
    builder.Super("Ljava/io/Reader;");
    builder.Virtual("<init>", "(Ljava/lang/String;)V", FileStreamInitPathHandler(context));
    builder.Virtual("<init>", "(Ljava/io/File;)V", FileStreamInitFileHandler(context));
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
