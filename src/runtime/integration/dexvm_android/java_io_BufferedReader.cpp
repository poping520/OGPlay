#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_java_io_BufferedReader(const Context& context) {
    return DeclareAndroidClass(context, "Ljava/io/BufferedReader;");
}

}  // namespace ogplay::runtime::android_intrinsics
