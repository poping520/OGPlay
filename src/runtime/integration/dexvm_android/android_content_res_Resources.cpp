#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_content_res_Resources(const Context& context) {
    const auto handlers = MakeAndroidHandlers(context);
    dx::IntrinsicClassBuilder builder("Landroid/content/res/Resources;");
    builder.Super("Ljava/lang/Object;");
    builder.Virtual("getConfiguration", "()Landroid/content/res/Configuration;", handlers.handler_android_resources_get_configuration);
    builder.Virtual("getIdentifier", "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)I", handlers.handler_android_resources_get_identifier);
    builder.Virtual("openRawResource", "(I)Ljava/io/InputStream;", handlers.handler_android_resources_open_raw_resource);
    builder.Virtual("getString", "(I)Ljava/lang/String;", handlers.handler_android_resources_get_string);
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
