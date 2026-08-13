#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_java_util_zip_ZipEntry(const Context& context) {
    return DeclareAndroidClass(context, "Ljava/util/zip/ZipEntry;");
}

}  // namespace ogplay::runtime::android_intrinsics
