#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_os_IBinder(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Interface("Landroid/os/IBinder;");
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
