#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_widget_TableLayout(const Context& context) {
    return DeclareAndroidClass(context, "Landroid/widget/TableLayout;");
}

}  // namespace ogplay::runtime::android_intrinsics
