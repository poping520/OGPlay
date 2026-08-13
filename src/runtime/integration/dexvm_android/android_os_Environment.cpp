#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_os_Environment(const Context& context) {
    const auto handlers = MakeAndroidHandlers(context);
    dx::IntrinsicClassBuilder builder("Landroid/os/Environment;");
    builder.Super("Ljava/lang/Object;");
    builder.Static("getExternalStorageDirectory", "()Ljava/io/File;", handlers.handler_android_environment_get_external_storage_dir);
    builder.Static("getExternalStorageState", "()Ljava/lang/String;", handlers.handler_android_environment_get_external_storage_state);
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
