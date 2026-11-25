#include <doctest/doctest.h>
#include <algorithm>

#include "android_value_fixture.h"
#include "runtime/integration/dexvm_android/rom_activity.h"
#include "runtime/integration/dexvm_android/rom_window.h"

using namespace ogplay::runtime;
using namespace ogplay::runtime::dexvm;
using namespace ogplay::runtime::android_intrinsics;
using namespace ogplay::test::android_value;
using V = VmValue;

namespace {
const auto null = V::Ref(VmObjectRef{});
constexpr auto start_signature =
    "(Landroid/app/IApplicationThread;Ljava/lang/String;Landroid/content/Intent;Ljava/lang/String;Landroid/os/IBinder;Ljava/lang/String;IILjava/lang/String;Landroid/os/ParcelFileDescriptor;Landroid/os/Bundle;)I";

struct Trace {
    std::vector<std::string> calls;
};

IntrinsicClassDecl ThreadProbe(const std::shared_ptr<Trace>& trace) {
    auto b = IntrinsicClassBuilder::Class("Lprobe/AppThread;", "Landroid/app/ApplicationThreadNative;");
    b.Constructor("()V", [](IntrinsicContext&) { return V::Void(); });
    for (
        const auto& [name, sig] : std::array{
            std::pair{
                "scheduleLaunchActivity",
                "(Landroid/content/Intent;Landroid/os/IBinder;ILandroid/content/pm/ActivityInfo;Landroid/content/res/Configuration;Landroid/content/res/CompatibilityInfo;ILandroid/os/Bundle;Ljava/util/List;Ljava/util/List;ZZLjava/lang/String;Landroid/os/ParcelFileDescriptor;Z)V"},
            std::pair{"schedulePauseActivity", "(Landroid/os/IBinder;ZZI)V"},
            std::pair{"scheduleStopActivity", "(Landroid/os/IBinder;ZI)V"},
            std::pair{"scheduleResumeActivity", "(Landroid/os/IBinder;IZ)V"},
            std::pair{"scheduleDestroyActivity", "(Landroid/os/IBinder;ZI)V"},
            std::pair{"scheduleNewIntent", "(Ljava/util/List;Landroid/os/IBinder;)V"},
            std::pair{"scheduleSendResult", "(Landroid/os/IBinder;Ljava/util/List;)V"}})
        b.VirtualMethod(name, sig, [trace, name](IntrinsicContext& c) {
            if (std::string_view(name) == "scheduleLaunchActivity") CHECK(c.arguments[5].ref.IsValid());
            trace->calls.emplace_back(name);
            return V::Void();
        });
    return std::move(b).Build();
}

struct Fixture {
    std::shared_ptr<Trace> trace = std::make_shared<Trace>();
    AndroidValueVm f{InterpreterBackend::threaded, {ThreadProbe(trace)}, false, nullptr};
    VmObjectRef am{}, thread{};

    Fixture() {
        f.context->activity_inventory_known = true;
        for (int mode = 0; mode < 4; ++mode) {
            ogplay::loader::AndroidManifestActivityComponent c;
            c.name = "probe.Activity" + std::to_string(mode);
            c.launch_mode = static_cast<unsigned>(mode);
            // Match the fixture display; orientation dispatch has APK coverage.
            c.screen_orientation = 0;
            f.context->activity_components.push_back(std::move(c));
        }
        am = f.Static("Landroid/os/ServiceManager;", "getService", "(Ljava/lang/String;)Landroid/os/IBinder;",
                      {V::Ref(f.vm.NewStringUtf8("activity"))})
                 .ref;
        thread = f.New("Lprobe/AppThread;");
        SetRomApplicationThread(f.context, thread);
    }

    void Start(int mode = 0, int request = -1) {
        auto intent = f.New("Landroid/content/Intent;");
        const auto root = f.vm.ProtectReferences(std::array{intent});
        auto component = f.New("Landroid/content/ComponentName;", "(Ljava/lang/String;Ljava/lang/String;)V",
                               {V::Ref(f.vm.NewStringUtf8(f.context->package_name)),
                                V::Ref(f.vm.NewStringUtf8("probe.Activity" + std::to_string(mode)))});
        f.On(intent, "setComponent", "(Landroid/content/ComponentName;)Landroid/content/Intent;", {V::Ref(component)});
        const auto caller =
            f.context->rom_activities->stack.empty() ? VmObjectRef{} : f.context->rom_activities->stack.back().token;
        f.On(am, "startActivity", start_signature,
             {V::Ref(thread), V::Ref(f.vm.NewStringUtf8(f.context->package_name)), V::Ref(intent), null, V::Ref(caller),
              null, V::Int(request), V::Int(0), null, null, null});
    }

