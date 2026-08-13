#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_os_Message(const Context& context) {
    return DeclareAndroidClass(context, "Landroid/os/Message;");
}

}  // namespace ogplay::runtime::android_intrinsics
