#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_view_inputmethod_InputMethodManager(const Context& context) {
    return DeclareAndroidClass(context, "Landroid/view/inputmethod/InputMethodManager;");
}

}  // namespace ogplay::runtime::android_intrinsics
