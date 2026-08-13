#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_os_AsyncTask(const Context& context) {
    return DeclareAndroidClass(context, "Landroid/os/AsyncTask;");
}

}  // namespace ogplay::runtime::android_intrinsics
