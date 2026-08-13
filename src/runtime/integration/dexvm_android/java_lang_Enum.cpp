#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_java_lang_Enum(const Context& context) {
    return DeclareAndroidClass(context, "Ljava/lang/Enum;");
}

}  // namespace ogplay::runtime::android_intrinsics
