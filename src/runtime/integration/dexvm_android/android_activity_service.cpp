#include "rom_activity.h"
#include "rom_window.h"
#include "rom_input.h"

#include <algorithm>
#include <limits>

namespace ogplay::runtime::android_intrinsics {
namespace {
using V = dx::VmValue;
using Ref = dx::VmObjectRef;
using Record = RomActivityState::Record;
const auto null = V::Ref(Ref{});

RomActivityState& Activities(const Context& context) {
    if (!context->rom_activities) context->rom_activities = std::make_shared<RomActivityState>();
    return *context->rom_activities;
}

Record* Find(RomActivityState& s, Ref token) {
    const auto i = std::ranges::find_if(s.stack, [token](const auto& r) { return r.token == token; });
    return i == s.stack.end() ? nullptr : &*i;
}

void Send(dx::Interpreter& vm, RomActivityState& s, const char* name, const char* sig, std::vector<V> args) {
    if (!s.application_thread.IsValid())
        throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;", "application thread is not attached"};
    static_cast<void>(CallAndroidMethod(vm, s.application_thread, name, sig, std::move(args)));
}

void Pause(dx::Interpreter& vm, RomActivityState& s, Record& r) {
    if (r.pausing) return;
    r.resumed = false;
    r.pausing = true;
    Send(vm, s, "schedulePauseActivity", "(Landroid/os/IBinder;ZZI)V",
         {V::Ref(r.token), V::Int(r.finishing), V::Int(0), V::Int(0)});
}

void Hide(dx::Interpreter& vm, const Context& context, Record& r) {
    if (!r.visible) return;
    r.visible = false;
    SetRomApplicationVisibility(vm, context, r.token, false);
}

void Stop(dx::Interpreter& vm, const Context& context, RomActivityState& s, Record& r) {
    if (r.stopping || r.stopped) return;
    r.stopping = true;
    Hide(vm, context, r);
    Send(vm, s, "scheduleStopActivity", "(Landroid/os/IBinder;ZI)V", {V::Ref(r.token), V::Int(0), V::Int(0)});
}

Ref List(dx::Interpreter& vm) {
    return NewPlatformObject(vm, "Ljava/util/ArrayList;", "()V", {});
}

void Append(dx::Interpreter& vm, Ref list, Ref item) {
    static_cast<void>(CallAndroidMethod(vm, list, "add", "(Ljava/lang/Object;)Z", {V::Ref(item)}));
}

void Result(dx::Interpreter& vm, RomActivityState& s, const Record& r) {
    if (s.exiting) return;
    if (r.request < 0 || !Find(s, r.caller)) return;
    const auto result =
        NewPlatformObject(vm, "Landroid/app/ResultInfo;", "(Ljava/lang/String;IILandroid/content/Intent;)V",
                          {null, V::Int(r.request), V::Int(r.result), V::Ref(r.result_data)});
    const auto roots = vm.ProtectReferences(std::array{result});
    const auto list = List(vm);
    const auto list_root = vm.ProtectReferences(std::array{list});
    Append(vm, list, result);
    Send(vm, s, "scheduleSendResult", "(Landroid/os/IBinder;Ljava/util/List;)V", {V::Ref(r.caller), V::Ref(list)});
}

void Destroy(dx::Interpreter& vm, RomActivityState& s, Record& r) {
    if (r.destroying) return;
    Result(vm, s, r);
    r.destroying = true;
    Send(vm, s, "scheduleDestroyActivity", "(Landroid/os/IBinder;ZI)V", {V::Ref(r.token), V::Int(1), V::Int(0)});
}

void Finish(dx::Interpreter& vm, const Context& context, RomActivityState& s, Record& r) {
    if (r.pausing) return;
    if (r.resumed || r.launching) {
        Pause(vm, s, r);
    } else {
        Stop(vm, context, s, r);
        Destroy(vm, s, r);
    }
}

void UpdateForeground(dx::Interpreter& vm, const Context& context, const RomActivityState& s) {
    if (s.stack.empty() || !s.application_thread.IsValid()) {
        context->foreground_activity = Ref{};
        return;
    }
    const auto type = vm.Linker().ResolveDescriptor("Landroid/app/ActivityThread;");
    const auto current = vm.Linker().FindDirectMethod(type, "currentActivityThread", "()Landroid/app/ActivityThread;");
    const auto result = vm.Call(*current, {});
    if (result.exception.IsValid()) throw dx::VmJavaThrow{"", "", result.exception};
    const auto thread = result.value.ref;
    if (!thread.IsValid()) {
        context->foreground_activity = Ref{};
        return;
    }
    const auto activities = Ref{WindowField(vm, thread, "mActivities", "Landroid/util/ArrayMap;").bits};
    const auto record = CallAndroidMethod(vm, activities, "get", "(Ljava/lang/Object;)Ljava/lang/Object;",
                                          {V::Ref(s.stack.back().token)})
                            .ref;
    context->foreground_activity =
        record.IsValid() ? Ref{WindowField(vm, record, "activity", "Landroid/app/Activity;").bits} : Ref{};
}

void ApplyOrientation(dx::Interpreter& vm, const Context& context);

void EnsureConfiguration(dx::Interpreter& vm, RomActivityState& s, Record& r) {
    if (!s.configuration.IsValid() || r.finishing || r.destroying) return;
    const auto changes = r.configuration.IsValid()
                             ? CallAndroidMethod(vm, r.configuration, "diff", "(Landroid/content/res/Configuration;)I",
                                                 {V::Ref(s.configuration)})
                                   .AsInt()
                             : 0;
    r.configuration = s.configuration;
    if (!changes) return;
    const auto handled = CallAndroidMethod(vm, r.info, "getRealConfigChanged", "()I").AsInt();
    if (changes & ~handled) {
        Send(vm, s, "scheduleRelaunchActivity",
             "(Landroid/os/IBinder;Ljava/util/List;Ljava/util/List;IZLandroid/content/res/Configuration;)V",
             {V::Ref(r.token), null, null, V::Int(changes), V::Int(!r.resumed), V::Ref(s.configuration)});
    } else {
        Send(vm, s, "scheduleActivityConfigurationChanged", "(Landroid/os/IBinder;)V", {V::Ref(r.token)});
    }
}

void Resume(dx::Interpreter& vm, const Context& context, RomActivityState& s) {
    if (s.suspended || s.exiting || s.stack.empty()) return;
    auto& top = s.stack.back();
    if (s.background_tasks.contains(top.task)) return;
    if (top.finishing || top.destroying || top.resumed || top.launching || top.pausing) return;
    context->requested_activity_orientation.store(top.orientation);
    ApplyOrientation(vm, context);
    EnsureConfiguration(vm, s, top);
    top.launching = true;
    Send(vm, s, "scheduleResumeActivity", "(Landroid/os/IBinder;IZ)V", {V::Ref(top.token), V::Int(2), V::Int(1)});
}

void Launch(dx::Interpreter& vm, const Context& context, const DexVmAndroidContext::ActivityCommand& cmd) {
    auto& s = Activities(context);
    s.exit_requested = false;
    if (s.stack.size() >= 32)
        throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;", "Activity stack budget exhausted"};
    int task = s.stack.empty() ? 1 : s.stack.back().task;
    auto reuse = s.stack.end();
    const bool isolated_source = !s.stack.empty() && s.stack.back().mode == 3;
    if ((!cmd.package_affinity && (cmd.launch_mode == 2 || (isolated_source && cmd.launch_mode != 3))) ||
        (cmd.request_code >= 0 && (cmd.launch_mode > 1 || isolated_source)))
        throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;", "custom affinity or cross-task results"};
    if (cmd.launch_mode == 3) {
        reuse = std::ranges::find_if(s.stack,
                                     [&](const auto& r) { return !r.finishing && r.descriptor == cmd.descriptor; });
        task = reuse == s.stack.end() ? s.next_task++ : reuse->task;
    } else if (cmd.launch_mode == 2 || (isolated_source && cmd.launch_mode != 3)) {
        const auto normal = std::find_if(s.stack.rbegin(), s.stack.rend(), [](const auto& r) { return r.mode != 3; });
        task = normal == s.stack.rend() ? s.next_task++ : normal->task;
        if (cmd.launch_mode == 2)
            reuse = std::ranges::find_if(s.stack, [&](const auto& r) {
                return !r.finishing && r.task == task && r.descriptor == cmd.descriptor;
            });
    } else if (cmd.launch_mode == 1 && !s.stack.empty() && s.stack.back().descriptor == cmd.descriptor &&
               !s.stack.back().finishing)
        reuse = std::prev(s.stack.end());
    if (reuse != s.stack.end()) {
        s.background_tasks.erase(task);
        if (cmd.request_code >= 0)
            throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;", "results to a reused Activity"};
        const auto token = reuse->token;
        if (cmd.launch_mode == 2) {
            for (auto i = std::next(reuse); i != s.stack.end(); ++i) {
                if (i->task != task) continue;
                i->finishing = true;
                Hide(vm, context, *i);
            }
            for (auto i = std::next(reuse); i != s.stack.end(); ++i)
                if (i->task == task) {
                    Stop(vm, context, s, *i);
                    Destroy(vm, s, *i);
                }
        }
        const auto list = List(vm);
        const auto root = vm.ProtectReferences(std::array{list});
        Append(vm, list, cmd.intent);
        Send(vm, s, "scheduleNewIntent", "(Ljava/util/List;Landroid/os/IBinder;)V", {V::Ref(list), V::Ref(token)});
        // Keep task groups contiguous while bringing the destination task to front.
        std::stable_partition(s.stack.begin(), s.stack.end(), [&](const auto& r) { return r.task != task; });
        const auto current = std::ranges::find_if(s.stack, [&](const auto& r) { return r.token == token; });
        std::rotate(current, std::next(current), s.stack.end());
        Resume(vm, context, s);
        return;
    }
    const auto package = vm.NewStringUtf8(context->package_name);
    const auto package_root = vm.ProtectReferences(std::array{package});
    s.background_tasks.erase(task);
    const auto name = vm.NewStringUtf8(cmd.component);
    const auto name_root = vm.ProtectReferences(std::array{name});
    const auto component = NewAndroidComponentName(vm, package, name);
    const auto component_root = vm.ProtectReferences(std::array{component});
    const auto service = GetLocalServiceInterface(vm, "package", "Landroid/content/pm/IPackageManager$Stub;",
                                                  "Landroid/content/pm/IPackageManager;");
    const auto info = CallAndroidMethod(vm, service, "getActivityInfo",
                                        "(Landroid/content/ComponentName;II)Landroid/content/pm/ActivityInfo;",
                                        {V::Ref(component), V::Int(0), V::Int(0)})
                          .ref;
    const auto info_root = vm.ProtectReferences(std::array{info});
    const auto token = NewPlatformObject(vm, "Landroid/os/Binder;", "()V", {});
    Record r;
    r.token = token;
    r.intent = cmd.intent;
    r.info = info;
    r.component = component;
    r.descriptor = cmd.descriptor;
    r.component_name = cmd.component;
    r.task = task;
    r.request = cmd.request_code;
    r.caller = cmd.owner;
    r.mode = cmd.launch_mode;
    if (const auto* declared =
            loader::FindAndroidManifestActivityComponent(context->activity_components, cmd.component))
        r.orientation = declared->screen_orientation;
    context->requested_activity_orientation.store(r.orientation);
    s.stack.push_back(std::move(r));
    const auto resources = [&] {
        dx::IntrinsicContext call{vm, token, {}};
        return ResourceObject(call, context, false);
    }();
    if (!s.configuration.IsValid()) {
        const auto initial =
            CallAndroidMethod(vm, resources, "getConfiguration", "()Landroid/content/res/Configuration;").ref;
        s.configuration = NewPlatformObject(vm, "Landroid/content/res/Configuration;",
                                            "(Landroid/content/res/Configuration;)V", {V::Ref(initial)});
    }
    const auto config = s.configuration;
    s.stack.back().configuration = config;
    const auto config_root = vm.ProtectReferences(std::array{config});
    const auto compat_type = vm.Linker().ResolveDescriptor("Landroid/content/res/CompatibilityInfo;");
    const auto initialized = vm.EnsureClassInitialized(compat_type);
    if (initialized.exception.IsValid()) throw dx::VmJavaThrow{"", "", initialized.exception};
    const auto field = vm.Linker().FindFieldRecursive(compat_type, "DEFAULT_COMPATIBILITY_INFO",
                                                      "Landroid/content/res/CompatibilityInfo;");
    const auto compat = Ref{vm.Linker().Class(compat_type).static_storage[vm.Linker().Field(*field).slot]};
    Send(
        vm, s, "scheduleLaunchActivity",
        "(Landroid/content/Intent;Landroid/os/IBinder;ILandroid/content/pm/ActivityInfo;Landroid/content/res/Configuration;Landroid/content/res/CompatibilityInfo;ILandroid/os/Bundle;Ljava/util/List;Ljava/util/List;ZZLjava/lang/String;Landroid/os/ParcelFileDescriptor;Z)V",
        {V::Ref(cmd.intent), V::Ref(token), V::Int(task), V::Ref(info), V::Ref(config), V::Ref(compat), V::Int(2), null,
         null, null, V::Int(s.suspended), V::Int(1), null, null, V::Int(0)});
}

// Only Activity orientation requests change the running logical display. Density and
// user-selected pixel dimensions have no live frontend editing entry point.
void ApplyOrientation(dx::Interpreter& vm, const Context& context) {
    auto& s = Activities(context);
    if (s.stack.empty() || s.exiting || s.stack.back().launching || s.stack.back().pausing) return;
    const auto orientation = s.stack.back().orientation;
    const bool portrait = orientation == 1 || orientation == 7 || orientation == 9 || orientation == 12;
    const bool landscape = orientation == 0 || orientation == 6 || orientation == 8 || orientation == 11;
    if ((!portrait && !landscape) || context->surface_width == context->surface_height ||
        portrait == (context->surface_width < context->surface_height))
        return;
    const auto old = s.configuration;
    const auto config = NewPlatformObject(vm, "Landroid/content/res/Configuration;",
                                          "(Landroid/content/res/Configuration;)V", {V::Ref(old)});
    const auto root = vm.ProtectReferences(std::array{config});
    std::swap(context->surface_width, context->surface_height);
    std::swap(WindowField(vm, config, "screenWidthDp", "I"), WindowField(vm, config, "screenHeightDp", "I"));
    WindowField(vm, config, "orientation", "I") = {portrait ? 1U : 2U, dx::SlotTag::cat1};
    WindowField(vm, config, "seq", "I") = {static_cast<unsigned>(++s.configuration_sequence), dx::SlotTag::cat1};
    s.configuration = config;
    ResizeRomDisplay(vm, context);
    // Invalidate DisplayManagerGlobal before ResourcesManager fetches new metrics.
    if (const auto callback = context->singletons.find("display_callback"); callback != context->singletons.end())
        static_cast<void>(CallAndroidMethod(vm, callback->second, "onDisplayEvent", "(II)V", {V::Int(0), V::Int(2)}));
    Send(vm, s, "scheduleConfigurationChanged", "(Landroid/content/res/Configuration;)V", {V::Ref(config)});
    for (auto& r : s.stack)
        if (r.visible) EnsureConfiguration(vm, s, r);
}

void Drain(dx::Interpreter& vm, const Context& context) {
    auto& s = Activities(context);
    if (s.pending.empty() || s.exiting) return;
    if (!s.stack.empty() && (s.stack.back().launching || s.stack.back().pausing)) return;
    if (!s.stack.empty() && s.stack.back().resumed) {
        Pause(vm, s, s.stack.back());
        return;
    }
    const auto cmd = s.pending.front();
    const auto command_roots = vm.ProtectReferences(std::array{cmd.owner, cmd.intent});
    s.pending.pop_front();
    Launch(vm, context, cmd);
}
}  // namespace

void SetRomApplicationThread(const Context& context, Ref thread) {
    Activities(context).application_thread = thread;
}

void VisitRomActivityRoots(const DexVmAndroidContext& context, const dx::VmRootVisitor& visit) {
    if (!context.rom_activities) return;
    const auto root = [&](Ref ref) {
        if (ref.IsValid()) visit(ref);
    };
    const auto& s = *context.rom_activities;
    root(s.application_thread);
    root(s.configuration);
    for (const auto& r : s.stack)
        for (const auto ref : {r.token, r.intent, r.info, r.component, r.caller, r.result_data, r.configuration})
            root(ref);
    for (const auto& cmd : s.pending) {
        root(cmd.owner);
        root(cmd.intent);
    }
}

void StartRomLauncher(dx::Interpreter& vm, const Context& context, const std::string& descriptor,
                      const std::string& component) {
    const auto action = vm.NewStringUtf8("android.intent.action.MAIN");
    const auto action_root = vm.ProtectReferences(std::array{action});
    const auto intent = NewPlatformObject(vm, "Landroid/content/Intent;", "(Ljava/lang/String;)V", {V::Ref(action)});
    const auto intent_root = vm.ProtectReferences(std::array{intent});
    const auto category = vm.NewStringUtf8("android.intent.category.LAUNCHER");
    static_cast<void>(CallAndroidMethod(vm, intent, "addCategory", "(Ljava/lang/String;)Landroid/content/Intent;",
                                        {V::Ref(category)}));
    const auto package = vm.NewStringUtf8(context->package_name);
    const auto package_root = vm.ProtectReferences(std::array{package});
    const auto name = vm.NewStringUtf8(component);
    const auto component_name = NewAndroidComponentName(vm, package, name);
    const auto component_root = vm.ProtectReferences(std::array{component_name});
    static_cast<void>(CallAndroidMethod(vm, intent, "setComponent",
                                        "(Landroid/content/ComponentName;)Landroid/content/Intent;",
                                        {V::Ref(component_name)}));
    static_cast<void>(CallAndroidMethod(vm, intent, "addFlags", "(I)Landroid/content/Intent;", {V::Int(0x10000000)}));
    DexVmAndroidContext::ActivityCommand launch;
    launch.intent = intent;
    launch.descriptor = descriptor;
    launch.component = component;
    if (const auto* declared = loader::FindAndroidManifestActivityComponent(context->activity_components, component))
        launch.launch_mode = declared->launch_mode;
    Launch(vm, context, launch);
}

void RequestRomActivitySuspend(dx::Interpreter& vm, const Context& context, bool suspend) {
    auto& s = Activities(context);
    if (s.suspended == suspend || s.exiting) return;
    s.suspended = suspend;
    if (s.stack.empty()) return;
    if (suspend) {
        auto& top = s.stack.back();
        top.suspend_stop_required = true;
        if (top.resumed || top.launching)
            Pause(vm, s, top);
        else
            Stop(vm, context, s, top);
    } else
        Resume(vm, context, s);
}

void RequestRomActivityExit(dx::Interpreter& vm, const Context& context, std::int64_t budget_millis) {
    if (budget_millis < 0) throw std::invalid_argument("negative Activity exit budget");
    auto& s = Activities(context);
    if (s.exiting) return;
    s.exiting = true;
    s.pending.clear();
    const auto now = context->uptime_millis.load();
    s.exit_deadline_millis = now > std::numeric_limits<std::int64_t>::max() - budget_millis
                                 ? std::numeric_limits<std::int64_t>::max()
                                 : now + budget_millis;
    // Queue focus loss across the stack before any lifecycle message for this exit.
    for (auto& r : s.stack) {
        r.finishing = true;
        Hide(vm, context, r);
    }
    for (auto& r : s.stack)
        Finish(vm, context, s, r);
    s.exit_requested = s.stack.empty();
    context->activities_destroyed.store(s.exit_requested);
}

RomActivityExitStatus ReadRomActivityExitStatus(const Context& context) {
    if (!context->rom_activities || context->rom_activities->exit_requested) return RomActivityExitStatus::complete;
    const auto& s = *context->rom_activities;
    return s.exiting && context->uptime_millis.load() >= s.exit_deadline_millis
               ? RomActivityExitStatus::deadline_expired
               : RomActivityExitStatus::waiting;
}

void AddRomActivityMethods(dx::IntrinsicClassBuilder& b, const Context& context) {
    b.VirtualMethod(
        "startActivity",
        "(Landroid/app/IApplicationThread;Ljava/lang/String;Landroid/content/Intent;Ljava/lang/String;Landroid/os/IBinder;Ljava/lang/String;IILjava/lang/String;Landroid/os/ParcelFileDescriptor;Landroid/os/Bundle;)I",
        [context](dx::IntrinsicContext& c) {
            auto& s = Activities(context);
            if (s.exiting || context->process_stopping)
                throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;", "process is stopping"};
            if (c.arguments[3].ref.IsValid() || c.arguments[5].ref.IsValid() || c.arguments[7].AsInt() ||
                c.arguments[8].ref.IsValid() || c.arguments[9].ref.IsValid() || c.arguments[10].ref.IsValid())
                throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;",
                                      "unsupported Activity start options"};
            const std::array args{c.arguments[2]};
            dx::IntrinsicContext resolve{c.vm, c.arguments[4].ref, args};
            auto cmd = ResolveAndroidActivity(resolve, context, c.arguments[6].AsInt());
            if (cmd) {
                cmd->owner = c.arguments[4].ref;
                const bool isolated = !s.stack.empty() && s.stack.back().mode == 3;
                const bool top_reuse =
                    !s.stack.empty() && s.stack.back().descriptor == cmd->descriptor && cmd->launch_mode == 1;
                if ((cmd->request_code >= 0 && (cmd->launch_mode > 1 || isolated || top_reuse)) ||
                    (!cmd->package_affinity && (cmd->launch_mode == 2 || (isolated && cmd->launch_mode != 3)))) {
                    if (auto* ledger = c.vm.Ledger()) ledger->RecordUnimplemented("dexvm.activity_result", 0);
                    throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;",
                                          "Activity custom affinity or reused/cross-task results"};
                }
                if (s.pending.size() >= 32)
                    throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;", "Activity command budget exhausted"};
                s.pending.push_back(*cmd);
                Drain(c.vm, context);
            }
            return V::Int(0);
        });
    b.VirtualMethod("finishActivity", "(Landroid/os/IBinder;ILandroid/content/Intent;)Z",
                    [context](dx::IntrinsicContext& c) {
                        auto& s = Activities(context);
                        auto* r = Find(s, c.arguments[0].ref);
                        if (!r || r->finishing) return V::Int(0);
                        r->finishing = true;
                        r->result = c.arguments[1].AsInt();
                        r->result_data = c.arguments[2].ref;
                        Hide(c.vm, context, *r);
                        Finish(c.vm, context, s, *r);
                        return V::Int(1);
                    });
    b.VirtualMethod("activityResumed", "(Landroid/os/IBinder;)V", [context](dx::IntrinsicContext& c) {
        auto& s = Activities(context);
        if (auto* r = Find(s, c.arguments[0].ref)) {
            r->resumed = true;
            r->paused = false;
            r->stopped = false;
            r->stopping = false;
            r->launching = false;
            if (!r->finishing) {
                r->visible = true;
                SetRomApplicationVisibility(c.vm, context, r->token, true);
            }
        }
        for (auto& r : s.stack)
            if (r.token != c.arguments[0].ref && r.paused && !r.stopped && !r.destroying) Stop(c.vm, context, s, r);
        if (auto* r = Find(s, c.arguments[0].ref);
            r && (r->finishing || s.suspended || !s.pending.empty() || s.background_tasks.contains(r->task)))
            Pause(c.vm, s, *r);
        UpdateForeground(c.vm, context, s);
        return V::Void();
    });
    b.VirtualMethod("activityPaused", "(Landroid/os/IBinder;)V", [context](dx::IntrinsicContext& c) {
        auto& s = Activities(context);
        if (auto* r = Find(s, c.arguments[0].ref)) {
            r->paused = true;
            r->resumed = false;
            r->pausing = false;
            r->launching = false;
            if (r->finishing || s.suspended || s.exiting || r->suspend_stop_required ||
                s.background_tasks.contains(r->task)) {
                Stop(c.vm, context, s, *r);
                r->suspend_stop_required = false;
                if (r->finishing || s.exiting) Destroy(c.vm, s, *r);
            }
        }
        Drain(c.vm, context);
        if (s.pending.empty()) Resume(c.vm, context, s);
        return V::Void();
    });
    b.VirtualMethod("activityStopped",
                    "(Landroid/os/IBinder;Landroid/os/Bundle;Landroid/graphics/Bitmap;Ljava/lang/CharSequence;)V",
                    [context](dx::IntrinsicContext& c) {
                        if (auto* r = Find(Activities(context), c.arguments[0].ref)) {
                            if (!r->resumed) r->stopped = true;
                            r->stopping = false;
                        }
                        return V::Void();
                    });
    b.VirtualMethod("activityIdle", "(Landroid/os/IBinder;Landroid/content/res/Configuration;Z)V",
                    [context](dx::IntrinsicContext& c) {
                        auto& s = Activities(context);
                        if (auto* r = Find(s, c.arguments[0].ref)) {
                            const bool completed_launch = r->launching && !r->paused && !r->stopped && !r->finishing &&
                                                          r->token == s.stack.back().token;
                            r->idle = true;
                            r->launching = false;
                            if (completed_launch && !s.suspended && !s.background_tasks.contains(r->task)) {
                                r->resumed = true;
                                r->visible = true;
                                SetRomApplicationVisibility(c.vm, context, r->token, true);
                            }
                            if (s.suspended || s.exiting || s.background_tasks.contains(r->task)) {
                                if (r->resumed)
                                    Pause(c.vm, s, *r);
                                else
                                    Stop(c.vm, context, s, *r);
                            }
                        }
                        for (auto& r : s.stack)
                            if (r.token != c.arguments[0].ref && r.paused && !r.stopped && !r.destroying)
                                Stop(c.vm, context, s, r);
                        Drain(c.vm, context);
                        UpdateForeground(c.vm, context, s);
                        return V::Void();
                    });
    b.VirtualMethod("activityDestroyed", "(Landroid/os/IBinder;)V", [context](dx::IntrinsicContext& c) {
        auto& s = Activities(context);
        std::erase_if(s.stack, [&](const auto& r) { return r.token == c.arguments[0].ref; });
        if (s.stack.empty() && s.pending.empty()) {
            s.exit_requested = true;
            context->activities_destroyed.store(true);
            if (!s.exiting) context->exit_requested.store(true);
        } else if (!s.stack.empty()) {
            context->requested_activity_orientation.store(s.stack.back().orientation);
            Resume(c.vm, context, s);
        }
        Drain(c.vm, context);
        UpdateForeground(c.vm, context, s);
        return V::Void();
    });
    b.VirtualMethod("getTaskForActivity", "(Landroid/os/IBinder;Z)I", [context](dx::IntrinsicContext& c) {
        auto& s = Activities(context);
        const auto* r = Find(s, c.arguments[0].ref);
        if (!r) return V::Int(-1);
        const auto first =
            std::ranges::find_if(s.stack, [&](const auto& item) { return item.task == r->task && !item.destroying; });
        return V::Int(c.arguments[1].AsInt() && (first == s.stack.end() || first->token != r->token) ? -1 : r->task);
    });
    b.VirtualMethod("isTopOfTask", "(Landroid/os/IBinder;)Z", [context](dx::IntrinsicContext& c) {
        auto& s = Activities(context);
        const auto* r = Find(s, c.arguments[0].ref);
        if (!r) return V::Int(0);
        const auto top = std::find_if(s.stack.rbegin(), s.stack.rend(),
                                      [&](const auto& item) { return item.task == r->task && !item.destroying; });
        return V::Int(top != s.stack.rend() && top->token == r->token);
    });
    b.VirtualMethod("getRequestedOrientation", "(Landroid/os/IBinder;)I", [context](dx::IntrinsicContext& c) {
        const auto* r = Find(Activities(context), c.arguments[0].ref);
        return V::Int(r ? r->orientation : -1);
    });
    b.VirtualMethod("willActivityBeVisible", "(Landroid/os/IBinder;)Z", [context](dx::IntrinsicContext& c) {
        auto& s = Activities(context);
        const auto* r = Find(s, c.arguments[0].ref);
        return V::Int(r && !r->finishing && !r->destroying && !s.exiting && !s.background_tasks.contains(r->task));
    });
    b.VirtualMethod("setRequestedOrientation", "(Landroid/os/IBinder;I)V", [context](dx::IntrinsicContext& c) {
        auto& s = Activities(context);
        if (auto* r = Find(s, c.arguments[0].ref)) {
            r->orientation = c.arguments[1].AsInt();
            if (r == &s.stack.back()) context->requested_activity_orientation.store(r->orientation);
        }
        return V::Void();
    });
    b.VirtualMethod("overridePendingTransition", "(Landroid/os/IBinder;Ljava/lang/String;II)V",
                    [](dx::IntrinsicContext&) { return V::Void(); });
    b.VirtualMethod("moveActivityTaskToBack", "(Landroid/os/IBinder;Z)Z", [context](dx::IntrinsicContext& c) {
        auto& s = Activities(context);
        auto* r = Find(s, c.arguments[0].ref);
        if (!r) return V::Int(0);
        const int task = r->task;
        const auto first = std::ranges::find_if(s.stack, [&](const auto& item) { return item.task == task; });
        if (!c.arguments[1].AsInt() && first->token != r->token) return V::Int(0);
        s.background_tasks.insert(task);
        if (!s.stack.empty() && s.stack.back().task == task) {
            if (s.stack.back().resumed)
                Pause(c.vm, s, s.stack.back());
            else
                Stop(c.vm, context, s, s.stack.back());
        }
        std::stable_partition(s.stack.begin(), s.stack.end(), [task](const auto& item) { return item.task == task; });
        if (!s.stack.empty() && s.stack.back().task != task) Resume(c.vm, context, s);
        return V::Int(1);
    });
    b.VirtualMethod(
        "getTasks", "(IILandroid/app/IThumbnailReceiver;)Ljava/util/List;", [context](dx::IntrinsicContext& c) {
            auto& s = Activities(context);
            auto& vm = c.vm;
            if (c.arguments[1].AsInt() || c.arguments[2].ref.IsValid())
                throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;", "task flags and thumbnails"};
            const auto list = List(vm);
            const auto list_root = vm.ProtectReferences(std::array{list});
            std::set<int> seen;
            for (auto i = s.stack.rbegin();
                 i != s.stack.rend() && static_cast<int>(seen.size()) < c.arguments[0].AsInt(); ++i) {
                if (i->destroying || !seen.insert(i->task).second) continue;
                const auto first =
                    std::ranges::find_if(s.stack, [&](const auto& r) { return r.task == i->task && !r.destroying; });
                const auto info = NewPlatformObject(vm, "Landroid/app/ActivityManager$RunningTaskInfo;", "()V", {});
                const auto root = vm.ProtectReferences(std::array{info});
                const auto count = static_cast<unsigned>(
                    std::ranges::count_if(s.stack, [&](const auto& r) { return r.task == i->task && !r.destroying; }));
                WindowField(vm, info, "id", "I") = {static_cast<unsigned>(i->task), dx::SlotTag::cat1};
                WindowField(vm, info, "baseActivity", "Landroid/content/ComponentName;") = {first->component.Value(),
                                                                                            dx::SlotTag::ref};
                WindowField(vm, info, "topActivity", "Landroid/content/ComponentName;") = {i->component.Value(),
                                                                                           dx::SlotTag::ref};
                WindowField(vm, info, "numActivities", "I") = {count, dx::SlotTag::cat1};
                WindowField(vm, info, "numRunning", "I") = {count, dx::SlotTag::cat1};
                Append(vm, list, info);
            }
            return V::Ref(list);
        });
    b.VirtualMethod("getRecentTasks", "(III)Ljava/util/List;", [context](dx::IntrinsicContext& call) {
        RequireServicePermission(context, "GET_TASKS");
        if (call.arguments[2].AsInt() != 0)
            throw dx::VmJavaThrow{"Ljava/lang/SecurityException;", "Recent tasks outside current user"};
        auto& vm = call.vm;
        auto& state = Activities(context);
        const auto list = List(vm);
        const auto root = vm.ProtectReferences(std::array{list});
        std::set<int> seen;
        for (auto it = state.stack.rbegin();
             it != state.stack.rend() && static_cast<int>(seen.size()) < call.arguments[0].AsInt(); ++it) {
            if (it->destroying || !seen.insert(it->task).second) continue;
            const auto base = std::ranges::find_if(
                state.stack, [&](const auto& record) { return record.task == it->task && !record.destroying; });
            const auto info = NewPlatformObject(vm, "Landroid/app/ActivityManager$RecentTaskInfo;", "()V", {});
            const auto info_root = vm.ProtectReferences(std::array{info});
            const auto id = static_cast<unsigned>(it->task);
            WindowField(vm, info, "id", "I") = {id, dx::SlotTag::cat1};
            WindowField(vm, info, "persistentId", "I") = {id, dx::SlotTag::cat1};
            const auto intent = NewPlatformObject(vm, "Landroid/content/Intent;", "(Landroid/content/Intent;)V",
                                                  {V::Ref(base->intent)});
            WindowField(vm, info, "baseIntent", "Landroid/content/Intent;") = {intent.Value(), dx::SlotTag::ref};
            Append(vm, list, info);
        }
        return V::Ref(list);
    });
    b.VirtualMethod("getActivityClassForToken", "(Landroid/os/IBinder;)Landroid/content/ComponentName;",
                    [context](dx::IntrinsicContext& call) {
                        const auto* record = Find(Activities(context), call.arguments[0].ref);
                        return V::Ref(record ? record->component : dx::VmObjectRef{});
                    });
    b.VirtualMethod("getCallingActivity", "(Landroid/os/IBinder;)Landroid/content/ComponentName;",
                    [context](dx::IntrinsicContext& call) {
                        auto& state = Activities(context);
                        const auto* record = Find(state, call.arguments[0].ref);
                        const auto* caller = record && record->request >= 0 ? Find(state, record->caller) : nullptr;
                        return V::Ref(caller ? caller->component : dx::VmObjectRef{});
                    });
    b.VirtualMethod("getCallingPackage", "(Landroid/os/IBinder;)Ljava/lang/String;",
                    [context](dx::IntrinsicContext& call) {
                        auto& state = Activities(context);
                        const auto* record = Find(state, call.arguments[0].ref);
                        return record && record->request >= 0 && Find(state, record->caller)
                                   ? MakeString(call, context->package_name)
                                   : V::Ref(dx::VmObjectRef{});
                    });
    for (const auto* name : {"getPackageForToken", "getLaunchedFromPackage"})
        b.VirtualMethod(name, "(Landroid/os/IBinder;)Ljava/lang/String;", [context](dx::IntrinsicContext& call) {
            return Find(Activities(context), call.arguments[0].ref) ? MakeString(call, context->package_name)
                                                                    : V::Ref(dx::VmObjectRef{});
        });
    b.VirtualMethod("getLaunchedFromUid", "(Landroid/os/IBinder;)I", [context](dx::IntrinsicContext& call) {
        return V::Int(Find(Activities(context), call.arguments[0].ref) ? static_cast<int>(context->application_uid)
                                                                       : -1);
    });
    b.VirtualMethod("setImmersive", "(Landroid/os/IBinder;Z)V", [context](dx::IntrinsicContext& call) {
        auto* record = Find(Activities(context), call.arguments[0].ref);
        if (!record) throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "Unknown activity token"};
        record->immersive = call.arguments[1].AsInt() != 0;
        return V::Void();
    });
    b.VirtualMethod("isImmersive", "(Landroid/os/IBinder;)Z", [context](dx::IntrinsicContext& call) {
        const auto* record = Find(Activities(context), call.arguments[0].ref);
        if (!record) throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "Unknown activity token"};
        return V::Int(record->immersive);
    });
    b.VirtualMethod("isTopActivityImmersive", "()Z", [context](dx::IntrinsicContext&) {
        const auto& stack = Activities(context).stack;
        return V::Int(!stack.empty() && stack.back().immersive);
    });
    b.VirtualMethod("targetTaskAffinityMatchesActivity", "(Landroid/os/IBinder;Ljava/lang/String;)Z",
                    [context](dx::IntrinsicContext& call) {
                        const auto* record = Find(Activities(context), call.arguments[0].ref);
                        if (!record || !call.arguments[1].ref.IsValid()) return V::Int(0);
                        const auto affinity = WindowField(call.vm, record->info, "taskAffinity", "Ljava/lang/String;");
                        const auto value = dx::VmObjectRef{affinity.bits};
                        return V::Int(value.IsValid() &&
                                      call.vm.StringUtf8(value) == call.vm.StringUtf8(call.arguments[1].ref));
                    });
    b.VirtualMethod("finishActivityAffinity", "(Landroid/os/IBinder;)Z", [context](dx::IntrinsicContext& call) {
        auto& state = Activities(context);
        const auto* target = Find(state, call.arguments[0].ref);
        if (!target || target->finishing) return V::Int(0);
        const auto affinity_of = [&](const auto& activity) {
            const auto value =
                dx::VmObjectRef{WindowField(call.vm, activity.info, "taskAffinity", "Ljava/lang/String;").bits};
            return value.IsValid() ? std::optional{call.vm.StringUtf8(value)} : std::nullopt;
        };
        const auto affinity = affinity_of(*target);
        std::vector<dx::VmObjectRef> tokens;
        auto index = static_cast<std::size_t>(target - state.stack.data());
        while (true) {
            const auto& record = state.stack[index];
            if (record.task != target->task || affinity_of(record) != affinity) break;
            tokens.push_back(record.token);
            if (index-- == 0) break;
        }
        const auto root = call.vm.ProtectReferences(tokens);
        for (const auto token : tokens)
            static_cast<void>(CallAndroidMethod(call.vm, call.receiver, "finishActivity",
                                                "(Landroid/os/IBinder;ILandroid/content/Intent;)Z",
                                                {V::Ref(token), V::Int(0), V::Ref(dx::VmObjectRef{})}));
        return V::Int(1);
    });
    b.VirtualMethod("finishSubActivity", "(Landroid/os/IBinder;Ljava/lang/String;I)V",
                    [context](dx::IntrinsicContext& call) {
                        if (call.arguments[1].ref.IsValid()) return V::Void();  // No nested Activity identities.
                        std::vector<dx::VmObjectRef> tokens;
                        for (const auto& record : Activities(context).stack)
                            if (record.caller == call.arguments[0].ref && record.request == call.arguments[2].AsInt())
                                tokens.push_back(record.token);
                        const auto root = call.vm.ProtectReferences(tokens);
                        for (const auto token : tokens)
                            static_cast<void>(CallAndroidMethod(call.vm, call.receiver, "finishActivity",
                                                                "(Landroid/os/IBinder;ILandroid/content/Intent;)Z",
                                                                {V::Ref(token), V::Int(0), V::Ref(dx::VmObjectRef{})}));
                        return V::Void();
                    });
    for (const auto* name : {"moveTaskBackwards", "moveTaskToBack"}) {
        b.VirtualMethod(name, "(I)V", [context](dx::IntrinsicContext& call) {
            RequireServicePermission(context, "REORDER_TASKS");
            auto& state = Activities(context);
            const auto task = call.arguments[0].AsInt();
            const auto record =
                std::ranges::find_if(state.stack, [task](const auto& entry) { return entry.task == task; });
            if (record != state.stack.end())
                static_cast<void>(CallAndroidMethod(call.vm, call.receiver, "moveActivityTaskToBack",
                                                    "(Landroid/os/IBinder;Z)Z", {V::Ref(record->token), V::Int(1)}));
            return V::Void();
        });
    }
    b.VirtualMethod("moveTaskToFront", "(IILandroid/os/Bundle;)V", [context](dx::IntrinsicContext& call) {
        RequireServicePermission(context, "REORDER_TASKS");
        auto& state = Activities(context);
        const auto task = call.arguments[0].AsInt();
        if (std::ranges::none_of(state.stack, [task](const auto& record) { return record.task == task; }))
            return V::Void();
        if (state.stack.back().task != task && state.stack.back().resumed) Pause(call.vm, state, state.stack.back());
        state.background_tasks.erase(task);
        std::stable_partition(state.stack.begin(), state.stack.end(),
                              [task](const auto& record) { return record.task != task; });
        Resume(call.vm, context, state);
        return V::Void();
    });
}

