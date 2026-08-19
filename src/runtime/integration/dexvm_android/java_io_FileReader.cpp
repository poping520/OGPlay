#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_java_io_FileReader(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Ljava/io/FileReader;", "Ljava/io/Reader;");
    builder.Constructor("(Ljava/lang/String;)V", FileStreamInitPathHandler(context));
    builder.Constructor("(Ljava/io/File;)V", FileStreamInitFileHandler(context));
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
