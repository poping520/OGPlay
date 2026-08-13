#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_view_inputmethod_InputMethodManager(const Context& context) {
    static_cast<void>(context);
    dx::IntrinsicClassBuilder builder("Landroid/view/inputmethod/InputMethodManager;");
    builder.Super("Ljava/lang/Object;");
    builder.Virtual("hideSoftInputFromWindow", "(Landroid/os/IBinder;I)Z", TelephonyFalseHandler());
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
