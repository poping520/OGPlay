#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_java_io_ByteArrayInputStream(const Context& context) {
    return DeclareAndroidClass(context, "Ljava/io/ByteArrayInputStream;");
}

}  // namespace ogplay::runtime::android_intrinsics