void PumpAndroidHostRequests(dx::Interpreter& vm, const Context& context) {
    std::deque<DexVmAndroidContext::HostActivityRequest> activities;
    std::deque<std::pair<std::string, bool>> text;
    bool media{};
    {
        std::scoped_lock lock(context->scheduler_mutex);
        activities.swap(context->host_activity_requests);
        text.swap(context->host_text_events);
        media = std::exchange(context->media_tick_pending, false);
    }
    using Kind = DexVmAndroidContext::HostActivityRequest::Kind;
    for (const auto& request : activities) {
        switch (request.kind) {
        case Kind::launch: StartRomLauncher(vm, context, request.descriptor, request.component); break;
        case Kind::suspend: RequestRomActivitySuspend(vm, context, true); break;
        case Kind::resume: RequestRomActivitySuspend(vm, context, false); break;
        case Kind::shutdown: RequestRomActivityExit(vm, context); break;
        }
    }
    ApplyOrientation(vm, context);
    if (context->rom_activities && context->rom_activities->exiting && context->activities_destroyed.load() &&
        !context->local_services_stopping)
        ShutdownLocalServices(vm, *context);
    for (const auto& [value, composing] : text)
        CommitRomInputText(vm, context, vm.NewStringUtf8(value), composing);
    if (media) {
        DispatchAndroidSurfaceStreamEvents(vm, *context);
        if (const auto error = PumpAndroidAudioTracks(vm, *context)) throw std::runtime_error(*error);
        if (const auto error = PumpVideoViews(vm, *context)) throw std::runtime_error(*error);
    }
}

}  // namespace ogplay::runtime::android_intrinsics

