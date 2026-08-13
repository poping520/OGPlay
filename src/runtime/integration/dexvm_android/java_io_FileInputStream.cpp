#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_java_io_FileInputStream(const Context& context) {
    return DeclareAndroidClass(context, "Ljava/io/FileInputStream;");
}

}  // namespace ogplay::runtime::android_intrinsics
