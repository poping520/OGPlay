#include "ogplay/runtime/integration/bitmap_pixels.h"
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
#include "ogplay/runtime/integration/bitmap_pixels.h"
#include "ogplay/runtime/integration/host_image_decode.h"
#include "../fixtures/bitmap_decode_data.h"

#include "android_value_fixture.h"

using namespace ogplay::test::android_value;
using namespace ogplay::runtime;
using namespace ogplay::runtime::dexvm;

TEST_CASE("InputMethodManager no-session branches follow UI and window focus and reject active connections") {
    for (const auto backend : {InterpreterBackend::switch_dispatch,
                               InterpreterBackend::threaded}) {
        AndroidValueVm fixture(backend);
        const auto base = fixture.vm.NewIntrinsicInstance(
            "Landroid/content/Context;");
        const auto manager = fixture.On(
            base, "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;",
            {VmValue::Ref(fixture.vm.NewStringUtf8("input_method"))}).ref;
        REQUIRE(manager.IsValid());
        CHECK(fixture.On(
            base, "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;",
            {VmValue::Ref(fixture.vm.NewStringUtf8("input_method"))}).ref == manager);
        const auto view = fixture.New(
            "Landroid/view/View;", "(Landroid/content/Context;)V",
            {VmValue::Ref(base)});
        const auto unrelated = fixture.New(
            "Landroid/view/View;", "(Landroid/content/Context;)V",
            {VmValue::Ref(base)});
        const auto node = FindViewUiNode(*fixture.context, view.Value());
        REQUIRE(node.has_value());
        fixture.On(view, "setFocusable", "(Z)V", {VmValue::Int(1)});
        const auto expect_no_session = [&] {
            REQUIRE_FALSE(fixture.OnOutcome(
                manager, "restartInput", "(Landroid/view/View;)V",
                {VmValue::Ref(view)}).exception.IsValid());
            REQUIRE_FALSE(fixture.OnOutcome(
                manager, "restartInput", "(Landroid/view/View;)V",
                {VmValue::Ref(VmObjectRef{})}).exception.IsValid());
            CHECK(fixture.On(manager, "showSoftInput", "(Landroid/view/View;I)Z",
                             {VmValue::Ref(view), VmValue::Int(0)}).AsInt() == 0);
            CHECK(fixture.On(manager, "hideSoftInputFromWindow",
                             "(Landroid/os/IBinder;I)Z",
                             {VmValue::Ref(VmObjectRef{}), VmValue::Int(0)}).AsInt() == 0);
        };
        // Initial onResume, before the window obtains input focus.
        expect_no_session();
        expect_no_session();
        fixture.context->activity = fixture.vm.NewIntrinsicInstance(
            "Landroid/app/Activity;");
        fixture.context->window_focus_activity.store(fixture.context->activity.Value());
        fixture.context->window_has_focus.store(true);
        // A window alone does not create an input connection.
        expect_no_session();
        fixture.context->ui_tree.Attach(fixture.context->ui_tree.Root(), *node);
        expect_no_session();
        REQUIRE(fixture.On(view, "requestFocus", "()Z").AsInt() == 1);
        const auto expect_gap = [&](const VmCallOutcome& outcome) {
            REQUIRE(outcome.exception.IsValid());
            CHECK(fixture.linker.Class(outcome.exception_class).descriptor ==
                  "Ljava/lang/UnsupportedOperationException;");
            CHECK(outcome.exception_message ==
                  "InputConnection creation for the focused View is unsupported");
        };
        expect_gap(fixture.OnOutcome(manager, "restartInput", "(Landroid/view/View;)V",
                                     {VmValue::Ref(view)}));
        // checkFocus can start input before the caller/proxy/token guard.
        expect_gap(fixture.OnOutcome(manager, "restartInput", "(Landroid/view/View;)V",
                                     {VmValue::Ref(unrelated)}));
        expect_gap(fixture.OnOutcome(manager, "showSoftInput", "(Landroid/view/View;I)Z",
                                     {VmValue::Ref(unrelated), VmValue::Int(0)}));
        expect_gap(fixture.OnOutcome(manager, "hideSoftInputFromWindow",
                                     "(Landroid/os/IBinder;I)Z",
                                     {VmValue::Ref(VmObjectRef{}), VmValue::Int(0)}));
        const auto hits = fixture.ledger.Unimplemented();
        REQUIRE(hits.size() == 1);
        CHECK(hits[0].id == "dexvm.input_method_sessions");
        CHECK(hits[0].count == 4);
        CHECK(fixture.context->ui_tree.Focused() == node);
        // Window focus and ownership use lifecycle facts, not a cached IMM bit.
        fixture.context->window_has_focus.store(false);
        expect_no_session();
        fixture.context->window_has_focus.store(true);
        fixture.context->window_focus_activity.store(unrelated.Value());
        expect_no_session();
        fixture.context->window_focus_activity.store(fixture.context->activity.Value());
        fixture.On(view, "clearFocus", "()V");
        expect_no_session();
        REQUIRE(fixture.On(view, "requestFocus", "()Z").AsInt() == 1);
        fixture.context->ui_tree.Detach(*node);
        expect_no_session();
        fixture.context->ui_tree.Reset();
        expect_no_session();
        CHECK(fixture.ledger.Unimplemented()[0].count == 4);
    }
}

