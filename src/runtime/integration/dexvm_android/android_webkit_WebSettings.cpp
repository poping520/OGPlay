#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_webkit_WebSettings(const Context& context) {
    const auto handlers = MakeAndroidHandlers(context);
    dx::IntrinsicClassBuilder builder("Landroid/webkit/WebSettings;");
    builder.Super("Ljava/lang/Object;");
    builder.Virtual("setJavaScriptEnabled", "(Z)V", handlers.handler_android_widget_noop);
    builder.Virtual("getUserAgentString", "()Ljava/lang/String;", handlers.handler_android_telephony_empty_string);
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
