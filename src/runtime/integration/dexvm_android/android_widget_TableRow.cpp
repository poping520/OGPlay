#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_widget_TableRow(const Context& context) {
    return DeclareAndroidClass(context, "Landroid/widget/TableRow;");
}

}  // namespace ogplay::runtime::android_intrinsics
