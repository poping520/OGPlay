#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_view_inputmethod_InputMethodManager(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/view/inputmethod/InputMethodManager;", "Ljava/lang/Object;");
    builder.FinalMethod("hideSoftInputFromWindow", "(Landroid/os/IBinder;I)Z", TelephonyFalseHandler());
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
