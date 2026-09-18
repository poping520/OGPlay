// DVM-80: API-family translation unit. Physical consolidation only.

// ---- migrated from android_webkit_WebChromeClient.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_webkit_WebChromeClient(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/webkit/WebChromeClient;", "Ljava/lang/Object;");
    builder.Constructor("()V", NeutralHandler('V'));
    builder.VirtualMethod("onProgressChanged", "(Landroid/webkit/WebView;I)V", NeutralHandler('V'));
    builder.VirtualMethod("onReceivedTitle", "(Landroid/webkit/WebView;Ljava/lang/String;)V", NeutralHandler('V'));
    builder.VirtualMethod("onConsoleMessage", "(Ljava/lang/String;ILjava/lang/String;)V", NeutralHandler('V'));
    builder.VirtualMethod("onConsoleMessage", "(Landroid/webkit/ConsoleMessage;)Z", NeutralHandler('Z'));
    builder.VirtualMethod("onShowCustomView", "(Landroid/view/View;Landroid/webkit/WebChromeClient$CustomViewCallback;)V", NeutralHandler('V'));
    builder.VirtualMethod("onHideCustomView", "()V", NeutralHandler('V'));
    return std::move(builder).Build();
}

Decl Declare_android_webkit_WebChromeClient_CustomViewCallback(
    const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Interface(
        "Landroid/webkit/WebChromeClient$CustomViewCallback;");
    builder.UnimplementedVirtual("onCustomViewHidden", "()V",
                                 dx::kAccPublic | dx::kAccAbstract);
    return std::move(builder).Build();
}

Decl Declare_android_webkit_ConsoleMessage(const Context& context) {
    static_cast<void>(context);
    return dx::IntrinsicClassBuilder::Class(
        "Landroid/webkit/ConsoleMessage;", "Ljava/lang/Object;").Build();
}