TEST_CASE("View focus and WebView configuration keep real per-instance state") {
    for (const auto backend :
         {InterpreterBackend::switch_dispatch,
          InterpreterBackend::threaded}) {
        std::vector<std::int32_t> focus_events;
        auto listener_class = IntrinsicClassBuilder::Class(
            "Ltest/FocusListener;", "Ljava/lang/Object;",
            {"Landroid/view/View$OnFocusChangeListener;"});
        listener_class.Constructor("()V", [](IntrinsicContext&) {
            return VmValue::Void();
        });
        listener_class.VirtualMethod(
            "onFocusChange", "(Landroid/view/View;Z)V",
            [&focus_events](IntrinsicContext& call) {
                focus_events.push_back(call.arguments[1].AsInt());
                return VmValue::Void();
            });
        AndroidValueVm fixture(
            backend, {std::move(listener_class).Build()});
        const auto activity = fixture.New("Landroid/app/Activity;");
        const auto parent = fixture.New(
            "Landroid/widget/FrameLayout;", "(Landroid/content/Context;)V",
            {VmValue::Ref(activity)});
        const auto first = fixture.New(
            "Landroid/webkit/WebView;", "(Landroid/content/Context;)V",
            {VmValue::Ref(activity)});
        const auto second = fixture.New(
            "Landroid/webkit/WebView;", "(Landroid/content/Context;)V",
            {VmValue::Ref(activity)});
        const auto listener = fixture.New("Ltest/FocusListener;");
        fixture.On(first, "setOnFocusChangeListener",
                   "(Landroid/view/View$OnFocusChangeListener;)V",
                   {VmValue::Ref(listener)});
        fixture.On(second, "setOnFocusChangeListener",
                   "(Landroid/view/View$OnFocusChangeListener;)V",
                   {VmValue::Ref(listener)});
        fixture.On(parent, "addView", "(Landroid/view/View;)V",
                   {VmValue::Ref(first)});
        fixture.On(parent, "addView", "(Landroid/view/View;)V",
                   {VmValue::Ref(second)});

        fixture.On(first, "setVisibility", "(I)V", {VmValue::Int(8)});
        CHECK(fixture.On(first, "requestFocus", "(I)Z",
                         {VmValue::Int(130)}).AsInt() == 0);
        CHECK(fixture.On(first, "hasFocus", "()Z").AsInt() == 0);
        fixture.On(first, "setVisibility", "(I)V", {VmValue::Int(0)});
        CHECK(fixture.On(first, "requestFocus", "(I)Z",
                         {VmValue::Int(130)}).AsInt() == 1);
        CHECK(fixture.On(first, "isFocused", "()Z").AsInt() == 1);
        CHECK(fixture.On(parent, "hasFocus", "()Z").AsInt() == 1);
        CHECK(fixture.On(second, "requestFocus", "()Z").AsInt() == 1);
        CHECK(fixture.On(first, "isFocused", "()Z").AsInt() == 0);
        CHECK(fixture.On(second, "isFocused", "()Z").AsInt() == 1);
        fixture.On(second, "clearFocus", "()V");
        CHECK(fixture.On(parent, "hasFocus", "()Z").AsInt() == 0);
        CHECK(focus_events == std::vector<std::int32_t>{1, 0, 1, 0});
        CHECK(fixture.On(second, "requestFocus", "()Z").AsInt() == 1);
        fixture.On(parent, "removeView", "(Landroid/view/View;)V",
                   {VmValue::Ref(second)});
        CHECK(fixture.On(second, "isFocused", "()Z").AsInt() == 0);
        CHECK(focus_events ==
              std::vector<std::int32_t>{1, 0, 1, 0, 1, 0});

        fixture.On(first, "setScrollContainer", "(Z)V", {VmValue::Int(1)});
        fixture.On(first, "setHorizontalScrollBarEnabled", "(Z)V",
                   {VmValue::Int(0)});
        fixture.On(first, "setVerticalScrollBarEnabled", "(Z)V",
                   {VmValue::Int(0)});
        CHECK(fixture.On(first, "isScrollContainer", "()Z").AsInt() == 1);
        CHECK(fixture.On(first, "isHorizontalScrollBarEnabled", "()Z").AsInt() == 0);
        CHECK(fixture.On(first, "isVerticalScrollBarEnabled", "()Z").AsInt() == 0);
        CHECK(fixture.On(second, "isHorizontalScrollBarEnabled", "()Z").AsInt() == 1);

        const auto settings = fixture.On(
            first, "getSettings", "()Landroid/webkit/WebSettings;").ref;
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
        const auto high = fixture.Static(
            "Landroid/webkit/WebSettings$RenderPriority;", "valueOf",
            "(Ljava/lang/String;)Landroid/webkit/WebSettings$RenderPriority;",
            {VmValue::Ref(high_name)}).ref;
        fixture.On(settings, "setRenderPriority",
                   "(Landroid/webkit/WebSettings$RenderPriority;)V",
                   {VmValue::Ref(high)});

        const auto client = fixture.New("Landroid/webkit/WebViewClient;");
        fixture.On(first, "setWebViewClient", "(Landroid/webkit/WebViewClient;)V",
                   {VmValue::Ref(client)});
        const auto js_object = fixture.New("Ljava/lang/Object;");
        const auto js_name = fixture.vm.NewStringUtf8("bridge");
        fixture.On(first, "addJavascriptInterface",
                   "(Ljava/lang/Object;Ljava/lang/String;)V",
                   {VmValue::Ref(js_object), VmValue::Ref(js_name)});
        const auto roots = fixture.vm.ProtectReferences(std::array{first});
        static_cast<void>(fixture.vm.CollectGarbage("webview-config-roots"));
        CHECK(fixture.model.IsValidRef(client));
        CHECK(fixture.model.IsValidRef(js_object));

        CHECK(fixture.On(client, "shouldOverrideUrlLoading",
                         "(Landroid/webkit/WebView;Ljava/lang/String;)Z",
                         {VmValue::Ref(first), VmValue::Ref(js_name)}).AsInt() == 0);
        fixture.On(client, "onPageFinished",
                   "(Landroid/webkit/WebView;Ljava/lang/String;)V",
                   {VmValue::Ref(first), VmValue::Ref(js_name)});
    }
}

