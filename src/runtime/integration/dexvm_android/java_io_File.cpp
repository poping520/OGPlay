#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_java_io_File(const Context& context) {
    return DeclareAndroidClass(context, "Ljava/io/File;");
}

}  // namespace ogplay::runtime::android_intrinsics
