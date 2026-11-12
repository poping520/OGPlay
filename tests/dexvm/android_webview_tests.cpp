#include "ogplay/runtime/boundary/android_boundary_hle.h"
#include "boot_dex.h"
#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <tuple>
#include <vector>

#include "ogplay/core/capability_ledger.h"
#include "ogplay/core/logger.h"
#include "ogplay/runtime/dexvm/access_flags.h"
#include "ogplay/runtime/dexvm/class_linker.h"
#include "ogplay/runtime/dexvm/class_loader_facade.h"
#include "ogplay/runtime/dexvm/interpreter.h"
#include "ogplay/runtime/dexvm/intrinsic_builder.h"
#include "ogplay/runtime/dexvm/object_model.h"
#include "ogplay/runtime/dexvm/reflection.h"
#include "ogplay/runtime/dexvm/vm_threads.h"
#include "ogplay/runtime/integration/dexvm_android.h"
#include "ogplay/runtime/integration/android_guest_call_session.h"
#include "ogplay/runtime/vfs/vfs.h"
#include "../fixtures/bitmap_decode_data.h"

#include "android_value_fixture.h"

using namespace ogplay::test::android_value;
using namespace ogplay::runtime;
using namespace ogplay::runtime::dexvm;

TEST_CASE("View focus and WebView configuration keep real per-instance state") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        std::vector<std::int32_t> focus_events;
        auto listener_class = IntrinsicClassBuilder::Class("Ltest/FocusListener;", "Ljava/lang/Object;",
                                                           {"Landroid/view/View$OnFocusChangeListener;"});
        listener_class.Constructor("()V", [](IntrinsicContext&) { return VmValue::Void(); });
        listener_class.VirtualMethod("onFocusChange", "(Landroid/view/View;Z)V",
                                     [&focus_events](IntrinsicContext& call) {
                                         focus_events.push_back(call.arguments[1].AsInt());
                                         return VmValue::Void();
                                     });
        std::vector<std::string> web_events;
        auto client_class = IntrinsicClassBuilder::Class("Ltest/WebClient;", "Landroid/webkit/WebViewClient;");
        client_class.VirtualMethod("onPageStarted",
                                   "(Landroid/webkit/WebView;Ljava/lang/String;Landroid/graphics/Bitmap;)V",
                                   [&](IntrinsicContext&) {
                                       web_events.emplace_back("started");
                                       return VmValue::Void();
                                   });
        client_class.VirtualMethod("onReceivedError",
                                   "(Landroid/webkit/WebView;ILjava/lang/String;Ljava/lang/String;)V",
                                   [&](IntrinsicContext& call) {
                                       web_events.push_back("error:" + std::to_string(call.arguments[1].AsInt()));
                                       return VmValue::Void();
                                   });
        client_class.VirtualMethod("onPageFinished", "(Landroid/webkit/WebView;Ljava/lang/String;)V",
                                   [&](IntrinsicContext&) {
                                       web_events.emplace_back("finished");
                                       return VmValue::Void();
                                   });
        auto chrome_class = IntrinsicClassBuilder::Class("Ltest/WebChrome;", "Landroid/webkit/WebChromeClient;");
        chrome_class.VirtualMethod("onProgressChanged", "(Landroid/webkit/WebView;I)V", [&](IntrinsicContext& call) {
            web_events.push_back("progress:" + std::to_string(call.arguments[1].AsInt()));
            return VmValue::Void();
        });
        auto js_callback =
            IntrinsicClassBuilder::Class("Ltest/WebResult;", "Ljava/lang/Object;", {"Landroid/webkit/ValueCallback;"});
        js_callback.Constructor("()V", [](IntrinsicContext&) { return VmValue::Void(); });
        js_callback.VirtualMethod("onReceiveValue", "(Ljava/lang/Object;)V", [&](IntrinsicContext&) {
            web_events.emplace_back("javascript");
            return VmValue::Void();
        });
        AndroidValueVm fixture(backend, {std::move(listener_class).Build(), std::move(client_class).Build(),
                                         std::move(chrome_class).Build(), std::move(js_callback).Build()});
        PrepareAndroidMainLooper(fixture.vm, fixture.context);
        const auto activity = fixture.New("Landroid/app/Activity;");
        const auto parent =
            fixture.New("Landroid/widget/FrameLayout;", "(Landroid/content/Context;)V", {VmValue::Ref(activity)});
        const auto first =
            fixture.New("Landroid/webkit/WebView;", "(Landroid/content/Context;)V", {VmValue::Ref(activity)});
        const auto second =
            fixture.New("Landroid/webkit/WebView;", "(Landroid/content/Context;)V", {VmValue::Ref(activity)});
        const auto listener = fixture.New("Ltest/FocusListener;");
        fixture.On(first, "setOnFocusChangeListener", "(Landroid/view/View$OnFocusChangeListener;)V",
                   {VmValue::Ref(listener)});
        fixture.On(second, "setOnFocusChangeListener", "(Landroid/view/View$OnFocusChangeListener;)V",
                   {VmValue::Ref(listener)});
        fixture.On(parent, "addView", "(Landroid/view/View;)V", {VmValue::Ref(first)});
        fixture.On(parent, "addView", "(Landroid/view/View;)V", {VmValue::Ref(second)});

        fixture.On(first, "setVisibility", "(I)V", {VmValue::Int(8)});
        CHECK(fixture.On(first, "requestFocus", "(I)Z", {VmValue::Int(130)}).AsInt() == 0);
        CHECK(fixture.On(first, "hasFocus", "()Z").AsInt() == 0);
        fixture.On(first, "setVisibility", "(I)V", {VmValue::Int(0)});
        CHECK(fixture.On(first, "requestFocus", "(I)Z", {VmValue::Int(130)}).AsInt() == 1);
        CHECK(fixture.On(first, "isFocused", "()Z").AsInt() == 1);
        CHECK(fixture.On(parent, "hasFocus", "()Z").AsInt() == 1);
        CHECK(fixture.On(second, "requestFocus", "()Z").AsInt() == 1);
        CHECK(fixture.On(first, "isFocused", "()Z").AsInt() == 0);
        CHECK(fixture.On(second, "isFocused", "()Z").AsInt() == 1);
        fixture.On(second, "clearFocus", "()V");
        // ROM clearFocus offers focus back through the real parent chain.
        CHECK(fixture.On(parent, "hasFocus", "()Z").AsInt() == 1);
        CHECK(fixture.On(first, "isFocused", "()Z").AsInt() == 1);
        CHECK(focus_events == std::vector<std::int32_t>{1, 0, 1, 0, 1});
        CHECK(fixture.On(second, "requestFocus", "()Z").AsInt() == 1);
        fixture.On(parent, "removeView", "(Landroid/view/View;)V", {VmValue::Ref(second)});
        CHECK(fixture.On(second, "isFocused", "()Z").AsInt() == 0);
        CHECK(focus_events == std::vector<std::int32_t>{1, 0, 1, 0, 1, 0, 1, 0, 1});

        for (const bool enabled : {false, true, false}) {
            fixture.On(first, "setHorizontalScrollBarEnabled", "(Z)V", {VmValue::Int(enabled)});
            CHECK(fixture.On(first, "isHorizontalScrollBarEnabled", "()Z").AsInt() == enabled);
        }
        fixture.On(first, "setVerticalScrollBarEnabled", "(Z)V", {VmValue::Int(0)});
        CHECK(fixture.On(first, "isVerticalScrollBarEnabled", "()Z").AsInt() == 0);

        const auto settings = fixture.On(first, "getSettings", "()Landroid/webkit/WebSettings;").ref;
        CHECK(fixture.On(settings, "getJavaScriptEnabled", "()Z").AsInt() == 0);
        CHECK(fixture.On(settings, "supportZoom", "()Z").AsInt() == 1);
        CHECK(fixture.On(settings, "getAllowFileAccess", "()Z").AsInt() == 1);
        CHECK(fixture.On(settings, "getCacheMode", "()I").AsInt() == -1);
        fixture.On(settings, "setJavaScriptEnabled", "(Z)V", {VmValue::Int(1)});
        fixture.On(settings, "setSupportZoom", "(Z)V", {VmValue::Int(0)});
        fixture.On(settings, "setAllowFileAccess", "(Z)V", {VmValue::Int(0)});
        fixture.On(settings, "setCacheMode", "(I)V", {VmValue::Int(2)});
        CHECK(fixture.On(settings, "getJavaScriptEnabled", "()Z").AsInt() == 1);
        CHECK(fixture.On(settings, "supportZoom", "()Z").AsInt() == 0);
        CHECK(fixture.On(settings, "getAllowFileAccess", "()Z").AsInt() == 0);
        CHECK(fixture.On(settings, "getCacheMode", "()I").AsInt() == 2);
        const auto high_name = fixture.vm.NewStringUtf8("HIGH");
        const auto high =
            fixture
                .Static("Landroid/webkit/WebSettings$RenderPriority;", "valueOf",
                        "(Ljava/lang/String;)Landroid/webkit/WebSettings$RenderPriority;", {VmValue::Ref(high_name)})
                .ref;
        fixture.On(settings, "setRenderPriority", "(Landroid/webkit/WebSettings$RenderPriority;)V",
                   {VmValue::Ref(high)});

        const auto client = fixture.New("Landroid/webkit/WebViewClient;");
        fixture.On(first, "setWebViewClient", "(Landroid/webkit/WebViewClient;)V", {VmValue::Ref(client)});
        const auto js_object = fixture.New("Ljava/lang/Object;");
        const auto js_name = fixture.vm.NewStringUtf8("bridge");
        fixture.On(first, "addJavascriptInterface", "(Ljava/lang/Object;Ljava/lang/String;)V",
                   {VmValue::Ref(js_object), VmValue::Ref(js_name)});
        const auto roots = fixture.vm.ProtectReferences(std::array{first});
        static_cast<void>(fixture.vm.CollectGarbage("webview-config-roots"));
        CHECK(fixture.model.IsValidRef(client));
        CHECK(fixture.model.IsValidRef(js_object));

        CHECK(fixture
                  .On(client, "shouldOverrideUrlLoading", "(Landroid/webkit/WebView;Ljava/lang/String;)Z",
                      {VmValue::Ref(first), VmValue::Ref(js_name)})
                  .AsInt() == 0);
        fixture.On(client, "onPageFinished", "(Landroid/webkit/WebView;Ljava/lang/String;)V",
                   {VmValue::Ref(first), VmValue::Ref(js_name)});

        // The ROM client methods above are real defaults. Record the provider's queued events.
        const auto recording_client = fixture.vm.NewIntrinsicInstance("Ltest/WebClient;");
        const auto chrome = fixture.vm.NewIntrinsicInstance("Ltest/WebChrome;");
        fixture.On(first, "setWebViewClient", "(Landroid/webkit/WebViewClient;)V", {VmValue::Ref(recording_client)});
        fixture.On(first, "setWebChromeClient", "(Landroid/webkit/WebChromeClient;)V", {VmValue::Ref(chrome)});
        const auto str = [&](const char* value) { return VmValue::Ref(fixture.vm.NewStringUtf8(value)); };
        const auto dispatch = [&] {
            const auto error = DispatchFixtureMessages(fixture.vm, *fixture.context);
            REQUIRE_MESSAGE(!error, error.value_or(""));
        };
        const auto load = [&](const char* url) { fixture.On(first, "loadUrl", "(Ljava/lang/String;)V", {str(url)}); };
        const auto string_result = [&](const char* method) {
            return fixture.vm.StringUtf8(fixture.On(first, method, "()Ljava/lang/String;").ref);
        };

        // Offline Chromium reports DNS failure, while inline data completes with no error.
        load("https://example.invalid/");
        CHECK(web_events.empty());
        dispatch();
        CHECK(web_events == std::vector<std::string>{"started", "error:-2", "progress:100", "finished"});
        web_events.clear();

        fixture.On(first, "loadData", "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)V",
                   {str("<title>Local</title>"), str("text/html"), str("UTF-8")});
        CHECK(web_events.empty());
        dispatch();
        CHECK(web_events == std::vector<std::string>{"started", "progress:100", "finished"});
        CHECK(string_result("getTitle") == "Local");
        const auto warnings = fixture.ledger.Warnings();
        const auto warning_id = std::string(ogplay::core::kUiDegradedWarningPrefix) + "WebView.pageRendering";
        CHECK(std::ranges::any_of(warnings,
                                  [&](const auto& warning) { return warning.id == warning_id && warning.count == 1; }));

        const auto data_url = string_result("getUrl");
        CHECK(data_url.starts_with("data:text/html,"));
        CHECK(fixture.On(first, "canGoBack", "()Z").AsInt() == 1);
        fixture.On(first, "goBack", "()V");
        dispatch();
        CHECK(string_result("getUrl") == "https://example.invalid/");
        CHECK(fixture.On(first, "canGoForward", "()Z").AsInt() == 1);
        fixture.On(first, "goForward", "()V");
        dispatch();
        CHECK(string_result("getUrl") == data_url);
        const auto history = fixture.On(first, "copyBackForwardList", "()Landroid/webkit/WebBackForwardList;").ref;
        CHECK(fixture.On(history, "getCurrentIndex", "()I").AsInt() == 1);
        CHECK(fixture.On(history, "getSize", "()I").AsInt() == 2);

        web_events.clear();
        load("file:///android_asset/missing-webview-test.html");
        dispatch();
        CHECK(web_events == std::vector<std::string>{"started", "error:-14", "progress:100", "finished"});

        web_events.clear();
        fixture.On(first, "loadDataWithBaseURL",
                   "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)V",
                   {str("https://example.invalid/base/"), str("<title>Inline</title>"), str("text/html"), str("UTF-8"),
                    str("https://example.invalid/history")});
        dispatch();
        CHECK(web_events == std::vector<std::string>{"started", "progress:100", "finished"});
        CHECK(string_result("getUrl") == "https://example.invalid/history");
        CHECK(string_result("getTitle") == "Inline");

        web_events.clear();
        const auto callback = fixture.New("Ltest/WebResult;");
        fixture.On(first, "evaluateJavascript", "(Ljava/lang/String;Landroid/webkit/ValueCallback;)V",
                   {str("42"), VmValue::Ref(callback)});
        load("javascript:42");
        dispatch();
        CHECK(web_events.empty());
        CHECK(std::ranges::any_of(fixture.ledger.Unimplemented(), [](const auto& hit) {
            return hit.id == "dexvm.webview_javascript" && hit.count == 2;
        }));

        const auto cookies =
            fixture.Static("Landroid/webkit/CookieManager;", "getInstance", "()Landroid/webkit/CookieManager;").ref;
        CHECK(fixture.On(cookies, "acceptCookie", "()Z").AsInt() == 1);
        fixture.On(cookies, "setCookie", "(Ljava/lang/String;Ljava/lang/String;)V",
                   {str("https://example.invalid/app/index"), str("session=one; Secure; Path=/app")});
        const auto read_cookie = [&](const char* url) {
            return fixture.On(cookies, "getCookie", "(Ljava/lang/String;)Ljava/lang/String;", {str(url)}).ref;
        };
        CHECK(fixture.vm.StringUtf8(read_cookie("https://example.invalid/app/play")) == "session=one");
        CHECK_FALSE(read_cookie("http://example.invalid/app/play").IsValid());
        CHECK_FALSE(read_cookie("https://example.invalid/other").IsValid());
        fixture.On(cookies, "setAcceptCookie", "(Z)V", {VmValue::Int(0)});
        CHECK(fixture.On(cookies, "acceptCookie", "()Z").AsInt() == 0);
        fixture.On(cookies, "removeSessionCookie", "()V");
        CHECK(fixture.On(cookies, "hasCookies", "()Z").AsInt() == 0);
    }
}