TEST_CASE("DVM-185 WebView.destroy isolates settings and enforces thread rules") {
    constexpr auto kDestroy = "destroy";
    constexpr auto kVoid = "()V";
    constexpr auto kViewInit = "(Landroid/content/Context;)V";
    constexpr auto kGetSettings = "()Landroid/webkit/WebSettings;";
    constexpr auto kLoadUrl = "(Ljava/lang/String;)V";
    for (const auto backend :
         {InterpreterBackend::switch_dispatch,
          InterpreterBackend::threaded}) {
        std::atomic<std::int32_t> worker_runs{};
        std::string worker_exception;
        VmObjectRef worker_view;
        auto subclass = IntrinsicClassBuilder::Class(
            "Ltest/OwnedWebView;", "Landroid/webkit/WebView;");
        subclass.Constructor(
            kViewInit, [](IntrinsicContext& call) {
                const auto type = call.vm.Linker().ResolveDescriptor(
                    "Landroid/webkit/WebView;");
                const auto constructor = call.vm.Linker().FindDirectMethod(
                    type, "<init>", kViewInit);
                const std::array arguments{
                    VmValue::Ref(call.receiver), call.arguments[0]};
                const auto outcome = call.vm.Call(*constructor, arguments);
                if (outcome.exception.IsValid()) {
                    throw VmJavaThrow{
                        call.vm.Linker().Class(outcome.exception_class)
                            .descriptor,
                        outcome.exception_message, outcome.exception};
                }
                return VmValue::Void();
            });
        auto destroyer = IntrinsicClassBuilder::Class(
            "Ltest/DestroyWebView;", "Ljava/lang/Object;",
            {"Ljava/lang/Runnable;"});
        destroyer.Constructor("()V", [](IntrinsicContext&) {
            return VmValue::Void();
        });
        destroyer.VirtualMethod(
            "run", "()V",
            [&worker_runs, &worker_exception, &worker_view](
                IntrinsicContext& call) {
                ++worker_runs;
                const auto klass = call.vm.Model().ObjectClass(worker_view);
                const auto index = call.vm.Linker().FindVtableIndex(
                    klass, kDestroy, kVoid);
                const std::array arguments{VmValue::Ref(worker_view)};
                const auto outcome = call.vm.Call(
                    call.vm.Linker().Class(klass).vtable[*index], arguments);
                if (outcome.exception.IsValid()) {
                    worker_exception =
                        call.vm.Linker().Class(outcome.exception_class)
                            .descriptor;
                } else {
                    worker_exception.clear();
                }
                return VmValue::Void();
            });
        AndroidValueVm fixture(
            backend, {std::move(subclass).Build(), std::move(destroyer).Build()});
        fixture.context->threads = &fixture.threads;
        RegisterAndroidSchedulerStateTable(fixture.vm, fixture.context);

        const auto web_type =
            fixture.linker.ResolveDescriptor("Landroid/webkit/WebView;");
        const auto subclass_type =
            fixture.linker.ResolveDescriptor("Ltest/OwnedWebView;");
        const auto web_slot =
            fixture.linker.FindVtableIndex(web_type, kDestroy, kVoid);
        const auto subclass_slot =
            fixture.linker.FindVtableIndex(subclass_type, kDestroy, kVoid);
        REQUIRE(web_slot.has_value());
        REQUIRE(subclass_slot.has_value());
        CHECK(*web_slot == *subclass_slot);
        const auto& inherited = fixture.linker.Method(
            fixture.linker.Class(subclass_type).vtable[*subclass_slot]);
        CHECK(inherited.owner == web_type);
        CHECK(inherited.overridable);
        CHECK((inherited.access_flags & kAccFinal) == 0);

        fixture.Static("Landroid/os/Looper;", "prepareMainLooper", "()V");
        const auto owner = fixture.New("Landroid/content/Context;");
        const auto first = fixture.New(
            "Ltest/OwnedWebView;", kViewInit, {VmValue::Ref(owner)});
        const auto second = fixture.New(
            "Landroid/webkit/WebView;", kViewInit, {VmValue::Ref(owner)});
        const auto first_settings =
            fixture.On(first, "getSettings", kGetSettings).ref;
        const auto first_again =
            fixture.On(first, "getSettings", kGetSettings).ref;
        const auto second_settings =
            fixture.On(second, "getSettings", kGetSettings).ref;
        CHECK(first_settings.IsValid());
        CHECK(first_settings == first_again);
        CHECK(second_settings.IsValid());
        CHECK(first_settings != second_settings);

        const auto url = fixture.vm.NewStringUtf8("https://example.invalid");
        fixture.context->strict_webview_errors = true;
        auto load = fixture.OnOutcome(first, "loadUrl", kLoadUrl,
                                      {VmValue::Ref(url)});
        REQUIRE(load.exception.IsValid());
        CHECK(fixture.linker.Class(load.exception_class).descriptor ==
              "Ljava/lang/UnsupportedOperationException;");

        fixture.On(first, kDestroy, kVoid);
        fixture.On(first, kDestroy, kVoid);
        auto after = fixture.OnOutcome(first, "getSettings", kGetSettings);
        REQUIRE(after.exception.IsValid());
        CHECK(fixture.linker.Class(after.exception_class).descriptor ==
              "Ljava/lang/IllegalStateException;");
        load = fixture.OnOutcome(first, "loadUrl", kLoadUrl,
                                 {VmValue::Ref(url)});
        REQUIRE(load.exception.IsValid());
        CHECK(fixture.linker.Class(load.exception_class).descriptor ==
              "Ljava/lang/IllegalStateException;");
        CHECK(fixture.On(second, "getSettings", kGetSettings).ref ==
              second_settings);

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
                fixture.linker.ResolveDescriptor(
                    "Landroid/os/HandlerThread;"),
                "<init>", "(Ljava/lang/String;)V");
            REQUIRE(init.has_value());
            const std::array init_args{
                VmValue::Ref(thread), VmValue::Ref(name)};
            auto outcome = fixture.vm.Call(*init, init_args);
            REQUIRE_MESSAGE(!outcome.exception.IsValid(),
                            outcome.exception_message);
            fixture.On(thread, "start", "()V");
            const auto looper =
                fixture.On(thread, "getLooper", "()Landroid/os/Looper;").ref;
            const auto handler = fixture.vm.NewIntrinsicInstance(
                "Landroid/os/Handler;");
            const auto handler_init = fixture.linker.FindDirectMethod(
                fixture.linker.ResolveDescriptor("Landroid/os/Handler;"),
                "<init>", "(Landroid/os/Looper;)V");
            REQUIRE(handler_init.has_value());
            const std::array handler_args{
                VmValue::Ref(handler), VmValue::Ref(looper)};
            outcome = fixture.vm.Call(*handler_init, handler_args);
            REQUIRE_MESSAGE(!outcome.exception.IsValid(),
                            outcome.exception_message);
            const auto runnable = fixture.New("Ltest/DestroyWebView;");
            fixture.On(handler, "post", "(Ljava/lang/Runnable;)Z",
                       {VmValue::Ref(runnable)});
            REQUIRE(wait_for([&] { return worker_runs.load() == 1; }));
            fixture.On(thread, "quit", "()Z");
            fixture.On(thread, "join", "()V");
        };

        fixture.context->target_sdk_version = 19;
        post_destroy(second);
        CHECK(worker_exception == "Ljava/lang/RuntimeException;");
        CHECK(fixture.On(second, "getSettings", kGetSettings).ref ==
              second_settings);

        fixture.context->target_sdk_version = 13;
        post_destroy(second);
        CHECK(worker_exception.empty());
        after = fixture.OnOutcome(second, "getSettings", kGetSettings);
        REQUIRE(after.exception.IsValid());
        CHECK(fixture.linker.Class(after.exception_class).descriptor ==
              "Ljava/lang/IllegalStateException;");

        ShutdownAndroidScheduler(*fixture.context);
    }
}

