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
            std::pair{"scheduleWindowVisibility", "(Landroid/os/IBinder;Z)V"},
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
    AndroidValueVm f{InterpreterBackend::threaded,           {ThreadProbe(trace)},         false, nullptr,
                     AndroidUiTraversalOwner::JavaTraversal, ogplay::gles::AngleBackend{}, true};
    VmObjectRef am{}, thread{};

    Fixture() {
        f.context->activity_inventory_known = true;
        for (int mode = 0; mode < 4; ++mode) {
            ogplay::loader::AndroidManifestActivityComponent c;
            c.name = "probe.Activity" + std::to_string(mode);
            c.launch_mode = static_cast<unsigned>(mode);
            c.screen_orientation = 1;
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

TEST_CASE("P3 AMS suspend resume and shutdown order is pause stop destroy") {
    Fixture x;
    x.Start();
    x.Resumed();
    auto token = x.Top();
    x.trace->calls.clear();
    RequestRomActivitySuspend(x.f.vm, x.f.context, true);
    REQUIRE(x.trace->calls == std::vector<std::string>{"schedulePauseActivity"});
    x.Paused(token);
    CHECK(x.trace->calls ==
          std::vector<std::string>{"schedulePauseActivity", "scheduleWindowVisibility", "scheduleStopActivity"});
    RequestRomActivitySuspend(x.f.vm, x.f.context, false);
    CHECK(x.trace->calls.back() == "scheduleResumeActivity");
    x.Resumed();
    x.trace->calls.clear();
    RequestRomActivityExit(x.f.vm, x.f.context);
    x.Paused(token);
    CHECK(x.trace->calls == std::vector<std::string>{"schedulePauseActivity", "scheduleWindowVisibility",
                                                     "scheduleStopActivity", "scheduleDestroyActivity"});
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
    CHECK(x.f.On(x.am, "getRequestedOrientation", "(Landroid/os/IBinder;)I", {V::Ref(isolated)}).AsInt() == 1);
}

TEST_CASE("P3 AMS exit deadline follows the injected Clock without declaring pending destroys complete") {
    Fixture x;
    x.Start();
    x.Resumed();
    const auto token = x.Top();
    RequestRomActivityExit(x.f.vm, x.f.context, 20);
    CHECK(ReadRomActivityExitStatus(x.f.context) == RomActivityExitStatus::waiting);
    AdvanceAndroidClock(*x.f.context, 20);
    CHECK(ReadRomActivityExitStatus(x.f.context) == RomActivityExitStatus::deadline_expired);
    x.Paused(token);
    x.f.On(x.am, "activityDestroyed", "(Landroid/os/IBinder;)V", {V::Ref(token)});
    CHECK(ReadRomActivityExitStatus(x.f.context) == RomActivityExitStatus::complete);
}

TEST_CASE("P3 AMS moveTaskToBack stops the root and keeps the task queryable") {
    Fixture x;
    x.Start();
    x.Resumed();
    const auto token = x.Top();
    x.trace->calls.clear();
    CHECK(x.f.On(x.am, "moveActivityTaskToBack", "(Landroid/os/IBinder;Z)Z", {V::Ref(token), V::Int(0)}).AsInt() == 1);
    x.Paused(token);
    CHECK(x.trace->calls ==
          std::vector<std::string>{"schedulePauseActivity", "scheduleWindowVisibility", "scheduleStopActivity"});
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
    CHECK(x.trace->calls == std::vector<std::string>{"schedulePauseActivity", "scheduleWindowVisibility",
                                                     "scheduleStopActivity", "scheduleResumeActivity"});
}