TEST_CASE("DVM-185 WebView.destroy isolates settings and enforces thread rules") {
    constexpr auto kDestroy = "destroy";
    constexpr auto kVoid = "()V";
    constexpr auto kViewInit = "(Landroid/content/Context;)V";
    constexpr auto kGetSettings = "()Landroid/webkit/WebSettings;";
    constexpr auto kLoadUrl = "(Ljava/lang/String;)V";
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        std::atomic<std::int32_t> worker_runs{};
        std::string worker_exception;
        VmObjectRef worker_view;
        std::string worker_method = "destroy";
        auto subclass = IntrinsicClassBuilder::Class("Ltest/OwnedWebView;", "Landroid/webkit/WebView;");
        subclass.Constructor(kViewInit, [](IntrinsicContext& call) {
            const auto type = call.vm.Linker().ResolveDescriptor("Landroid/webkit/WebView;");
            const auto constructor = call.vm.Linker().FindDirectMethod(type, "<init>", kViewInit);
            const std::array arguments{VmValue::Ref(call.receiver), call.arguments[0]};
            const auto outcome = call.vm.Call(*constructor, arguments);
            if (outcome.exception.IsValid()) {
                throw VmJavaThrow{call.vm.Linker().Class(outcome.exception_class).descriptor, outcome.exception_message,
                                  outcome.exception};
            }
            return VmValue::Void();
        });
        auto destroyer =
            IntrinsicClassBuilder::Class("Ltest/DestroyWebView;", "Ljava/lang/Object;", {"Ljava/lang/Runnable;"});
        destroyer.Constructor("()V", [](IntrinsicContext&) { return VmValue::Void(); });
        destroyer.VirtualMethod(
            "run", "()V", [&worker_runs, &worker_exception, &worker_view, &worker_method](IntrinsicContext& call) {
                ++worker_runs;
                const auto klass = call.vm.Model().ObjectClass(worker_view);
                const auto signature = worker_method == "setInitialScale" ? "(I)V"
                                       : worker_method == "canGoForward"  ? "()Z"
                                                                          : kVoid;
                const auto index = call.vm.Linker().FindVtableIndex(klass, worker_method, signature);
                std::vector arguments{VmValue::Ref(worker_view)};
                if (worker_method == "setInitialScale") arguments.push_back(VmValue::Int(80));
                const auto outcome = call.vm.Call(call.vm.Linker().Class(klass).vtable[*index], arguments);
                if (outcome.exception.IsValid()) {
                    worker_exception = call.vm.Linker().Class(outcome.exception_class).descriptor;
                } else {
                    worker_exception.clear();
                }
                return VmValue::Void();
            });
        AndroidValueVm fixture(backend, {std::move(subclass).Build(), std::move(destroyer).Build()});
        fixture.context->threads = &fixture.threads;
        fixture.context->target_sdk_version = 19;
        PrepareAndroidMainLooper(fixture.vm, fixture.context);

        const auto web_type = fixture.linker.ResolveDescriptor("Landroid/webkit/WebView;");
        const auto subclass_type = fixture.linker.ResolveDescriptor("Ltest/OwnedWebView;");
        const auto web_slot = fixture.linker.FindVtableIndex(web_type, kDestroy, kVoid);
        const auto subclass_slot = fixture.linker.FindVtableIndex(subclass_type, kDestroy, kVoid);
        REQUIRE(web_slot.has_value());
        REQUIRE(subclass_slot.has_value());
        CHECK(*web_slot == *subclass_slot);
        const auto& inherited = fixture.linker.Method(fixture.linker.Class(subclass_type).vtable[*subclass_slot]);
        CHECK(inherited.owner == web_type);
        CHECK(inherited.overridable);
        CHECK((inherited.access_flags & kAccFinal) == 0);

        const auto owner = fixture.New("Landroid/content/Context;");
        const auto first = fixture.New("Ltest/OwnedWebView;", kViewInit, {VmValue::Ref(owner)});
        const auto second = fixture.New("Landroid/webkit/WebView;", kViewInit, {VmValue::Ref(owner)});
        const auto first_settings = fixture.On(first, "getSettings", kGetSettings).ref;
        const auto first_again = fixture.On(first, "getSettings", kGetSettings).ref;
        const auto second_settings = fixture.On(second, "getSettings", kGetSettings).ref;
        CHECK(first_settings.IsValid());
        CHECK(first_settings == first_again);
        CHECK(second_settings.IsValid());
        CHECK(first_settings != second_settings);

        const auto url = fixture.vm.NewStringUtf8("https://example.invalid");
        fixture.On(first, "loadUrl", kLoadUrl, {VmValue::Ref(url)});
        fixture.On(first, kDestroy, kVoid);
        fixture.On(first, kDestroy, kVoid);
        // AwContents.destroy cancels pending work and leaves its WebSettings adapter accessible.
        fixture.On(first, "loadUrl", kLoadUrl, {VmValue::Ref(url)});
        CHECK_FALSE(DispatchFixtureMessages(fixture.vm, *fixture.context));
        CHECK_FALSE(fixture.On(first, "getUrl", "()Ljava/lang/String;").ref.IsValid());
        CHECK(fixture.On(first, "getSettings", kGetSettings).ref == first_settings);
        CHECK(fixture.On(second, "getSettings", kGetSettings).ref == second_settings);

        const auto wait_for = [](const auto& predicate) {
            for (int attempt = 0; attempt < 2000; ++attempt) {
                if (predicate()) return true;
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            return false;
        };
        const auto post_destroy = [&](VmObjectRef view) {
            worker_runs = 0;
            worker_exception.clear();
            worker_view = view;
            const auto thread = fixture.New("Landroid/os/HandlerThread;");
            const auto name = fixture.vm.NewStringUtf8("webview-worker");
            const auto init = fixture.linker.FindDirectMethod(
                fixture.linker.ResolveDescriptor("Landroid/os/HandlerThread;"), "<init>", "(Ljava/lang/String;)V");
            REQUIRE(init.has_value());
            const std::array init_args{VmValue::Ref(thread), VmValue::Ref(name)};
            auto outcome = fixture.vm.Call(*init, init_args);
            REQUIRE_MESSAGE(!outcome.exception.IsValid(), outcome.exception_message);
            fixture.On(thread, "start", "()V");
            const auto looper = fixture.On(thread, "getLooper", "()Landroid/os/Looper;").ref;
            const auto handler = fixture.vm.NewIntrinsicInstance("Landroid/os/Handler;");
            const auto handler_init = fixture.linker.FindDirectMethod(
                fixture.linker.ResolveDescriptor("Landroid/os/Handler;"), "<init>", "(Landroid/os/Looper;)V");
            REQUIRE(handler_init.has_value());
            const std::array handler_args{VmValue::Ref(handler), VmValue::Ref(looper)};
            outcome = fixture.vm.Call(*handler_init, handler_args);
            REQUIRE_MESSAGE(!outcome.exception.IsValid(), outcome.exception_message);
            const auto runnable = fixture.New("Ltest/DestroyWebView;");
            fixture.On(handler, "post", "(Ljava/lang/Runnable;)Z", {VmValue::Ref(runnable)});
            REQUIRE(wait_for([&] { return worker_runs.load() == 1; }));
            fixture.On(thread, "quit", "()Z");
            fixture.On(thread, "join", "()V");
        };

        post_destroy(second);
        CHECK(worker_exception == "Ljava/lang/RuntimeException;");
        CHECK(fixture.On(second, "getSettings", kGetSettings).ref == second_settings);

        for (const auto name : {"setInitialScale", "canGoForward", "goForward"}) {
            worker_method = name;
            post_destroy(second);
            CHECK(worker_exception == "Ljava/lang/RuntimeException;");
        }
        // The ROM View attachment override delegates without calling WebView.checkThread().
        worker_method = "onAttachedToWindow";
        post_destroy(second);
        CHECK(worker_exception.empty());

        // ROM caches the enforcement flag when constructing a WebView from ApplicationInfo.
        const auto info = fixture.On(owner, "getApplicationInfo", "()Landroid/content/pm/ApplicationInfo;").ref;
        const auto sdk = fixture.linker.FindFieldRecursive(fixture.model.ObjectClass(info), "targetSdkVersion", "I");
        REQUIRE(sdk);
        fixture.model.InstanceSlots(info)[fixture.linker.Field(*sdk).slot].bits = 13;
        const auto legacy = fixture.New("Landroid/webkit/WebView;", kViewInit, {VmValue::Ref(owner)});
        worker_method = "destroy";
        post_destroy(legacy);
        CHECK(worker_exception.empty());
        CHECK(fixture.On(legacy, "canGoForward", "()Z").AsInt() == 0);

        ShutdownAndroidScheduler(*fixture.context);
    }
}

