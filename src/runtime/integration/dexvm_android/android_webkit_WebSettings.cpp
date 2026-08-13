#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_webkit_WebSettings(const Context& context) {
    static_cast<void>(context);
    dx::IntrinsicClassBuilder builder("Landroid/webkit/WebSettings;");
    builder.Super("Ljava/lang/Object;");
    builder.Virtual("setJavaScriptEnabled", "(Z)V", WidgetNoopHandler());
    builder.Virtual("getUserAgentString", "()Ljava/lang/String;", TelephonyEmptyStringHandler());
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
