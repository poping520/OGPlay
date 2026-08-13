#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_telephony_SmsManager(const Context& context) {
    return DeclareAndroidClass(context, "Landroid/telephony/SmsManager;");
}

}  // namespace ogplay::runtime::android_intrinsics
