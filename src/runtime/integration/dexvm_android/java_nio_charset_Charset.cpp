#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_java_nio_charset_Charset(const Context& context) {
    return DeclareAndroidClass(context, "Ljava/nio/charset/Charset;");
}

}  // namespace ogplay::runtime::android_intrinsics
