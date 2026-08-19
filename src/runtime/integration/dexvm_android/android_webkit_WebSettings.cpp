#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_webkit_WebSettings(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/webkit/WebSettings;", "Ljava/lang/Object;");
    builder.FinalMethod("setJavaScriptEnabled", "(Z)V", WidgetNoopHandler());
    builder.FinalMethod("getUserAgentString", "()Ljava/lang/String;", TelephonyEmptyStringHandler());
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
