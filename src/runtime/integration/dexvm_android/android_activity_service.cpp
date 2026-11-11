#include "rom_activity.h"
#include "rom_window.h"

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

void Stop(dx::Interpreter& vm, const Context& context, RomActivityState& s, Record& r) {
    if (r.stopping || r.stopped) return;
    r.stopping = true;
    r.visible = false;
    SetRomApplicationVisibility(vm, context, r.token, false);
    Send(vm, s, "scheduleWindowVisibility", "(Landroid/os/IBinder;Z)V", {V::Ref(r.token), V::Int(0)});
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

void Resume(dx::Interpreter& vm, RomActivityState& s) {
    if (s.suspended || s.exiting || s.stack.empty()) return;
    auto& top = s.stack.back();
    if (s.background_tasks.contains(top.task)) return;
    if (top.finishing || top.destroying || top.resumed || top.launching || top.pausing) return;
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
        if (cmd.launch_mode == 2)
            for (auto i = std::next(reuse); i != s.stack.end(); ++i)
                if (i->task == task) {
                    i->finishing = true;
                    Stop(vm, context, s, *i);
                    Destroy(vm, s, *i);
                }
        const auto list = List(vm);
        const auto root = vm.ProtectReferences(std::array{list});
        Append(vm, list, cmd.intent);
        Send(vm, s, "scheduleNewIntent", "(Ljava/util/List;Landroid/os/IBinder;)V", {V::Ref(list), V::Ref(token)});
        // Keep task groups contiguous while bringing the destination task to front.
        std::stable_partition(s.stack.begin(), s.stack.end(), [&](const auto& r) { return r.task != task; });
        const auto current = std::ranges::find_if(s.stack, [&](const auto& r) { return r.token == token; });
        std::rotate(current, std::next(current), s.stack.end());
        Resume(vm, s);
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
    s.stack.push_back(std::move(r));
    const auto resources = [&] {
        dx::IntrinsicContext call{vm, token, {}};
        return ResourceObject(call, context, false);
    }();
    const auto config =
        CallAndroidMethod(vm, resources, "getConfiguration", "()Landroid/content/res/Configuration;").ref;
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
    if (context->rom_window_services_enabled) Activities(context).application_thread = thread;
}

void VisitRomActivityRoots(const DexVmAndroidContext& context, const dx::VmRootVisitor& visit) {
    if (!context.rom_activities) return;
    const auto root = [&](Ref ref) {
        if (ref.IsValid()) visit(ref);
    };
    const auto& s = *context.rom_activities;
    root(s.application_thread);
    for (const auto& r : s.stack)
        for (const auto ref : {r.token, r.intent, r.info, r.component, r.caller, r.result_data})
            root(ref);
    for (const auto& cmd : s.pending) {
        root(cmd.owner);
        root(cmd.intent);
    }
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
        Resume(vm, s);
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
    for (auto& r : s.stack) {
        r.finishing = true;
        if (r.resumed || r.launching)
            Pause(vm, s, r);
        else {
            Stop(vm, context, s, r);
            Destroy(vm, s, r);
        }
    }
    s.exit_requested = s.stack.empty();
}

RomActivityExitStatus ReadRomActivityExitStatus(const Context& context) {
    if (!context->rom_activities || context->rom_activities->exit_requested) return RomActivityExitStatus::complete;
    const auto& s = *context->rom_activities;
    return s.exiting && context->uptime_millis.load() >= s.exit_deadline_millis
               ? RomActivityExitStatus::deadline_expired
               : RomActivityExitStatus::waiting;
}

void AddRomActivityMethods(dx::IntrinsicClassBuilder& b, const Context& context) {
    if (!context->rom_window_services_enabled) return;
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
                        if (r->resumed || r->launching)
                            Pause(c.vm, s, *r);
                        else {
                            Stop(c.vm, context, s, *r);
                            Destroy(c.vm, s, *r);
                        }
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
            r->visible = true;
            SetRomApplicationVisibility(c.vm, context, r->token, true);
        }
        for (auto& r : s.stack)
            if (r.token != c.arguments[0].ref && r.paused && !r.stopped && !r.destroying) Stop(c.vm, context, s, r);
        if (s.exiting || s.suspended || !s.pending.empty() ||
            (Find(s, c.arguments[0].ref) && s.background_tasks.contains(Find(s, c.arguments[0].ref)->task))) {
            if (auto* r = Find(s, c.arguments[0].ref)) Pause(c.vm, s, *r);
        }
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
        if (s.pending.empty()) Resume(c.vm, s);
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
                        return V::Void();
                    });
    b.VirtualMethod("activityDestroyed", "(Landroid/os/IBinder;)V", [context](dx::IntrinsicContext& c) {
        auto& s = Activities(context);
        std::erase_if(s.stack, [&](const auto& r) { return r.token == c.arguments[0].ref; });
        if (s.stack.empty() && s.pending.empty())
            s.exit_requested = true;
        else
            Resume(c.vm, s);
        Drain(c.vm, context);
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
    b.VirtualMethod("setRequestedOrientation", "(Landroid/os/IBinder;I)V", [context](dx::IntrinsicContext& c) {
        if (auto* r = Find(Activities(context), c.arguments[0].ref)) r->orientation = c.arguments[1].AsInt();
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
        if (!s.stack.empty() && s.stack.back().task != task) Resume(c.vm, s);
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
}
}  // namespace ogplay::runtime::android_intrinsics
