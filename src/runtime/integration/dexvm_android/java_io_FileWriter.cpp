#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_java_io_FileWriter(const Context& context) {
    return DeclareAndroidClass(context, "Ljava/io/FileWriter;");
}

}  // namespace ogplay::runtime::android_intrinsics
