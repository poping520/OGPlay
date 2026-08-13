#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_java_io_FileReader(const Context& context) {
    return DeclareAndroidClass(context, "Ljava/io/FileReader;");
}

}  // namespace ogplay::runtime::android_intrinsics
