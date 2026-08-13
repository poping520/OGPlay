#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_text_TextWatcher(const Context& context) {
    return DeclareAndroidClass(context, "Landroid/text/TextWatcher;");
}

}  // namespace ogplay::runtime::android_intrinsics
