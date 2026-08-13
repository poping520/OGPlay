#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_java_io_Reader(const Context& context) {
    return DeclareAndroidClass(context, "Ljava/io/Reader;");
}

}  // namespace ogplay::runtime::android_intrinsics
