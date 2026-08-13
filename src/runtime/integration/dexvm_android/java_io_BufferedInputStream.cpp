#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_java_io_BufferedInputStream(const Context& context) {
    return DeclareAndroidClass(context, "Ljava/io/BufferedInputStream;");
}

}  // namespace ogplay::runtime::android_intrinsics
