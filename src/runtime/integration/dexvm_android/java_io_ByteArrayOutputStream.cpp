#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_java_io_ByteArrayOutputStream(const Context& context) {
    return DeclareAndroidClass(context, "Ljava/io/ByteArrayOutputStream;");
}

}  // namespace ogplay::runtime::android_intrinsics
