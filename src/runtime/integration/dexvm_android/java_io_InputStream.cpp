#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_java_io_InputStream(const Context& context) {
    return DeclareAndroidClass(context, "Ljava/io/InputStream;");
}

}  // namespace ogplay::runtime::android_intrinsics