#include "container_traversal_fixture.h"

TEST_CASE("DVM-365 WebView complete constructor configuration and ROM super attachment") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ogplay::test::container_traversal::ContainerVm f(backend);
        PrepareAndroidMainLooper(f.vm, f.context);
        const auto owner = f.New("Landroid/app/Activity;");
        const auto view = f.New("Ltest/WebViewLeaf;", "(Landroid/content/Context;)V", {VmValue::Ref(owner)});
        const auto roots = f.vm.ProtectReferences(std::array{owner, view});
        f.On(view, "setId", "(I)V", {VmValue::Int(123)});
        CHECK(f.On(view, "getId", "()I").AsInt() == 123);
        const auto client = f.New("Landroid/webkit/WebViewClient;");
        f.On(view, "setWebViewClient", "(Landroid/webkit/WebViewClient;)V", {VmValue::Ref(client)});
        const auto settings = f.On(view, "getSettings", "()Landroid/webkit/WebSettings;").ref;
        CHECK(f.On(settings, "getUseWideViewPort", "()Z").AsInt() == 0);
        for (const auto name : {"setJavaScriptEnabled", "setBuiltInZoomControls", "setSupportZoom"})
            f.On(settings, name, "(Z)V", {VmValue::Int(1)});
        f.On(view, "setInitialScale", "(I)V", {VmValue::Int(80)});
        f.On(settings, "setUseWideViewPort", "(Z)V", {VmValue::Int(1)});
        CHECK(f.On(settings, "getJavaScriptEnabled", "()Z").AsInt() == 1);
        CHECK(f.On(settings, "getBuiltInZoomControls", "()Z").AsInt() == 1);
        CHECK(f.On(settings, "supportZoom", "()Z").AsInt() == 1);
        CHECK(f.On(settings, "getUseWideViewPort", "()Z").AsInt() == 1);
        CHECK(f.On(view, "getScale", "()F").AsFloat() == doctest::Approx(0.8f));
        for (int scale : {0, -1, 100}) {
            f.On(view, "setInitialScale", "(I)V", {VmValue::Int(scale)});
            CHECK(f.On(view, "getScale", "()F").AsFloat() == 1.0f);
        }
        f.On(settings, "setUseWideViewPort", "(Z)V", {VmValue::Int(0)});
        CHECK(f.On(settings, "getUseWideViewPort", "()Z").AsInt() == 0);
        CHECK(f.On(view, "canGoForward", "()Z").AsInt() == 0);
        f.On(view, "goForward", "()V");
        CHECK(f.On(view, "canGoBack", "()Z").AsInt() == 0);
        f.On(view, "onAttachedToWindow", "()V");
        CHECK(f.On(view, "attachments", "()I").AsInt() == 1);
        f.On(view, "destroy", "()V");
        CHECK(f.On(view, "canGoForward", "()Z").AsInt() == 0);
        f.On(view, "goForward", "()V");
        f.On(view, "setInitialScale", "(I)V", {VmValue::Int(80)});
        CHECK(f.On(view, "getScale", "()F").AsFloat() == 1.0f);
    }
}