    VmObjectRef Top() { return f.context->rom_activities->stack.back().token; }

    void Resumed() { f.On(am, "activityResumed", "(Landroid/os/IBinder;)V", {V::Ref(Top())}); }

    void Paused(VmObjectRef token) { f.On(am, "activityPaused", "(Landroid/os/IBinder;)V", {V::Ref(token)}); }
};
}  // namespace

TEST_CASE("P3 AMS launch for-result finish and return use IApplicationThread scheduling") {
    Fixture x;
    x.Start();
    REQUIRE(x.trace->calls == std::vector<std::string>{"scheduleLaunchActivity"});
    x.Resumed();
    const auto caller = x.Top();
    x.Start(0, 7);
    CHECK(x.trace->calls.back() == "schedulePauseActivity");
    x.Paused(caller);
    CHECK(x.trace->calls.back() == "scheduleLaunchActivity");
    const auto child = x.Top();
    CHECK(child != caller);
    x.Resumed();
    CHECK(x.f.On(x.am, "getTaskForActivity", "(Landroid/os/IBinder;Z)I", {V::Ref(caller), V::Int(1)}).AsInt() == 1);
    const auto tasks =
        x.f.On(x.am, "getTasks", "(IILandroid/app/IThumbnailReceiver;)Ljava/util/List;", {V::Int(10), V::Int(0), null})
            .ref;
    REQUIRE(x.f.On(tasks, "size", "()I").AsInt() == 1);
    const auto task = x.f.On(tasks, "get", "(I)Ljava/lang/Object;", {V::Int(0)}).ref;
    CHECK(WindowField(x.f.vm, task, "numActivities", "I").bits == 2);
    x.f.On(x.am, "finishActivity", "(Landroid/os/IBinder;ILandroid/content/Intent;)Z",
           {V::Ref(child), V::Int(-1), null});
    x.Paused(child);
    REQUIRE(x.trace->calls.size() >= 3);
    CHECK(x.trace->calls[x.trace->calls.size() - 2] == "scheduleSendResult");
    CHECK(x.trace->calls.back() == "scheduleDestroyActivity");
    x.f.On(x.am, "activityDestroyed", "(Landroid/os/IBinder;)V", {V::Ref(child)});
    CHECK(x.Top() == caller);
    CHECK(x.trace->calls.back() == "scheduleResumeActivity");
}

TEST_CASE("P3 AMS suspend and resume preserve the Activity until it finishes") {
    Fixture x;
    x.Start();
    x.Resumed();
    auto token = x.Top();
    x.trace->calls.clear();
    RequestRomActivitySuspend(x.f.vm, x.f.context, true);
    REQUIRE(x.trace->calls == std::vector<std::string>{"schedulePauseActivity"});
    x.Paused(token);
    CHECK(x.trace->calls == std::vector<std::string>{"schedulePauseActivity", "scheduleStopActivity"});
    RequestRomActivitySuspend(x.f.vm, x.f.context, false);
    CHECK(x.trace->calls.back() == "scheduleResumeActivity");
    x.Resumed();
    x.trace->calls.clear();
    CHECK(x.f.On(x.am, "finishActivity", "(Landroid/os/IBinder;ILandroid/content/Intent;)Z",
                 {V::Ref(token), V::Int(0), null})
              .AsInt() == 1);
    x.Paused(token);
    CHECK(x.trace->calls ==
          std::vector<std::string>{"schedulePauseActivity", "scheduleStopActivity", "scheduleDestroyActivity"});
    CHECK(x.f.On(x.am, "getTaskForActivity", "(Landroid/os/IBinder;Z)I", {V::Ref(token), V::Int(1)}).AsInt() == -1);
    x.f.On(x.am, "activityDestroyed", "(Landroid/os/IBinder;)V", {V::Ref(token)});
    CHECK(x.f.context->rom_activities->exit_requested);
}

