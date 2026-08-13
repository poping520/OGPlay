#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_java_io_InputStreamReader(const Context& context) {
    return DeclareAndroidClass(context, "Ljava/io/InputStreamReader;");
}

}  // namespace ogplay::runtime::android_intrinsics
