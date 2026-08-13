#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_text_EditableImpl(const Context& context) {
    return DeclareAndroidClass(context, "Landroid/text/EditableImpl;");
}

}  // namespace ogplay::runtime::android_intrinsics
