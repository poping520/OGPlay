#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_java_io_BufferedOutputStream(const Context& context) {
    return DeclareAndroidClass(context, "Ljava/io/BufferedOutputStream;");
}

}  // namespace ogplay::runtime::android_intrinsics
