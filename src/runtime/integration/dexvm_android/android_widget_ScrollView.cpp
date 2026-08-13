#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_widget_ScrollView(const Context& context) {
    return DeclareAndroidClass(context, "Landroid/widget/ScrollView;");
}

}  // namespace ogplay::runtime::android_intrinsics
