#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_webkit_WebView(const Context& context) {
    dx::IntrinsicClassBuilder builder("Landroid/webkit/WebView;");
    builder.Super("Landroid/view/ViewGroup;");
    builder.Virtual("<init>", "(Landroid/content/Context;)V", ViewInitHandler(context));
    builder.Virtual("loadUrl", "(Ljava/lang/String;)V",
        [](dx::IntrinsicContext& call) {
            GuestLog(call, core::LogLevel::warn,
                     "WebView.loadUrl dropped (web content is a non-goal): " +
                         call.vm.StringUtf8(call.arguments[0].ref));
            return dx::VmValue::Void();
        });
    builder.Virtual("getSettings", "()Landroid/webkit/WebSettings;",
        [context](dx::IntrinsicContext& call) {
            return dx::VmValue::Ref(
                Singleton(call, context, "web_settings",
                          "Landroid/webkit/WebSettings;"));
        });
    builder.Virtual("setWebViewClient", "(Landroid/webkit/WebViewClient;)V", WidgetNoopHandler());
    builder.Virtual("addJavascriptInterface", "(Ljava/lang/Object;Ljava/lang/String;)V", WidgetNoopHandler());
    builder.Virtual("clearHistory", "()V", WidgetNoopHandler());
    builder.Virtual("goBack", "()V", WidgetNoopHandler());
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
