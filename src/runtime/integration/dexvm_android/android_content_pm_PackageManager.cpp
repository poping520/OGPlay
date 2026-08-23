#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_content_pm_PackageManager(const Context&) {
    constexpr std::uint32_t kPublicAbstract = 0x0401U;
    auto builder = dx::IntrinsicClassBuilder::Class(
        "Landroid/content/pm/PackageManager;", "Ljava/lang/Object;", {},
        kPublicAbstract);
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
