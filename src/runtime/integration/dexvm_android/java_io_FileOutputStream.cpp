#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_java_io_FileOutputStream(const Context& context) {
    return DeclareAndroidClass(context, "Ljava/io/FileOutputStream;");
}

}  // namespace ogplay::runtime::android_intrinsics
