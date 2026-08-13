#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_java_util_zip_ZipEntry(const Context& context) {
    const auto handlers = MakeAndroidHandlers(context);
    dx::IntrinsicClassBuilder builder("Ljava/util/zip/ZipEntry;");
    builder.Super("Ljava/lang/Object;");
    builder.Field("name", "Ljava/lang/String;", false);
    builder.Virtual("getName", "()Ljava/lang/String;", handlers.handler_android_zip_entry_get_name);
    builder.Virtual("isDirectory", "()Z", handlers.handler_android_zip_entry_is_directory);
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
