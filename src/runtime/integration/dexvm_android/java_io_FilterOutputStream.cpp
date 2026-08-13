#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_java_io_FilterOutputStream(const Context& context) {
    return DeclareAndroidClass(context, "Ljava/io/FilterOutputStream;");
}

}  // namespace ogplay::runtime::android_intrinsics
