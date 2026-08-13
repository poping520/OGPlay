#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_telephony_PhoneStateListener(const Context& context) {
    return DeclareAndroidClass(context, "Landroid/telephony/PhoneStateListener;");
}

}  // namespace ogplay::runtime::android_intrinsics
