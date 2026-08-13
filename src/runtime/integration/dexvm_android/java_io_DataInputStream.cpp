#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_java_io_DataInputStream(const Context& context) {
    return DeclareAndroidClass(context, "Ljava/io/DataInputStream;");
}

}  // namespace ogplay::runtime::android_intrinsics
