#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_java_io_ObjectInputStream(const Context& context) {
    return DeclareAndroidClass(context, "Ljava/io/ObjectInputStream;");
}

}  // namespace ogplay::runtime::android_intrinsics
