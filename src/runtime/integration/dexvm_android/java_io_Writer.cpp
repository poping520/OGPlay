#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_java_io_Writer(const Context& context) {
    return DeclareAndroidClass(context, "Ljava/io/Writer;");
}

}  // namespace ogplay::runtime::android_intrinsics
