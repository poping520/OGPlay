#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_view_ViewGroup(const Context& context) {
    return DeclareAndroidClass(context, "Landroid/view/ViewGroup;");
}

}  // namespace ogplay::runtime::android_intrinsics