namespace ogplay::runtime {
namespace {
void WakeMainQueueLocked(DexVmAndroidContext& context) {
    for (const auto& [_, queue] : context.message_queues)
        if (queue->context_token == 1U) queue->wake = true;
}
}  // namespace

void QueueAndroidActivityRequest(DexVmAndroidContext& context, DexVmAndroidContext::HostActivityRequest request) {
    {
        std::scoped_lock lock(context.scheduler_mutex);
        context.host_activity_requests.push_back(std::move(request));
        WakeMainQueueLocked(context);
    }
    NotifyAndroidScheduler(context);
}

void QueueAndroidTextEvent(DexVmAndroidContext& context, std::string text, bool composing) {
    {
        std::scoped_lock lock(context.scheduler_mutex);
        if (context.host_text_events.size() >= 128U) throw std::length_error("host text input queue exhausted");
        context.host_text_events.emplace_back(std::move(text), composing);
        WakeMainQueueLocked(context);
    }
    NotifyAndroidScheduler(context);
}

void QueueAndroidMediaTick(DexVmAndroidContext& context) {
    {
        std::scoped_lock lock(context.scheduler_mutex);
        context.media_tick_pending = true;
        WakeMainQueueLocked(context);
    }
    NotifyAndroidScheduler(context);
}

bool AndroidMainQueueIdle(DexVmAndroidContext& context) {
    std::scoped_lock lock(context.scheduler_mutex);
    const auto now = context.uptime_millis.load();
    const auto queue = std::ranges::find_if(context.message_queues,
                                            [](const auto& entry) { return entry.second->context_token == 1U; });
    if (queue == context.message_queues.end() || !queue->second->idling || queue->second->wake ||
        now >= queue->second->poll_deadline_millis || now >= context.toast_deadline_millis.load())
        return false;
    return std::ranges::none_of(context.scheduled_work, [&](const auto& work) {
        return work.looper == context.main_looper && work.deadline_millis <= now;
    });
}
}  // namespace ogplay::runtime
