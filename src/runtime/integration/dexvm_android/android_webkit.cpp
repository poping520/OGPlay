// DVM-80: API-family translation unit. Physical consolidation only.

// ---- migrated from android_webkit_WebChromeClient.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics::dvm80_android_webkit_WebChromeClient {

Decl Declare_android_webkit_WebChromeClient(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/webkit/WebChromeClient;", "Ljava/lang/Object;");
    builder.Constructor("()V", NeutralHandler('V'));
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

namespace ogplay::runtime::android_intrinsics {
Decl Declare_android_webkit_WebChromeClient(const Context& context) {
    return dvm80_android_webkit_WebChromeClient::Declare_android_webkit_WebChromeClient(context);
}
}  // namespace ogplay::runtime::android_intrinsics

// ---- migrated from android_webkit_WebSettings.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics::dvm80_android_webkit_WebSettings {

Decl Declare_android_webkit_WebSettings(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/webkit/WebSettings;", "Ljava/lang/Object;");
    builder.FinalMethod("setJavaScriptEnabled", "(Z)V", WidgetNoopHandler());
    builder.FinalMethod("getUserAgentString", "()Ljava/lang/String;", TelephonyEmptyStringHandler());
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

namespace ogplay::runtime::android_intrinsics {
Decl Declare_android_webkit_WebSettings(const Context& context) {
    return dvm80_android_webkit_WebSettings::Declare_android_webkit_WebSettings(context);
}
}  // namespace ogplay::runtime::android_intrinsics

// ---- migrated from android_webkit_WebView.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics::dvm80_android_webkit_WebView {

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

namespace ogplay::runtime::android_intrinsics {
Decl Declare_android_webkit_WebView(const Context& context) {
    return dvm80_android_webkit_WebView::Declare_android_webkit_WebView(context);
}
}  // namespace ogplay::runtime::android_intrinsics

// ---- migrated from android_webkit_WebViewClient.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics::dvm80_android_webkit_WebViewClient {

Decl Declare_android_webkit_WebViewClient(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/webkit/WebViewClient;", "Ljava/lang/Object;");
    builder.Constructor("()V", NeutralHandler('V'));
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

namespace ogplay::runtime::android_intrinsics {
Decl Declare_android_webkit_WebViewClient(const Context& context) {
    return dvm80_android_webkit_WebViewClient::Declare_android_webkit_WebViewClient(context);
}
}  // namespace ogplay::runtime::android_intrinsics