Decl Declare_android_webkit_HttpAuthHandler(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class(
        "Landroid/webkit/HttpAuthHandler;", "Ljava/lang/Object;");
    builder.FinalMethod("cancel", "()V", NeutralHandler('V'));
    builder.FinalMethod("proceed", "(Ljava/lang/String;Ljava/lang/String;)V",
                        NeutralHandler('V'));
    builder.FinalMethod("useHttpAuthUsernamePassword", "()Z",
                        NeutralHandler('Z'));
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics


// ---- migrated from android_webkit_WebSettings.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_webkit_WebSettings(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/webkit/WebSettings;", "Ljava/lang/Object;");
    const auto support_zoom = builder.BoundInstanceField("mSupportZoom", "Z", dx::kAccPrivate);
    const auto built_in_zoom = builder.BoundInstanceField("mBuiltInZoomControls", "Z", dx::kAccPrivate);
    const auto allow_file = builder.BoundInstanceField("mAllowFileAccess", "Z", dx::kAccPrivate);
    const auto save_form = builder.BoundInstanceField("mSaveFormData", "Z", dx::kAccPrivate);
    const auto save_password = builder.BoundInstanceField("mSavePassword", "Z", dx::kAccPrivate);
    const auto multiple_windows = builder.BoundInstanceField("mSupportMultipleWindows", "Z", dx::kAccPrivate);
    const auto load_images = builder.BoundInstanceField("mLoadsImagesAutomatically", "Z", dx::kAccPrivate);
    const auto javascript = builder.BoundInstanceField("mJavaScriptEnabled", "Z", dx::kAccPrivate);
    const auto plugins = builder.BoundInstanceField("mPluginsEnabled", "Z", dx::kAccPrivate);
    const auto cache_mode = builder.BoundInstanceField("mCacheMode", "I", dx::kAccPrivate);
    const auto encoding = builder.BoundInstanceField("mDefaultTextEncoding", "Ljava/lang/String;", dx::kAccPrivate);
    const auto user_agent = builder.BoundInstanceField("mUserAgent", "Ljava/lang/String;", dx::kAccPrivate);
    const auto render_priority = builder.BoundInstanceField("mRenderPriority", "Landroid/webkit/WebSettings$RenderPriority;", dx::kAccPrivate);
    const auto boolean_setting = [&builder](const char* setter, const char* getter,
                                            const dx::IntrinsicFieldHandle field,
                                            const bool default_value) {
        builder.FinalMethod(setter, "(Z)V", [field](dx::IntrinsicContext& call) {
            dx::IntrinsicCall(call).SetInt(field, call.arguments[0].AsInt() != 0 ? 2 : 1);
            return dx::VmValue::Void();
        });
        builder.FinalMethod(getter, "()Z", [field, default_value](dx::IntrinsicContext& call) {
            const auto value = dx::IntrinsicCall(call).GetInt(field);
            return dx::VmValue::Int(value == 0 ? (default_value ? 1 : 0)
                                               : (value == 2 ? 1 : 0));
        });
    };
    boolean_setting("setSupportZoom", "supportZoom", support_zoom, true);
    boolean_setting("setBuiltInZoomControls", "getBuiltInZoomControls", built_in_zoom, false);
    boolean_setting("setAllowFileAccess", "getAllowFileAccess", allow_file, true);
    boolean_setting("setSaveFormData", "getSaveFormData", save_form, true);
    boolean_setting("setSavePassword", "getSavePassword", save_password, true);
    boolean_setting("setSupportMultipleWindows", "supportMultipleWindows", multiple_windows, false);
    boolean_setting("setLoadsImagesAutomatically", "getLoadsImagesAutomatically", load_images, true);
    boolean_setting("setJavaScriptEnabled", "getJavaScriptEnabled", javascript, false);
    boolean_setting("setPluginsEnabled", "getPluginsEnabled", plugins, false);
    builder.FinalMethod("setCacheMode", "(I)V", [cache_mode](dx::IntrinsicContext& call) {
        dx::IntrinsicCall(call).SetInt(cache_mode, call.arguments[0].AsInt() + 1);
        return dx::VmValue::Void();
    });
    builder.FinalMethod("getCacheMode", "()I", [cache_mode](dx::IntrinsicContext& call) {
        return dx::VmValue::Int(dx::IntrinsicCall(call).GetInt(cache_mode) - 1);
    });
    const auto string_setting = [&builder](const char* setter, const char* getter,
                                           const dx::IntrinsicFieldHandle field,
                                           const char* fallback) {
        builder.FinalMethod(setter, "(Ljava/lang/String;)V", [field](dx::IntrinsicContext& call) {
            dx::IntrinsicCall(call).SetRef(field, call.arguments[0].ref);
            return dx::VmValue::Void();
        });
        builder.FinalMethod(getter, "()Ljava/lang/String;",
            [field, fallback](dx::IntrinsicContext& call) {
                const auto value = dx::IntrinsicCall(call).GetRef(field);
                return dx::VmValue::Ref(value.IsValid()
                    ? value : call.vm.NewStringUtf8(fallback));
            });
    };
    string_setting("setDefaultTextEncodingName", "getDefaultTextEncodingName",
                   encoding, "Latin-1");
    string_setting("setUserAgentString", "getUserAgentString", user_agent, "");
    builder.FinalMethod("setRenderPriority", "(Landroid/webkit/WebSettings$RenderPriority;)V",
        [render_priority](dx::IntrinsicContext& call) {
            if (!call.arguments[0].ref.IsValid()) {
                throw dx::VmJavaThrow{"Ljava/lang/NullPointerException;", "priority == null"};
            }
            dx::IntrinsicCall(call).SetRef(render_priority, call.arguments[0].ref);
            return dx::VmValue::Void();
        });
    return std::move(builder).Build();
}

Decl Declare_android_webkit_WebSettings_RenderPriority(const Context& context) {
    static_cast<void>(context);
    return dx::IntrinsicEnumBuilder(
        "Landroid/webkit/WebSettings$RenderPriority;",
        {"NORMAL", "HIGH", "LOW"}).Build();
}

}  // namespace ogplay::runtime::android_intrinsics


