#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_java_io_FilterInputStream(const Context& context) {
    return DeclareAndroidClass(context, "Ljava/io/FilterInputStream;");
}

}  // namespace ogplay::runtime::android_intrinsics
