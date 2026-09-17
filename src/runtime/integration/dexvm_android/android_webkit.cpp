// DVM-80: API-family translation unit. Physical consolidation only.

// ---- migrated from android_webkit_WebChromeClient.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_webkit_WebChromeClient(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/webkit/WebChromeClient;", "Ljava/lang/Object;");
    builder.Constructor("()V", NeutralHandler('V'));
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics


// ---- migrated from android_webkit_WebSettings.cpp ----
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


// ---- migrated from android_webkit_WebView.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_webkit_WebView(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/webkit/WebView;", "Landroid/view/ViewGroup;");
    const auto webview_thread = builder.BoundInstanceField(
        "mWebViewThread", "Landroid/os/Looper;", dx::kAccPrivate);
    const auto destroyed = builder.BoundInstanceField(
        "mDestroyed", "Z", dx::kAccPrivate);
    const auto settings = builder.BoundInstanceField(
        "mWebSettings", "Landroid/webkit/WebSettings;", dx::kAccPrivate);
    const auto check_thread = [context, webview_thread](dx::IntrinsicContext& call) {
        const auto expected = dx::IntrinsicCall(call).GetRef(webview_thread);
        if (!expected.IsValid()) return;
        if (CurrentLooper(context, call.vm.CurrentContextToken()) == expected) {
            return;
        }
        // API 18+ (JELLY_BEAN_MR2) throws; older targets only warn in AOSP.
        if (context->target_sdk_version >= 18) {
            throw dx::VmJavaThrow{
                "Ljava/lang/RuntimeException;",
                "A WebView method was called on the wrong thread."};
        }
    };
    const auto require_alive = [destroyed](dx::IntrinsicContext& call) {
        if (dx::IntrinsicCall(call).GetInt(destroyed) != 0) {
            throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;",
                                  "WebView already destroyed"};
        }
    };
    builder.Constructor("(Landroid/content/Context;)V",
        [context, webview_thread](dx::IntrinsicContext& call) {
            const auto result = ViewInitHandler(context)(call);
            dx::IntrinsicCall(call).SetRef(
                webview_thread,
                CurrentLooper(context, call.vm.CurrentContextToken()));
            return result;
        });
    builder.VirtualMethod("destroy", "()V",
        [check_thread, destroyed, settings](dx::IntrinsicContext& call) {
            check_thread(call);
            if (dx::IntrinsicCall(call).GetInt(destroyed) != 0) {
                return dx::VmValue::Void();
            }
            dx::IntrinsicCall(call).SetInt(destroyed, 1);
            dx::IntrinsicCall(call).SetRef(settings, dx::VmObjectRef{});
            return dx::VmValue::Void();
        });
    builder.FinalMethod("loadUrl", "(Ljava/lang/String;)V",
        [check_thread, require_alive](dx::IntrinsicContext& call) -> dx::VmValue {
            check_thread(call);
            require_alive(call);
            throw dx::VmJavaThrow{
                "Ljava/lang/UnsupportedOperationException;",
                "WebView content is outside the bounded UI implementation: " +
                    call.vm.StringUtf8(call.arguments[0].ref)};
        });
    builder.FinalMethod("getSettings", "()Landroid/webkit/WebSettings;",
        [check_thread, require_alive, settings](dx::IntrinsicContext& call) {
            check_thread(call);
            require_alive(call);
            auto current = dx::IntrinsicCall(call).GetRef(settings);
            if (!current.IsValid()) {
                current = call.vm.NewIntrinsicInstance(
                    "Landroid/webkit/WebSettings;");
                dx::IntrinsicCall(call).SetRef(settings, current);
            }
            return dx::VmValue::Ref(current);
        });
    builder.FinalMethod("setWebViewClient", "(Landroid/webkit/WebViewClient;)V",
        [check_thread, require_alive](dx::IntrinsicContext& call) {
            check_thread(call);
            require_alive(call);
            return WidgetNoopHandler()(call);
        });
    builder.FinalMethod("addJavascriptInterface",
                        "(Ljava/lang/Object;Ljava/lang/String;)V",
        [check_thread, require_alive](dx::IntrinsicContext& call) {
            check_thread(call);
            require_alive(call);
            return WidgetNoopHandler()(call);
        });
    builder.FinalMethod("clearHistory", "()V",
        [check_thread, require_alive](dx::IntrinsicContext& call) {
            check_thread(call);
            require_alive(call);
            return WidgetNoopHandler()(call);
        });
    builder.FinalMethod("goBack", "()V",
        [check_thread, require_alive](dx::IntrinsicContext& call) {
            check_thread(call);
            require_alive(call);
            return WidgetNoopHandler()(call);
        });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics


// ---- migrated from android_webkit_WebViewClient.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_webkit_WebViewClient(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/webkit/WebViewClient;", "Ljava/lang/Object;");
    builder.Constructor("()V", NeutralHandler('V'));
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
