#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_webkit_WebView(const Context& context) {
    const auto handlers = MakeAndroidHandlers(context);
    dx::IntrinsicClassBuilder builder("Landroid/webkit/WebView;");
    builder.Super("Landroid/view/ViewGroup;");
    builder.Virtual("<init>", "(Landroid/content/Context;)V", handlers.handler_android_view_init);
    builder.Virtual("loadUrl", "(Ljava/lang/String;)V", handlers.handler_android_webview_load_url);
    builder.Virtual("getSettings", "()Landroid/webkit/WebSettings;", handlers.handler_android_webview_get_settings);
    builder.Virtual("setWebViewClient", "(Landroid/webkit/WebViewClient;)V", handlers.handler_android_widget_noop);
    builder.Virtual("addJavascriptInterface", "(Ljava/lang/Object;Ljava/lang/String;)V", handlers.handler_android_widget_noop);
    builder.Virtual("clearHistory", "()V", handlers.handler_android_widget_noop);
    builder.Virtual("goBack", "()V", handlers.handler_android_widget_noop);
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
