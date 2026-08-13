#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_java_util_zip_ZipInputStream(const Context& context) {
    return DeclareAndroidClass(context, "Ljava/util/zip/ZipInputStream;");
}

}  // namespace ogplay::runtime::android_intrinsics
