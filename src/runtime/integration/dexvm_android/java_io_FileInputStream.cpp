#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_java_io_FileInputStream(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Ljava/io/FileInputStream;", "Ljava/io/InputStream;");
    builder.Constructor("(Ljava/io/File;)V", FileStreamInitFileHandler(context));
    builder.Constructor("(Ljava/lang/String;)V", FileStreamInitPathHandler(context));
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
