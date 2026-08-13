#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_java_io_OutputStream(const Context& context) {
    return DeclareAndroidClass(context, "Ljava/io/OutputStream;");
}

}  // namespace ogplay::runtime::android_intrinsics