// ---- migrated from android_webkit_WebView.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_webkit_WebView_DisabledLoadCallback(
    const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class(
        "Landroid/webkit/WebView$DisabledLoadCallback;",
        "Ljava/lang/Object;", {"Ljava/lang/Runnable;"});
    const auto view = builder.BoundInstanceField(
        "mView", "Landroid/webkit/WebView;", dx::kAccPrivate);
    const auto client = builder.BoundInstanceField(
        "mClient", "Landroid/webkit/WebViewClient;", dx::kAccPrivate);
    const auto url = builder.BoundInstanceField(
        "mUrl", "Ljava/lang/String;", dx::kAccPrivate);
    const auto cancelled = builder.BoundInstanceField(
        "mCancelled", "Z", dx::kAccPrivate);
    builder.Constructor(
        "(Landroid/webkit/WebView;Landroid/webkit/WebViewClient;Ljava/lang/String;)V",
        [view, client, url](dx::IntrinsicContext& call) {
            auto self = dx::IntrinsicCall(call);
            self.SetRef(view, call.arguments[0].ref);
            self.SetRef(client, call.arguments[1].ref);
            self.SetRef(url, call.arguments[2].ref);
            return dx::VmValue::Void();
        });
    builder.FinalMethod("cancel", "()V",
        [cancelled](dx::IntrinsicContext& call) {
            dx::IntrinsicCall(call).SetInt(cancelled, 1);
            return dx::VmValue::Void();
        });
    builder.VirtualMethod("run", "()V",
        [view, client, url, cancelled](dx::IntrinsicContext& call) {
            auto self = dx::IntrinsicCall(call);
            if (self.GetInt(cancelled) != 0) return dx::VmValue::Void();
            self.SetInt(cancelled, 1);
            const auto target = self.GetRef(view);
            const auto receiver = self.GetRef(client);
            const auto failing_url = self.GetRef(url);
            if (!target.IsValid() || !receiver.IsValid()) {
                return dx::VmValue::Void();
            }
            const auto description = call.vm.NewStringUtf8(
                "Web content is disabled by OGPlay policy");
            static_cast<void>(CallAndroidMethod(
                call.vm, receiver, "onReceivedError",
                "(Landroid/webkit/WebView;ILjava/lang/String;Ljava/lang/String;)V",
                {dx::VmValue::Ref(target), dx::VmValue::Int(-10),
                 dx::VmValue::Ref(description),
                 dx::VmValue::Ref(failing_url)}));
            return dx::VmValue::Void();
        });
    return std::move(builder).Build();
}