TEST_CASE("P3 AMS launch modes retain tokens and isolated tasks") {
    Fixture x;
    x.Start(1);
    x.Resumed();
    auto one = x.Top();
    x.Start(1);
    x.Paused(one);
    CHECK(x.Top() == one);
    CHECK(std::ranges::find(x.trace->calls, "scheduleNewIntent") != x.trace->calls.end());
    x.Resumed();
    x.Start(2);
    x.Paused(one);
    auto task = x.Top();
    x.Resumed();
    x.Start(0);
    x.Paused(task);
    auto ordinary = x.Top();
    CHECK(ordinary != task);
    x.Resumed();
    x.Start(2);
    x.Paused(ordinary);
    CHECK(x.Top() == task);
    x.Resumed();
    x.Start(3);
    x.Paused(task);
    auto isolated = x.Top();
    x.Resumed();
    CHECK(x.f.On(x.am, "getTaskForActivity", "(Landroid/os/IBinder;Z)I", {V::Ref(isolated), V::Int(0)}).AsInt() != 1);
    CHECK(x.f.On(x.am, "getRequestedOrientation", "(Landroid/os/IBinder;)I", {V::Ref(isolated)}).AsInt() == 0);
}

TEST_CASE("P3 AMS singleTop reuses the Activity below a finishing caller") {
    Fixture x;
    x.Start(1);
    x.Resumed();
    const auto game = x.Top();
    x.Start(0);
    x.Paused(game);
    const auto video = x.Top();
    REQUIRE(video != game);
    x.Resumed();
    x.trace->calls.clear();
    // finish() then startActivity() before the pause completes.
    x.f.On(x.am, "finishActivity", "(Landroid/os/IBinder;ILandroid/content/Intent;)Z",
           {V::Ref(video), V::Int(0), null});
    x.Start(1);
    x.Paused(video);
    const auto& stack = x.f.context->rom_activities->stack;
    CHECK(std::ranges::find(x.trace->calls, "scheduleLaunchActivity") == x.trace->calls.end());
    const auto intent = std::ranges::find(x.trace->calls, "scheduleNewIntent");
    REQUIRE(intent != x.trace->calls.end());
    CHECK(std::find(intent, x.trace->calls.end(), "scheduleResumeActivity") != x.trace->calls.end());
    CHECK(x.Top() == game);
    CHECK(stack.size() == 2);
    x.Resumed();
    x.f.On(x.am, "activityDestroyed", "(Landroid/os/IBinder;)V", {V::Ref(video)});
    CHECK(stack.size() == 1);
    CHECK(x.Top() == game);
    CHECK_FALSE(x.f.context->exit_requested.load());
}

TEST_CASE("P3 AMS host shutdown waits for stop confirmation and keeps the Activity") {
    Fixture x;
    x.Start();
    x.Resumed();
    const auto token = x.Top();
    x.trace->calls.clear();
    QueueAndroidActivityRequest(*x.f.context, {DexVmAndroidContext::HostActivityRequest::Kind::shutdown, {}, {}});
    PumpAndroidHostRequests(x.f.vm, x.f.context);
    CHECK_FALSE(x.f.context->activities_stopped_for_shutdown);
    x.Paused(token);
    PumpAndroidHostRequests(x.f.vm, x.f.context);
    CHECK_FALSE(x.f.context->activities_stopped_for_shutdown);
    x.f.On(x.am, "activityStopped",
           "(Landroid/os/IBinder;Landroid/os/Bundle;Landroid/graphics/Bitmap;Ljava/lang/CharSequence;)V",
           {V::Ref(token), null, null, null});
    PumpAndroidHostRequests(x.f.vm, x.f.context);
    CHECK(x.f.context->activities_stopped_for_shutdown);
    CHECK(x.Top() == token);
    CHECK(x.trace->calls == std::vector<std::string>{"schedulePauseActivity", "scheduleStopActivity"});
}

TEST_CASE("P3 AMS moveTaskToBack stops the root and keeps the task queryable") {
    Fixture x;
    x.Start();
    x.Resumed();
    const auto token = x.Top();
    x.trace->calls.clear();
    CHECK(x.f.On(x.am, "moveActivityTaskToBack", "(Landroid/os/IBinder;Z)Z", {V::Ref(token), V::Int(0)}).AsInt() == 1);
    x.Paused(token);
    CHECK(x.trace->calls == std::vector<std::string>{"schedulePauseActivity", "scheduleStopActivity"});
    CHECK_FALSE(x.f.context->rom_activities->exit_requested);
}

TEST_CASE("P3 AMS a fast host resume keeps the queued pause stop resume order") {
    Fixture x;
    x.Start();
    x.Resumed();
    const auto token = x.Top();
    x.trace->calls.clear();
    RequestRomActivitySuspend(x.f.vm, x.f.context, true);
    RequestRomActivitySuspend(x.f.vm, x.f.context, false);
    x.Paused(token);
    CHECK(x.trace->calls ==
          std::vector<std::string>{"schedulePauseActivity", "scheduleStopActivity", "scheduleResumeActivity"});
}

