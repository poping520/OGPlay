#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_java_io_DataOutputStream(const Context& context) {
    return DeclareAndroidClass(context, "Ljava/io/DataOutputStream;");
}

}  // namespace ogplay::runtime::android_intrinsics