Decl Declare_android_webkit_WebView(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/webkit/WebView;", "Landroid/view/ViewGroup;");
    const auto webview_thread = builder.BoundInstanceField(
        "mWebViewThread", "Landroid/os/Looper;", dx::kAccPrivate);
    const auto destroyed = builder.BoundInstanceField(
        "mDestroyed", "Z", dx::kAccPrivate);
    const auto settings = builder.BoundInstanceField(
        "mWebSettings", "Landroid/webkit/WebSettings;", dx::kAccPrivate);
    const auto view_client = builder.BoundInstanceField(
        "mWebViewClient", "Landroid/webkit/WebViewClient;", dx::kAccPrivate);
    const auto chrome_client = builder.BoundInstanceField(
        "mWebChromeClient", "Landroid/webkit/WebChromeClient;", dx::kAccPrivate);
    const auto javascript_interfaces = builder.BoundInstanceField(
        "mJavascriptInterfaces", "Ljava/util/HashMap;", dx::kAccPrivate);
    const auto pending_load = builder.BoundInstanceField(
        "mPendingDisabledLoad",
        "Landroid/webkit/WebView$DisabledLoadCallback;", dx::kAccPrivate);
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
            const auto node = EnsureViewUiNode(
                *context, call.receiver, ui::UiClass::View);
            auto* state = context->ui_tree.Get(node);
            state->focusable = true;
            state->focusable_in_touch_mode = true;
            dx::IntrinsicCall(call).SetRef(
                webview_thread,
                CurrentLooper(context, call.vm.CurrentContextToken()));
            return result;
        });
    builder.VirtualMethod("destroy", "()V",
        [check_thread, destroyed, settings, view_client, chrome_client,
         javascript_interfaces, pending_load](dx::IntrinsicContext& call) {
            check_thread(call);
            if (dx::IntrinsicCall(call).GetInt(destroyed) != 0) {
                return dx::VmValue::Void();
            }
            dx::IntrinsicCall(call).SetInt(destroyed, 1);
            const auto pending = dx::IntrinsicCall(call).GetRef(pending_load);
            if (pending.IsValid()) {
                static_cast<void>(CallAndroidMethod(
                    call.vm, pending, "cancel", "()V"));
            }
            dx::IntrinsicCall(call).SetRef(pending_load, dx::VmObjectRef{});
            dx::IntrinsicCall(call).SetRef(settings, dx::VmObjectRef{});
            dx::IntrinsicCall(call).SetRef(view_client, dx::VmObjectRef{});
            dx::IntrinsicCall(call).SetRef(chrome_client, dx::VmObjectRef{});
            dx::IntrinsicCall(call).SetRef(javascript_interfaces,
                                           dx::VmObjectRef{});
            return dx::VmValue::Void();
        });
    const auto disabled_load =
        [context, check_thread, require_alive, view_client, pending_load](
            dx::IntrinsicContext& call, const dx::VmObjectRef url,
            const std::string_view action) -> dx::VmValue {
            check_thread(call);
            require_alive(call);
            if (!url.IsValid()) {
                throw dx::VmJavaThrow{"Ljava/lang/NullPointerException;",
                                      "url == null"};
            }
            const auto text = call.vm.StringUtf8(url);
            const auto target = ParseDisabledWebTarget(text);
            if (context->strict_webview_errors) {
                throw dx::VmJavaThrow{
                    "Ljava/lang/UnsupportedOperationException;",
                    "WebView content is outside the bounded UI implementation: " +
                        text};
            }
            LogDisabledWeb(call.vm, target.javascript ? "javascript" : action,
                           target);
            if (target.javascript) return dx::VmValue::Void();
            const auto previous =
                dx::IntrinsicCall(call).GetRef(pending_load);
            if (previous.IsValid()) {
                static_cast<void>(CallAndroidMethod(
                    call.vm, previous, "cancel", "()V"));
            }
            const auto callback = call.vm.NewIntrinsicInstance(
                "Landroid/webkit/WebView$DisabledLoadCallback;");
            const auto callback_type = call.vm.Linker().ResolveDescriptor(
                "Landroid/webkit/WebView$DisabledLoadCallback;");
            const auto init = call.vm.Linker().FindDirectMethod(
                callback_type, "<init>",
                "(Landroid/webkit/WebView;Landroid/webkit/WebViewClient;Ljava/lang/String;)V");
            const std::array arguments{
                dx::VmValue::Ref(callback), dx::VmValue::Ref(call.receiver),
                dx::VmValue::Ref(dx::IntrinsicCall(call).GetRef(view_client)),
                dx::VmValue::Ref(url)};
            const auto initialized = call.vm.Call(*init, arguments);
            if (initialized.exception.IsValid()) {
                call.vm.SetPendingException(initialized.exception);
                return dx::VmValue::Void();
            }
            dx::IntrinsicCall(call).SetRef(pending_load, callback);
            static_cast<void>(PostViewRunnable(
                call, context, call.receiver, callback, 0));
            return dx::VmValue::Void();
        };
    builder.FinalMethod("loadUrl", "(Ljava/lang/String;)V",
        [disabled_load](dx::IntrinsicContext& call) {
            return disabled_load(call, call.arguments[0].ref, "load");
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
        [check_thread, require_alive, view_client](dx::IntrinsicContext& call) {
            check_thread(call);
            require_alive(call);
            dx::IntrinsicCall(call).SetRef(view_client, call.arguments[0].ref);
            return dx::VmValue::Void();
        });
    builder.FinalMethod("setWebChromeClient", "(Landroid/webkit/WebChromeClient;)V",
        [check_thread, require_alive, chrome_client](dx::IntrinsicContext& call) {
            check_thread(call);
            require_alive(call);
            dx::IntrinsicCall(call).SetRef(chrome_client, call.arguments[0].ref);
            return dx::VmValue::Void();
        });
    builder.FinalMethod("addJavascriptInterface",
                        "(Ljava/lang/Object;Ljava/lang/String;)V",
        [check_thread, require_alive, javascript_interfaces](dx::IntrinsicContext& call) {
            check_thread(call);
            require_alive(call);
            auto map = dx::IntrinsicCall(call).GetRef(javascript_interfaces);
            if (!map.IsValid()) {
                map = call.vm.NewIntrinsicInstance("Ljava/util/HashMap;");
                const auto type = call.vm.Linker().ResolveDescriptor("Ljava/util/HashMap;");
                const auto init = call.vm.Linker().FindDirectMethod(type, "<init>", "()V");
                const std::array init_arguments{dx::VmValue::Ref(map)};
                const auto initialized = call.vm.Call(*init, init_arguments);
                if (initialized.exception.IsValid()) {
                    call.vm.SetPendingException(initialized.exception);
                    return dx::VmValue::Void();
                }
                dx::IntrinsicCall(call).SetRef(javascript_interfaces, map);
            }
            static_cast<void>(CallAndroidMethod(
                call.vm, map, "put",
                "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;",
                {dx::VmValue::Ref(call.arguments[1].ref),
                 dx::VmValue::Ref(call.arguments[0].ref)}));
            return dx::VmValue::Void();
        });
    builder.FinalMethod("clearHistory", "()V",
        [check_thread, require_alive](dx::IntrinsicContext& call) {
            check_thread(call);
            require_alive(call);
            return WidgetNoopHandler()(call);
        });
    const auto no_history =
        [check_thread, require_alive](dx::IntrinsicContext& call) {
            check_thread(call);
            require_alive(call);
            return dx::VmValue::Void();
        };
    builder.FinalMethod("goBack", "()V", no_history);
    builder.FinalMethod("reload", "()V", no_history);
    builder.FinalMethod("stopLoading", "()V",
        [check_thread, require_alive, pending_load](dx::IntrinsicContext& call) {
            check_thread(call);
            require_alive(call);
            const auto pending = dx::IntrinsicCall(call).GetRef(pending_load);
            if (pending.IsValid()) {
                static_cast<void>(CallAndroidMethod(
                    call.vm, pending, "cancel", "()V"));
                dx::IntrinsicCall(call).SetRef(pending_load,
                                                   dx::VmObjectRef{});
            }
            return dx::VmValue::Void();
        });
    builder.FinalMethod("loadDataWithBaseURL",
        "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)V",
        [disabled_load](dx::IntrinsicContext& call) {
            auto url = call.arguments[0].ref;
            if (!url.IsValid()) url = call.vm.NewStringUtf8("about:blank");
            return disabled_load(call, url, "load_data");
        });
    builder.FinalMethod("canGoBack", "()Z",
        [check_thread, require_alive](dx::IntrinsicContext& call) {
            check_thread(call);
            require_alive(call);
            return dx::VmValue::Int(0);
        });
    builder.FinalMethod("getTitle", "()Ljava/lang/String;",
        [check_thread, require_alive](dx::IntrinsicContext& call) {
            check_thread(call);
            require_alive(call);
            return dx::VmValue::Ref(dx::VmObjectRef{});
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
    builder.VirtualMethod("shouldOverrideUrlLoading", "(Landroid/webkit/WebView;Ljava/lang/String;)Z", NeutralHandler('Z'));
    builder.VirtualMethod("onPageStarted", "(Landroid/webkit/WebView;Ljava/lang/String;Landroid/graphics/Bitmap;)V", NeutralHandler('V'));
    builder.VirtualMethod("onPageFinished", "(Landroid/webkit/WebView;Ljava/lang/String;)V", NeutralHandler('V'));
    builder.VirtualMethod("onLoadResource", "(Landroid/webkit/WebView;Ljava/lang/String;)V", NeutralHandler('V'));
    builder.VirtualMethod("onReceivedError", "(Landroid/webkit/WebView;ILjava/lang/String;Ljava/lang/String;)V", NeutralHandler('V'));
    builder.VirtualMethod("doUpdateVisitedHistory", "(Landroid/webkit/WebView;Ljava/lang/String;Z)V", NeutralHandler('V'));
    builder.VirtualMethod("onScaleChanged", "(Landroid/webkit/WebView;FF)V", NeutralHandler('V'));
    builder.VirtualMethod("onUnhandledKeyEvent", "(Landroid/webkit/WebView;Landroid/view/KeyEvent;)V", NeutralHandler('V'));
    builder.VirtualMethod("shouldOverrideKeyEvent", "(Landroid/webkit/WebView;Landroid/view/KeyEvent;)Z", NeutralHandler('Z'));
    builder.VirtualMethod("onReceivedHttpAuthRequest",
        "(Landroid/webkit/WebView;Landroid/webkit/HttpAuthHandler;Ljava/lang/String;Ljava/lang/String;)V",
        [](dx::IntrinsicContext& call) {
            if (call.arguments[1].ref.IsValid()) {
                static_cast<void>(CallAndroidMethod(
                    call.vm, call.arguments[1].ref, "cancel", "()V"));
            }
            return dx::VmValue::Void();
        });
    const auto send_message = [](const std::size_t index) {
        return dx::IntrinsicHandler([index](dx::IntrinsicContext& call) {
            if (call.arguments[index].ref.IsValid()) {
                static_cast<void>(CallAndroidMethod(
                    call.vm, call.arguments[index].ref, "sendToTarget", "()V"));
            }
            return dx::VmValue::Void();
        });
    };
    builder.VirtualMethod("onFormResubmission",
        "(Landroid/webkit/WebView;Landroid/os/Message;Landroid/os/Message;)V",
        send_message(2));
    builder.VirtualMethod("onTooManyRedirects",
        "(Landroid/webkit/WebView;Landroid/os/Message;Landroid/os/Message;)V",
        send_message(1));
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
