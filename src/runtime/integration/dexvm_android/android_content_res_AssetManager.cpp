#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_content_res_AssetManager(const Context& context) {
    const auto handlers = MakeAndroidHandlers(context);
    dx::IntrinsicClassBuilder builder("Landroid/content/res/AssetManager;");
    builder.Super("Ljava/lang/Object;");
    builder.Virtual("open", "(Ljava/lang/String;)Ljava/io/InputStream;", handlers.handler_android_assets_open);
    builder.Virtual("open", "(Ljava/lang/String;I)Ljava/io/InputStream;", handlers.handler_android_assets_open_mode);
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
