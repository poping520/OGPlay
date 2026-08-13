#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_telephony_SmsMessage(const Context& context) {
    return DeclareAndroidClass(context, "Landroid/telephony/SmsMessage;");
}

}  // namespace ogplay::runtime::android_intrinsics
