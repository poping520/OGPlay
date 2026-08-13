#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_os_IBinder(const Context& context) {
    static_cast<void>(context);
    dx::IntrinsicClassBuilder builder("Landroid/os/IBinder;");
    builder.MarkInterface();
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
