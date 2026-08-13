#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_telephony_TelephonyManager(const Context& context) {
    return DeclareAndroidClass(context, "Landroid/telephony/TelephonyManager;");
}

}  // namespace ogplay::runtime::android_intrinsics