TEST_CASE("Google Play market links resolve and launch without changing the Activity stack") {
    Fixture x;
    x.Start();
    x.Resumed();
    const auto caller = x.Top();
    const auto calls = x.trace->calls;
    const auto base = x.f.New("Landroid/content/Context;");
    const auto base_root = x.f.vm.ProtectReferences(std::array{base});
    const auto pm = x.f.On(base, "getPackageManager", "()Landroid/content/pm/PackageManager;").ref;
    const auto pm_root = x.f.vm.ProtectReferences(std::array{pm});
    const auto intent = x.f.New("Landroid/content/Intent;", "(Ljava/lang/String;)V",
                                {V::Ref(x.f.vm.NewStringUtf8("android.intent.action.VIEW"))});
    const auto intent_root = x.f.vm.ProtectReferences(std::array{intent});
    const auto uri = x.f.Static("Landroid/net/Uri;", "parse", "(Ljava/lang/String;)Landroid/net/Uri;",
                                {V::Ref(x.f.vm.NewStringUtf8("market://details?id=x"))})
                         .ref;
    x.f.On(intent, "setData", "(Landroid/net/Uri;)Landroid/content/Intent;", {V::Ref(uri)});
    const auto resolve = [&] {
        return x.f
            .On(pm, "resolveActivity", "(Landroid/content/Intent;I)Landroid/content/pm/ResolveInfo;",
                {V::Ref(intent), V::Int(0)})
            .ref;
    };
    const auto start = [&](int request) {
        return x.f.OnOutcome(x.am, "startActivity", start_signature,
                             {V::Ref(x.thread), V::Ref(x.f.vm.NewStringUtf8(x.f.context->package_name)), V::Ref(intent),
                              null, V::Ref(caller), null, V::Int(request), V::Int(0), null, null, null});
    };
    for (int mode = 0; mode < 3; ++mode) {
        if (mode == 1)
            x.f.On(intent, "setPackage", "(Ljava/lang/String;)Landroid/content/Intent;",
                   {V::Ref(x.f.vm.NewStringUtf8("com.android.vending"))});
        if (mode == 2) {
            const auto package = x.f.vm.NewStringUtf8("com.android.vending");
            const auto package_root = x.f.vm.ProtectReferences(std::array{package});
            const auto name = x.f.vm.NewStringUtf8("com.google.android.finsky.activities.LaunchUrlHandlerActivity");
            x.f.On(intent, "setClassName", "(Ljava/lang/String;Ljava/lang/String;)Landroid/content/Intent;",
                   {V::Ref(package), V::Ref(name)});
        }
        for (bool enabled : {true, false}) {
            x.f.context->google_play = enabled;
            const auto result = resolve();
            const auto result_root = x.f.vm.ProtectReferences(std::array{result});
            CHECK(result.IsValid() == enabled);
            if (enabled) {
                const auto info =
                    VmObjectRef{WindowField(x.f.vm, result, "activityInfo", "Landroid/content/pm/ActivityInfo;").bits};
                CHECK(x.f.vm.StringUtf8(
                          VmObjectRef{WindowField(x.f.vm, info, "packageName", "Ljava/lang/String;").bits}) ==
                      "com.android.vending");
                CHECK(x.f.vm.StringUtf8(VmObjectRef{WindowField(x.f.vm, info, "name", "Ljava/lang/String;").bits}) ==
                      "com.google.android.finsky.activities.LaunchUrlHandlerActivity");
            }
            const auto list = x.f.On(pm, "queryIntentActivities", "(Landroid/content/Intent;I)Ljava/util/List;",
                                     {V::Ref(intent), V::Int(0)})
                                  .ref;
            CHECK(x.f.On(list, "size", "()I").AsInt() == (enabled ? 1 : 0));
            const auto launched = start(-1);
            if (enabled)
                CHECK_FALSE(launched.exception.IsValid());
            else
                CHECK(launched.exception_class ==
                      x.f.linker.ResolveDescriptor("Landroid/content/ActivityNotFoundException;"));
            CHECK(x.Top() == caller);
            CHECK(x.trace->calls == calls);
        }
    }
    x.f.context->google_play = true;
    // The user leaves the store without a result.
    const auto for_result = start(7);
    CHECK_FALSE(for_result.exception.IsValid());
    CHECK(x.Top() == caller);
    REQUIRE(x.trace->calls.size() == calls.size() + 1U);
    CHECK(x.trace->calls.back() == "scheduleSendResult");
}
