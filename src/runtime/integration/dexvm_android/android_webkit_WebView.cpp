#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_webkit_WebView(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/webkit/WebView;", "Landroid/view/ViewGroup;");
    builder.Constructor("(Landroid/content/Context;)V", ViewInitHandler(context));
    builder.FinalMethod("loadUrl", "(Ljava/lang/String;)V",
        [](dx::IntrinsicContext& call) {
            GuestLog(call, core::LogLevel::warn,
                     "WebView.loadUrl dropped (web content is a non-goal): " +
                         call.vm.StringUtf8(call.arguments[0].ref));
            return dx::VmValue::Void();
        });
    builder.FinalMethod("getSettings", "()Landroid/webkit/WebSettings;",
        [context](dx::IntrinsicContext& call) {
            return dx::VmValue::Ref(
                Singleton(call, context, "web_settings",
                          "Landroid/webkit/WebSettings;"));
        });
    builder.FinalMethod("setWebViewClient", "(Landroid/webkit/WebViewClient;)V", WidgetNoopHandler());
    builder.FinalMethod("addJavascriptInterface", "(Ljava/lang/Object;Ljava/lang/String;)V", WidgetNoopHandler());
    builder.FinalMethod("clearHistory", "()V", WidgetNoopHandler());
    builder.FinalMethod("goBack", "()V", WidgetNoopHandler());
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