TEST_CASE("disabled WebView policy reports asynchronous failure and supports cancellation") {
    for (const auto backend :
         {InterpreterBackend::switch_dispatch,
          InterpreterBackend::threaded}) {
        std::int32_t error_count{};
        std::int32_t last_error{};
        std::string last_url;
        auto client_class = IntrinsicClassBuilder::Class(
            "Ltest/DisabledWebClient;", "Landroid/webkit/WebViewClient;");
        client_class.Constructor("()V", [](IntrinsicContext&) {
            return VmValue::Void();
        });
        client_class.OverrideMethod(
            "onReceivedError",
            "(Landroid/webkit/WebView;ILjava/lang/String;Ljava/lang/String;)V",
            [&error_count, &last_error, &last_url](IntrinsicContext& call) {
                ++error_count;
                last_error = call.arguments[1].AsInt();
                last_url = call.vm.StringUtf8(call.arguments[3].ref);
                return VmValue::Void();
            });
        AndroidValueVm fixture(
            backend, {std::move(client_class).Build()});
        fixture.context->threads = &fixture.threads;
        RegisterAndroidSchedulerStateTable(fixture.vm, fixture.context);
        fixture.Static("Landroid/os/Looper;", "prepareMainLooper", "()V");
        const auto activity = fixture.New("Landroid/app/Activity;");
        const auto parent = fixture.New(
            "Landroid/widget/FrameLayout;", "(Landroid/content/Context;)V",
            {VmValue::Ref(activity)});
        const auto view = fixture.New(
            "Landroid/webkit/WebView;", "(Landroid/content/Context;)V",
            {VmValue::Ref(activity)});
        const auto client = fixture.New("Ltest/DisabledWebClient;");
        fixture.On(view, "setWebViewClient",
                   "(Landroid/webkit/WebViewClient;)V",
                   {VmValue::Ref(client)});
        fixture.On(parent, "addView", "(Landroid/view/View;)V",
                   {VmValue::Ref(view)});
        fixture.context->ui_tree.Attach(
            fixture.context->ui_tree.Root(),
            EnsureViewUiNode(*fixture.context, parent,
                             ogplay::runtime::ui::UiClass::View));

        const auto first = fixture.vm.NewStringUtf8(
            "https://cloud.example/path/page?token=secret#fragment");
        fixture.On(view, "loadUrl", "(Ljava/lang/String;)V",
                   {VmValue::Ref(first)});
        CHECK(error_count == 0);
        CHECK_FALSE(PumpJavaThreads(fixture.vm, *fixture.context).has_value());
        CHECK(error_count == 1);
        CHECK(last_error == -10);
        CHECK(last_url ==
              "https://cloud.example/path/page?token=secret#fragment");

        const auto base = fixture.vm.NewStringUtf8("https://data.example/base?x=1");
        const auto html = fixture.vm.NewStringUtf8("<p>offline</p>");
        fixture.On(
            view, "loadDataWithBaseURL",
            "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)V",
            {VmValue::Ref(base), VmValue::Ref(html), VmValue::Ref(VmObjectRef{}),
             VmValue::Ref(VmObjectRef{}), VmValue::Ref(VmObjectRef{})});
        CHECK_FALSE(PumpJavaThreads(fixture.vm, *fixture.context).has_value());
        CHECK(error_count == 2);
        CHECK(last_error == -10);
        CHECK(last_url == "https://data.example/base?x=1");

        const auto second = fixture.vm.NewStringUtf8("https://example/stop");
        fixture.On(view, "loadUrl", "(Ljava/lang/String;)V",
                   {VmValue::Ref(second)});
        fixture.On(view, "stopLoading", "()V");
        CHECK_FALSE(PumpJavaThreads(fixture.vm, *fixture.context).has_value());
        CHECK(error_count == 2);

        const auto script = fixture.vm.NewStringUtf8("javascript:callback(1)");
        fixture.On(view, "loadUrl", "(Ljava/lang/String;)V",
                   {VmValue::Ref(script)});
        CHECK_FALSE(PumpJavaThreads(fixture.vm, *fixture.context).has_value());
        CHECK(error_count == 2);
        CHECK(fixture.On(view, "canGoBack", "()Z").AsInt() == 0);
        fixture.On(view, "goBack", "()V");

        fixture.On(view, "loadUrl", "(Ljava/lang/String;)V",
                   {VmValue::Ref(second)});
        fixture.On(view, "destroy", "()V");
        CHECK_FALSE(PumpJavaThreads(fixture.vm, *fixture.context).has_value());
        CHECK(error_count == 2);

        const auto records = fixture.logger.Snapshot(
            ogplay::core::LogLevel::warn, "runtime.web.disabled");
        REQUIRE(records.size() == 3);
        CHECK(records[0].fields.size() == 4);
        CHECK(std::get<std::string>(records[0].fields[1].value) ==
              "https://cloud.example/path/page?token=secret#fragment");
        CHECK(std::get<std::string>(records[0].fields[2].value) ==
              "cloud.example");
        CHECK(std::get<std::string>(records[0].fields[3].value) ==
              "/path/page");
        CHECK(std::get<std::string>(records[1].fields[0].value) ==
              "load_data");
        CHECK(std::get<std::string>(records[2].fields[0].value) ==
              "javascript");

        const auto strict_view = fixture.New(
            "Landroid/webkit/WebView;", "(Landroid/content/Context;)V",
            {VmValue::Ref(activity)});
        fixture.context->strict_webview_errors = true;
        const auto strict = fixture.OnOutcome(
            strict_view, "loadUrl", "(Ljava/lang/String;)V",
            {VmValue::Ref(second)});
        REQUIRE(strict.exception.IsValid());
        CHECK(fixture.linker.Class(strict.exception_class).descriptor ==
              "Ljava/lang/UnsupportedOperationException;");
        ShutdownAndroidScheduler(*fixture.context);
    }
}
