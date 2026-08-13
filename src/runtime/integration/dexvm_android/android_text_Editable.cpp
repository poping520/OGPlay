#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_text_Editable(const Context& context) {
    return DeclareAndroidClass(context, "Landroid/text/Editable;");
}

}  // namespace ogplay::runtime::android_intrinsics
