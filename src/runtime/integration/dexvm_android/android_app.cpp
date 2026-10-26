// DVM-80: API-family translation unit. Physical consolidation only.

// ---- migrated from android_app_Activity.cpp ----
// Activity handlers. setContentView(int) inflates the real binary XML
// layout through the registry inflater into the UiTree;
// presentation-only calls stay no-ops.

#include "ogplay/loader/binary_xml.h"
#include "ogplay/runtime/integration/native_activity_runtime.h"

#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

namespace {
void RequireNotificationPackage(dx::IntrinsicContext& call, const Context& context) {
    if (!call.arguments[0].ref.IsValid())
        throw dx::VmJavaThrow{"Ljava/lang/NullPointerException;", "packageName"};
    if (call.vm.StringUtf8(call.arguments[0].ref) != context->package_name) {
        if (auto* ledger = call.vm.Ledger()) ledger->RecordUnimplemented("dexvm.notification_cancel", 0);
        throw dx::VmJavaThrow{"Ljava/lang/SecurityException;", "notification access is limited to the current application"};
    }
}
}

Decl Declare_android_app_Notification(const Context&) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/app/Notification;");
    builder.StaticMethod("nativeUnsupported", "(Ljava/lang/String;)V",
        [](dx::IntrinsicContext& call) -> dx::VmValue {
            const auto operation = dx::IntrinsicCall(call).NonNullRef(0, "operation");
            const auto name = call.vm.StringUtf8(operation);
            const bool template_request = name == "system_template";
            if (!template_request && name != "parcel")
                throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "unknown notification operation"};
            if (auto* ledger = call.vm.Ledger()) ledger->RecordUnimplemented(
                template_request ? "dexvm.notification_template" : "dexvm.notification_parcel", 0);
            throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;",
                template_request ? "Android system notification templates are not available"
                                 : "notification parcel transport is not supported"};
        }, dx::kAccPrivate | dx::kAccNative);
    return std::move(builder).Build();
}

Decl Declare_android_app_ActivityManager(const Context&) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/app/ActivityManager;");
    builder.StaticMethod("staticGetMemoryClass", "()I", [](dx::IntrinsicContext& c) { return dx::VmValue::Int(static_cast<std::int32_t>(c.vm.Model().HeapGrowthLimitBytes() / (1024U * 1024U))); });
    builder.StaticMethod("staticGetLargeMemoryClass", "()I", [](dx::IntrinsicContext& c) { return dx::VmValue::Int(static_cast<std::int32_t>(c.vm.Model().MaximumHeapBytes() / (1024U * 1024U))); });
    builder.VirtualMethod("getMemoryClass", "()I", [](dx::IntrinsicContext& c) { return dx::VmValue::Int(static_cast<std::int32_t>(c.vm.Model().HeapGrowthLimitBytes() / (1024U * 1024U))); });
    builder.VirtualMethod("getLargeMemoryClass", "()I", [](dx::IntrinsicContext& c) { return dx::VmValue::Int(static_cast<std::int32_t>(c.vm.Model().MaximumHeapBytes() / (1024U * 1024U))); });
    builder.StaticMethod("nativeUnsupported", "(Ljava/lang/String;)Ljava/lang/UnsupportedOperationException;",
        [](dx::IntrinsicContext& call) -> dx::VmValue {
            static_cast<void>(dx::IntrinsicCall(call).NonNullRef(0, "operation"));
            if (auto* ledger = call.vm.Ledger()) ledger->RecordUnimplemented("dexvm.activity_process_query", 0);
            throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;", "ActivityManager process enumeration is not provided"};
        }, dx::kAccPrivate | dx::kAccNative);
    return std::move(builder).Build();
}

Decl Declare_android_app_ActivityManager_MemoryInfo(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/app/ActivityManager$MemoryInfo;");
    const std::array fields{
        builder.BoundInstanceField("availMem", "J", dx::kAccPublic),
        builder.BoundInstanceField("totalMem", "J", dx::kAccPublic),
        builder.BoundInstanceField("threshold", "J", dx::kAccPublic),
        builder.BoundInstanceField("hiddenAppThreshold", "J", dx::kAccPublic),
        builder.BoundInstanceField("secondaryServerThreshold", "J", dx::kAccPublic),
        builder.BoundInstanceField("visibleAppThreshold", "J", dx::kAccPublic),
        builder.BoundInstanceField("foregroundAppThreshold", "J", dx::kAccPublic)};
    builder.DirectMethod("nativeRead", "()V", [context, fields](dx::IntrinsicContext& call) {
        if (!context->memory_snapshot) {
            if (auto* ledger = call.vm.Ledger()) ledger->RecordUnimplemented("dexvm.activity_memory_query", 0);
            throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;", "guest memory facts are not installed"};
        }
        const auto& memory = *context->memory_snapshot;
        const auto& policy = memory.pressure;
        const std::array<std::uint64_t, 7> values{memory.AvailableBytes(), memory.TotalBytes(),
            static_cast<std::uint64_t>(policy.home_kb) * 1024U,
            static_cast<std::uint64_t>(policy.cached_kb) * 1024U,
            static_cast<std::uint64_t>(policy.service_kb) * 1024U,
            static_cast<std::uint64_t>(policy.visible_kb) * 1024U,
            static_cast<std::uint64_t>(policy.foreground_kb) * 1024U};
        const dx::IntrinsicCall api(call);
        for (std::size_t i = 0; i < fields.size(); ++i)
            api.SetLong(fields[i], static_cast<std::int64_t>(values[i]));
        return dx::VmValue::Void();
    }, dx::kAccPrivate | dx::kAccNative);
    return std::move(builder).Build();
}

dx::VmObjectRef ActivityManagerForContext(dx::IntrinsicContext& call, const Context& context) {
    const auto found = context->singletons.find("activity");
    if (found != context->singletons.end()) return found->second;
    auto& vm = call.vm;
    const auto type = vm.Linker().ResolveDescriptor("Landroid/app/ActivityManager;");
    const auto require = [&](const dx::VmCallOutcome& outcome) {
        if (outcome.exception.IsValid())
            throw dx::VmJavaThrow{vm.Linker().Class(outcome.exception_class).descriptor,
                                  outcome.exception_message, outcome.exception};
    };
    require(vm.EnsureClassInitialized(type));
    const auto object = vm.NewIntrinsicInstance("Landroid/app/ActivityManager;");
    const auto owner = context->application_base_context.IsValid() ? context->application_base_context : call.receiver;
    const auto roots = vm.ProtectReferences(std::array{object, owner});
    const auto constructor = vm.Linker().FindDirectMethod(type, "<init>", "(Landroid/content/Context;Landroid/os/Handler;)V");
    if (!constructor) throw dx::DexVmError(dx::DexVmErrorReason::unresolved_reference, "ActivityManager constructor is missing");
    require(vm.Call(*constructor, std::array{dx::VmValue::Ref(object), dx::VmValue::Ref(owner), dx::VmValue::Ref(dx::VmObjectRef{})}));
    context->singletons.emplace("activity", object);
    return object;
}

Decl Declare_android_app_NotificationManager(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/app/NotificationManager;");
    builder.DirectMethod("nativeCancel", "(Ljava/lang/String;Ljava/lang/String;IZ)V",
        [context](dx::IntrinsicContext& call) {
            RequireNotificationPackage(call, context);
            // Publication is rejected by the only post boundary. No other
            // producer or persisted inventory exists, so the set is known empty.
            // Cancellation must not invalidate an unrelated PendingIntent.
            return dx::VmValue::Void();
        }, dx::kAccPrivate | dx::kAccNative);
    builder.DirectMethod("nativeRejectPost", "(Ljava/lang/String;)V",
        [context](dx::IntrinsicContext& call) -> dx::VmValue {
            RequireNotificationPackage(call, context);
            if (auto* ledger = call.vm.Ledger()) ledger->RecordUnimplemented("dexvm.notification_post", 0);
            throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;", "notification publication is disabled"};
        }, dx::kAccPrivate | dx::kAccNative);
    return std::move(builder).Build();
}

dx::VmObjectRef NotificationManagerForContext(dx::IntrinsicContext& call, const Context& context) {
    const auto package = CallAndroidMethod(call.vm, call.receiver, "getPackageName", "()Ljava/lang/String;").ref;
    if (!package.IsValid() || call.vm.StringUtf8(package) != context->package_name) {
        if (auto* ledger = call.vm.Ledger()) ledger->RecordUnimplemented("dexvm.notification_cancel", 0);
        throw dx::VmJavaThrow{"Ljava/lang/SecurityException;", "notification manager requires the current application context"};
    }
    const auto found = context->singletons.find("notification");
    if (found != context->singletons.end()) return found->second;
    auto& vm = call.vm;
    const auto type = vm.Linker().ResolveDescriptor("Landroid/app/NotificationManager;");
    const auto require = [&](const dx::VmCallOutcome& outcome) {
        if (outcome.exception.IsValid())
            throw dx::VmJavaThrow{vm.Linker().Class(outcome.exception_class).descriptor,
                                  outcome.exception_message, outcome.exception};
    };
    require(vm.EnsureClassInitialized(type));
    const auto object = vm.NewIntrinsicInstance("Landroid/app/NotificationManager;");
    const auto owner = context->application_base_context.IsValid() ? context->application_base_context : call.receiver;
    const auto roots = vm.ProtectReferences(std::array{object, owner});
    const auto constructor = vm.Linker().FindDirectMethod(type, "<init>", "(Landroid/content/Context;Landroid/os/Handler;)V");
    if (!constructor)
        throw dx::DexVmError(dx::DexVmErrorReason::unresolved_reference, "NotificationManager constructor is missing");
    require(vm.Call(*constructor, std::array{dx::VmValue::Ref(object), dx::VmValue::Ref(owner), dx::VmValue::Ref(dx::VmObjectRef{})}));
    context->singletons.emplace("notification", object);
    return object;
}

Decl Declare_android_app_KeyguardManager(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class(
        "Landroid/app/KeyguardManager;", "Ljava/lang/Object;");
    const auto state = [context] {
        return context->keyguard_state_provider
                   ? context->keyguard_state_provider()
                   : AndroidKeyguardState{};
    };
    builder.VirtualMethod(
        "isKeyguardLocked", "()Z",
        [state](dx::IntrinsicContext&) {
            return dx::VmValue::Int(state().locked ? 1 : 0);
        });
    builder.VirtualMethod(
        "isKeyguardSecure", "()Z",
        [state](dx::IntrinsicContext&) {
            return dx::VmValue::Int(state().secure ? 1 : 0);
        });
    builder.VirtualMethod(
        "inKeyguardRestrictedInputMode", "()Z",
        [state](dx::IntrinsicContext&) {
            return dx::VmValue::Int(state().restricted_input ? 1 : 0);
        });
    return std::move(builder).Build();
}

Decl Declare_android_app_backup_BackupManager(const Context&) {
    auto builder = dx::IntrinsicClassBuilder::Class(
        "Landroid/app/backup/BackupManager;");
    const auto service = builder.BoundStaticField(
        "sService", "Landroid/app/backup/IBackupManager;", dx::kAccPrivate);
    // This process has no Android backup service. API 19 Java owns the
    // absent-service returns; no Binder lookup or backup job is simulated.
    builder.StaticMethod("checkServiceBinder", "()V",
        [service](dx::IntrinsicContext& call) {
            if (auto* ledger = call.vm.Ledger())
                ledger->RecordUnimplemented("dexvm.backup_service", 0);
            if (dx::IntrinsicCall(call).GetRef(service).IsValid())
                throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;",
                                      "external backup service is unavailable"};
            return dx::VmValue::Void();
        }, dx::kAccPrivate);
    return std::move(builder).Build();
}

Decl Declare_android_app_Application(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/app/Application;", "Landroid/content/ContextWrapper;",
                                                    {"Landroid/content/ComponentCallbacks2;"});
    builder.Constructor("()V", [](dx::IntrinsicContext&) {
        return dx::VmValue::Void();
    });
    builder.VirtualMethod("onCreate", "()V", [](dx::IntrinsicContext&) {
        return dx::VmValue::Void();
    });
    return std::move(builder).Build();
}

Decl Declare_android_app_Activity(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class(
        "Landroid/app/Activity;", "Landroid/view/ContextThemeWrapper;");
    const auto component = builder.BoundInstanceField(
        "mComponent", "Landroid/content/ComponentName;", dx::kAccPrivate);
    const auto intent =
        builder.BoundInstanceField("mIntent", "Landroid/content/Intent;", 0);
    builder.VirtualMethod("getComponentName", "()Landroid/content/ComponentName;",
                          [component](dx::IntrinsicContext& call) {
                              return dx::VmValue::Ref(
                                  dx::IntrinsicCall(call).GetRef(component));
                          });
    builder.VirtualMethod(
        "getLocalClassName", "()Ljava/lang/String;",
        [component](dx::IntrinsicContext& call) {
            const auto package =
                CallAndroidMethod(call.vm, call.receiver, "getPackageName",
                                  "()Ljava/lang/String;")
                    .ref;
            const auto roots = call.vm.ProtectReferences(std::array{package});
            const auto name =
                CallAndroidMethod(call.vm, dx::IntrinsicCall(call).GetRef(component),
                                  "getClassName", "()Ljava/lang/String;")
                    .ref;
            if (!package.IsValid()) {
                throw dx::VmJavaThrow{"Ljava/lang/NullPointerException;",
                                      "Activity package name is null"};
            }
            const auto pkg = call.vm.Model().StringValue(package);
            const auto cls = call.vm.Model().StringValue(name);
            if (!cls.starts_with(pkg) || cls.size() <= pkg.size() ||
                cls[pkg.size()] != '.')
                return dx::VmValue::Ref(name);
            return dx::VmValue::Ref(call.vm.Model().NewString(cls.substr(pkg.size() + 1)));
        });
    builder.VirtualMethod(
        "getPreferences", "(I)Landroid/content/SharedPreferences;",
        [](dx::IntrinsicContext& call) {
            const auto name =
                CallAndroidMethod(call.vm, call.receiver, "getLocalClassName",
                                  "()Ljava/lang/String;")
                    .ref;
            return CallAndroidMethod(
                call.vm, call.receiver, "getSharedPreferences",
                "(Ljava/lang/String;I)Landroid/content/SharedPreferences;",
                {dx::VmValue::Ref(name), call.arguments[0]});
        });
    // OGPlay only creates top-level application activities; embedded child
    // activities and ActivityGroup are outside the process compatibility boundary.
    builder.VirtualMethod(
        "isChild", "()Z",
        [](dx::IntrinsicContext&) { return dx::VmValue::Int(0); },
        dx::kAccPublic | dx::kAccFinal);
    const auto lifecycle_noop = dx::IntrinsicHandler(
        [](dx::IntrinsicContext&) { return dx::VmValue::Void(); });
    builder.VirtualMethod("onLowMemory", "()V", lifecycle_noop);
    builder.VirtualMethod("onSaveInstanceState", "(Landroid/os/Bundle;)V", lifecycle_noop, dx::kAccProtected);
    builder.VirtualMethod("onCreate", "(Landroid/os/Bundle;)V", lifecycle_noop,
                          dx::kAccProtected);
    builder.VirtualMethod("onStart", "()V", lifecycle_noop,
                          dx::kAccProtected);
    builder.VirtualMethod("onRestart", "()V", lifecycle_noop,
                          dx::kAccProtected);
    builder.VirtualMethod("onResume", "()V", lifecycle_noop,
                          dx::kAccProtected);
    builder.VirtualMethod("onPause", "()V", lifecycle_noop,
                          dx::kAccProtected);
    builder.VirtualMethod("onStop", "()V", lifecycle_noop,
                          dx::kAccProtected);
    builder.VirtualMethod("onWindowFocusChanged", "(Z)V", lifecycle_noop);
    builder.VirtualMethod("hasWindowFocus", "()Z",
        [context](dx::IntrinsicContext& call) {
            return dx::VmValue::Int(
                context->window_has_focus.load() &&
                context->window_focus_activity.load() ==
                    call.receiver.Value());
        });
    builder.VirtualMethod("onConfigurationChanged",
        "(Landroid/content/res/Configuration;)V", lifecycle_noop);
    const auto window = builder.BoundInstanceField("mWindow", "Landroid/view/Window;", dx::kAccPrivate);
    context->ui_java_activity_window = window;
    builder.FinalMethod("getWindow", "()Landroid/view/Window;",
        [window](dx::IntrinsicContext& call) {
            dx::IntrinsicCall fields(call);
            auto value = fields.GetRef(window);
            if (!value.IsValid()) {
                value = call.vm.NewIntrinsicInstance("Landroid/view/Window;");
                const auto owner = call.vm.Linker().FindFieldRecursive(call.vm.Model().ObjectClass(value),
                    "mOgplayOwner", "Landroid/app/Activity;");
                if (!owner) throw dx::DexVmError(dx::DexVmErrorReason::internal_invariant, "Window owner field is missing");
                call.vm.Model().InstanceSlots(value)[call.vm.Linker().Field(*owner).slot] = {call.receiver.Value(), dx::SlotTag::ref};
                fields.SetRef(window, value);
            }
            return dx::VmValue::Ref(value);
        });
    builder.FinalMethod("getApplication", "()Landroid/app/Application;",
        [context](dx::IntrinsicContext&) {
            return dx::VmValue::Ref(context->application);
        });
    builder.FinalMethod("requestWindowFeature", "(I)Z",
        [](dx::IntrinsicContext&) { return dx::VmValue::Int(1); });
    builder.VirtualMethod("onContentChanged", "()V", lifecycle_noop);
    const auto content_delegate = [](const char* name, const char* signature) {
        return dx::IntrinsicHandler([name, signature](dx::IntrinsicContext& call) {
            const auto window = CallAndroidMethod(call.vm, call.receiver,
                "getWindow", "()Landroid/view/Window;").ref;
            return CallAndroidMethod(call.vm, window, name, signature,
                std::vector<dx::VmValue>{call.arguments.begin(), call.arguments.end()});
        });
    };
    builder.VirtualMethod("setContentView", "(I)V", content_delegate("setContentView", "(I)V"));
    builder.VirtualMethod("setContentView", "(Landroid/view/View;)V",
        content_delegate("setContentView", "(Landroid/view/View;)V"));
    builder.VirtualMethod("setContentView", "(Landroid/view/View;Landroid/view/ViewGroup$LayoutParams;)V",
        content_delegate("setContentView", "(Landroid/view/View;Landroid/view/ViewGroup$LayoutParams;)V"));
    builder.VirtualMethod("addContentView", "(Landroid/view/View;Landroid/view/ViewGroup$LayoutParams;)V",
        content_delegate("addContentView", "(Landroid/view/View;Landroid/view/ViewGroup$LayoutParams;)V"));
    builder.VirtualMethod("findViewById", "(I)Landroid/view/View;",
        content_delegate("findViewById", "(I)Landroid/view/View;"));
    builder.VirtualMethod("getIntent", "()Landroid/content/Intent;",
                          [intent](dx::IntrinsicContext& call) {
                              return dx::VmValue::Ref(
                                  dx::IntrinsicCall(call).GetRef(intent));
                          });
    builder.VirtualMethod("setIntent", "(Landroid/content/Intent;)V",
                          [intent](dx::IntrinsicContext& call) {
                              dx::IntrinsicCall(call).SetRef(intent,
                                                             call.arguments[0].ref);
                              return dx::VmValue::Void();
                          });
    // API19's default onNewIntent is an empty application callback.
    builder.VirtualMethod("onNewIntent", "(Landroid/content/Intent;)V",
        [](dx::IntrinsicContext&) { return dx::VmValue::Void(); }, dx::kAccProtected);
    // API 19 Activity runs inline only on the main thread. Calls from a guest
    // worker (notably GLSurfaceView.GLThread) are posted to the main Looper and
    // dispatched by the lifecycle safe-point pump.
    builder.FinalMethod("runOnUiThread", "(Ljava/lang/Runnable;)V",
        [context](dx::IntrinsicContext& call) {
            const auto runnable = call.arguments[0].ref;
            if (!runnable.IsValid()) {
                throw dx::VmJavaThrow{"Ljava/lang/NullPointerException;",
                                      "runOnUiThread action is null"};
            }
            if (call.vm.CurrentContextToken() != 1U) {
                const auto main = EnsureMainLooper(call, context);
                if (!EnqueueHandlerWork(
                        context, main, call.receiver, runnable,
                        dx::VmObjectRef{}, 0, true,
                        context->uptime_millis.load())) {
                    throw dx::VmJavaThrow{
                        "Ljava/lang/IllegalStateException;",
                        "main Looper is not accepting work"};
                }
                return dx::VmValue::Void();
            }
            auto& vm = call.vm;
            auto& linker = vm.Linker();
            const auto runnable_class = vm.Model().ObjectClass(runnable);
            const auto index =
                linker.FindVtableIndex(runnable_class, "run", "()V");
            if (!index.has_value()) {
                throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;",
                    "runOnUiThread target has no run() method"};
            }
            const auto outcome =
                vm.Call(linker.Class(runnable_class).vtable[*index],
                        std::vector<dx::VmValue>{dx::VmValue::Ref(runnable)});
            if (outcome.exception.IsValid()) {
                throw dx::VmJavaThrow{
                    linker.Class(outcome.exception_class).descriptor,
                    outcome.exception_message, outcome.exception};
            }
            return dx::VmValue::Void();
        });
    builder.FinalMethod("setVolumeControlStream", "(I)V",
        [](dx::IntrinsicContext&) { return dx::VmValue::Void(); });
    builder.FinalMethod("setRequestedOrientation", "(I)V",
        [context](dx::IntrinsicContext& call) {
            context->requested_orientations[call.receiver.Value()] =
                call.arguments[0].AsInt();
            return dx::VmValue::Void();
        });
    builder.FinalMethod("getRequestedOrientation", "()I",
        [context](dx::IntrinsicContext& call) {
            const auto found = context->requested_orientations.find(
                call.receiver.Value());
            return dx::VmValue::Int(
                found == context->requested_orientations.end()
                    ? -1
                    : found->second);
        });
    const auto on_key_false = dx::IntrinsicHandler(
        [](dx::IntrinsicContext&) { return dx::VmValue::Int(0); });
    builder.VirtualMethod("onKeyDown", "(ILandroid/view/KeyEvent;)Z",
        on_key_false);
    builder.VirtualMethod("onKeyUp", "(ILandroid/view/KeyEvent;)Z",
        on_key_false);
    builder.VirtualMethod("onUserInteraction", "()V", lifecycle_noop);
    builder.VirtualMethod("dispatchTouchEvent", "(Landroid/view/MotionEvent;)Z",
        [](dx::IntrinsicContext& call) {
            const auto event = call.arguments[0];
            if (CallAndroidMethod(call.vm, event.ref, "getAction", "()I").AsInt() == 0)
                static_cast<void>(CallAndroidMethod(call.vm, call.receiver, "onUserInteraction", "()V"));
            const auto window = CallAndroidMethod(call.vm, call.receiver, "getWindow", "()Landroid/view/Window;").ref;
            if (CallAndroidMethod(call.vm, window, "superDispatchTouchEvent", "(Landroid/view/MotionEvent;)Z", {event}).AsInt())
                return dx::VmValue::Int(1);
            return CallAndroidMethod(call.vm, call.receiver, "onTouchEvent", "(Landroid/view/MotionEvent;)Z", {event});
        });
    builder.VirtualMethod("onTouchEvent", "(Landroid/view/MotionEvent;)Z",
        [](dx::IntrinsicContext&) { return dx::VmValue::Int(0); });
    builder.DirectMethod("nativeStartActivityForResult",
        "(Landroid/content/Intent;ILandroid/os/Bundle;)V",
        [context](dx::IntrinsicContext& call) -> dx::VmValue {
            const auto reject = [&] {
                if (auto* ledger = call.vm.Ledger()) ledger->RecordUnimplemented("dexvm.activity_result", 0);
                throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;",
                                      "Activity result launch requires the active caller, no options and standard flags"};
            };
            if (call.arguments[2].ref.IsValid() ||
                (context->activity.IsValid() && call.receiver != context->activity)) reject();
            if (call.arguments[1].AsInt() >= 0 && (context->renderer.IsValid() ||
                !context->active_surface_holders.empty() || context->window_surface_callback.IsValid() ||
                AnyVideoPlaying(*context))) reject();
            if (!call.arguments[0].ref.IsValid())
                throw dx::VmJavaThrow{"Ljava/lang/NullPointerException;", "intent is null"};
            if (CallAndroidMethod(call.vm, call.arguments[0].ref, "getFlags", "()I").AsInt() != 0) reject();
            return StartAndroidActivity(call, context, call.arguments[1].AsInt());
        }, dx::kAccPrivate | dx::kAccNative);
    builder.DirectMethod("nativeFinish", "(ILandroid/content/Intent;)V",
        [context](dx::IntrinsicContext& call) {
            if (context->process_stopping)
                throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;", "process is stopping"};
            context->finishing_activity = call.receiver.Value();
            if (std::ranges::any_of(context->activity_stack, [&](const auto& record) { return record.object == call.receiver; }) &&
                !std::ranges::any_of(context->activity_commands, [&](const auto& command) {
                    return command.kind == DexVmAndroidContext::ActivityCommand::Kind::finish && command.owner == call.receiver;
                })) {
                context->activity_commands.push_back({DexVmAndroidContext::ActivityCommand::Kind::finish,
                    call.receiver, call.arguments[1].ref, {}, {}, -1, call.arguments[0].AsInt()});
                context->activity_switch_pending = true;
            }
            return dx::VmValue::Void();
        }, dx::kAccPrivate | dx::kAccNative);
    // Task groups have independent roots; foreground ordering is not task identity.
    builder.FinalMethod("isTaskRoot", "()Z",
        [context](dx::IntrinsicContext& call) {
            const auto found = std::find_if(context->activity_stack.begin(), context->activity_stack.end(),
                [&](const auto& record) { return record.object == call.receiver; });
            if (found == context->activity_stack.end())
                return dx::VmValue::Int(0);
            const auto root = std::find_if(context->activity_stack.begin(), context->activity_stack.end(),
                [&](const auto& record) { return record.task_id == found->task_id; });
            return dx::VmValue::Int(root == found ? 1 : 0);
        });
    builder.FinalMethod("getTaskId", "()I", [context](dx::IntrinsicContext& call) {
        const auto found = std::find_if(context->activity_stack.begin(), context->activity_stack.end(),
            [&](const auto& record) { return record.object == call.receiver; });
        if (found == context->activity_stack.end())
            return dx::VmValue::Int(-1); // API19 getTaskForActivity: no live token.
        return dx::VmValue::Int(found->task_id);
    });
    builder.FinalMethod("getWindowManager", "()Landroid/view/WindowManager;",
        [context](dx::IntrinsicContext& call) {
            return dx::VmValue::Ref(
                Singleton(call, context, "window_manager",
                          "Landroid/view/WindowManagerImpl;"));
        });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics


// ---- migrated from android_app_AlertDialog_Builder.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_app_AlertDialog_Builder(const Context&) {
    return std::move(dx::IntrinsicClassBuilder::Class("Landroid/app/AlertDialog$Builder;")).Build();
}
Decl Declare_android_app_AlertDialog(const Context&) {
    return std::move(dx::IntrinsicClassBuilder::Class("Landroid/app/AlertDialog;", "Landroid/app/Dialog;")).Build();
}
Decl Declare_android_app_Dialog(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/app/Dialog;", "Ljava/lang/Object;", {"Landroid/content/DialogInterface;"});
    const auto generation = builder.BoundInstanceField("mGeneration", "J", dx::kAccPrivate);
    builder.DirectMethod("nativeShow", "(Landroid/content/Context;Landroid/app/Activity;Ljava/lang/CharSequence;Ljava/lang/CharSequence;[Ljava/lang/CharSequence;J)V",
        [context, generation](dx::IntrinsicContext& call) { ShowAndroidDialog(call, context, generation); return dx::VmValue::Void(); }, dx::kAccPrivate | dx::kAccNative);
    builder.DirectMethod("nativeDismiss", "()V", [context](dx::IntrinsicContext& call) {
        DismissAndroidDialog(*context, call.receiver); return dx::VmValue::Void();
    }, dx::kAccPrivate | dx::kAccNative);
    builder.DirectMethod("nativeVisible", "(Z)V", [context](dx::IntrinsicContext& call) {
        SetAndroidDialogVisible(*context, call.receiver, call.arguments[0].AsInt() != 0); return dx::VmValue::Void();
    }, dx::kAccPrivate | dx::kAccNative);
    builder.DirectMethod("nativeFocus", "(I)I", [context](dx::IntrinsicContext& call) {
        return dx::VmValue::Int(FocusAndroidDialog(*context, call.receiver, call.arguments[0].AsInt()));
    }, dx::kAccPrivate | dx::kAccNative);
    builder.StaticMethod("nativeUnsupported", "(Ljava/lang/String;)V", [](dx::IntrinsicContext& call) -> dx::VmValue {
        if (auto* ledger = call.vm.Ledger()) ledger->RecordUnimplemented("runtime.ui.dialog.presentation", 0);
        throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;", "unsupported Dialog option: " + call.vm.StringUtf8(call.arguments[0].ref)};
    }, dx::kAccStatic | dx::kAccNative);
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics


// ---- migrated from android_app_IntentService.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_app_IntentService(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class(
        "Landroid/app/IntentService;", "Landroid/app/Service;", {},
        dx::kAccPublic | dx::kAccAbstract);
    builder.Constructor("()V", NeutralHandler('V'));
    builder.VirtualMethod("onHandleIntent", "(Landroid/content/Intent;)V",
        [](dx::IntrinsicContext&) -> dx::VmValue {
            throw dx::VmJavaThrow{
                "Ljava/lang/UnsupportedOperationException;",
                "IntentService.onHandleIntent must be overridden"};
        }, dx::kAccProtected | dx::kAccAbstract);
    const auto wu_name = builder.BoundInstanceField("mName", "Ljava/lang/String;", dx::kAccPrivate);
    // AOSP 4.4 ROM line 76.
    builder.Constructor(
        "(Ljava/lang/String;)V",
        [wu_name](dx::IntrinsicContext& c) {
            const auto target = c.vm.Linker().FindDirectMethod(c.vm.Linker().ResolveDescriptor("Landroid/app/Service;"),
                                                               "<init>", "()V");
            const auto result = c.vm.Call(*target, std::vector<dx::VmValue>{dx::VmValue::Ref(c.receiver)});
            if (result.exception.IsValid())
                throw dx::VmJavaThrow{c.vm.Linker().Class(result.exception_class).descriptor, result.exception_message,
                                      result.exception};
            dx::IntrinsicCall(c).SetRef(wu_name, c.arguments[0].ref);
            return dx::VmValue::Void();
        },
        dx::kAccPublic);

    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics


namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_app_Service(const Context& context) {
    auto builder =
        dx::IntrinsicClassBuilder::Class("Landroid/app/Service;", "Landroid/content/ContextWrapper;",
                                         {"Landroid/content/ComponentCallbacks2;"}, dx::kAccPublic | dx::kAccAbstract);
    builder.Constructor("()V", [](dx::IntrinsicContext&) {
        return dx::VmValue::Void();
    });
    builder.ConstantInt("START_CONTINUATION_MASK", "I", 0x0f)
        .ConstantInt("START_STICKY_COMPATIBILITY", "I", 0)
        .ConstantInt("START_STICKY", "I", 1)
        .ConstantInt("START_NOT_STICKY", "I", 2)
        .ConstantInt("START_REDELIVER_INTENT", "I", 3)
        .ConstantInt("START_FLAG_REDELIVERY", "I", 1)
        .ConstantInt("START_FLAG_RETRY", "I", 2);
    const auto noop = dx::IntrinsicHandler(
        [](dx::IntrinsicContext&) { return dx::VmValue::Void(); });
    builder.FinalMethod("getApplication", "()Landroid/app/Application;",
        [context](dx::IntrinsicContext&) {
            return dx::VmValue::Ref(context->application);
        });
    builder.VirtualMethod("onCreate", "()V", noop)
        .VirtualMethod("onStart", "(Landroid/content/Intent;I)V", noop)
        .VirtualMethod("onStartCommand", "(Landroid/content/Intent;II)I",
            [](dx::IntrinsicContext&) { return dx::VmValue::Int(0); })
        .VirtualMethod("onDestroy", "()V", noop)
        .VirtualMethod("onConfigurationChanged",
            "(Landroid/content/res/Configuration;)V", noop)
        .VirtualMethod("onLowMemory", "()V", noop)
        .VirtualMethod("onTrimMemory", "(I)V", noop)
        .VirtualMethod("onTaskRemoved", "(Landroid/content/Intent;)V", noop)
        .VirtualMethod("onBind",
            "(Landroid/content/Intent;)Landroid/os/IBinder;",
            [](dx::IntrinsicContext&) -> dx::VmValue {
                throw dx::VmJavaThrow{
                    "Ljava/lang/UnsupportedOperationException;",
                    "Service.onBind must be overridden"};
            }, dx::kAccPublic | dx::kAccAbstract)
        .VirtualMethod("onUnbind", "(Landroid/content/Intent;)Z",
            [](dx::IntrinsicContext&) { return dx::VmValue::Int(0); })
        .VirtualMethod("onRebind", "(Landroid/content/Intent;)V", noop);
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

// ---- migrated from android_app_PendingIntent.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

namespace {
constexpr std::int32_t kOneShot = 1 << 30;
constexpr std::int32_t kNoCreate = 1 << 29;
constexpr std::int32_t kCancelCurrent = 1 << 28;
constexpr std::int32_t kUpdateCurrent = 1 << 27;
constexpr std::int32_t kControlFlags = kNoCreate | kCancelCurrent | kUpdateCurrent;
constexpr std::int32_t kIntentFillFlags = 0xff; // API19 FILL_IN_*.

[[noreturn]] void UnsupportedPending(dx::IntrinsicContext& call,
                                       const char* message) {
    if (auto* ledger = call.vm.Ledger())
        ledger->RecordUnimplemented("dexvm.pending_intent", 0);
    throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;", message};
}

[[nodiscard]] dx::VmObjectRef CopyPendingIntent(dx::Interpreter& vm,
                                                dx::VmObjectRef source) {
    const auto source_root = vm.ProtectReferences(std::array{source});
    const auto initialized = vm.EnsureClassInitialized(
        vm.Linker().ResolveDescriptor("Landroid/content/Intent;"));
    if (initialized.exception.IsValid())
        throw dx::VmJavaThrow{vm.Linker().Class(initialized.exception_class).descriptor,
                              initialized.exception_message, initialized.exception};
    const auto snapshot = vm.NewIntrinsicInstance("Landroid/content/Intent;");
    const auto roots = vm.ProtectReferences(std::array{snapshot});
    const auto init = vm.Linker().FindDirectMethod(vm.Model().ObjectClass(snapshot),
        "<init>", "(Landroid/content/Intent;)V");
    if (!init) throw dx::DexVmError(dx::DexVmErrorReason::internal_invariant,
                                   "Intent copy constructor is not linked");
    const auto outcome = vm.Call(*init,
        std::vector{dx::VmValue::Ref(snapshot), dx::VmValue::Ref(source)});
    if (outcome.exception.IsValid())
        throw dx::VmJavaThrow{vm.Linker().Class(outcome.exception_class).descriptor,
                              outcome.exception_message, outcome.exception};
    return snapshot;
}

[[nodiscard]] dx::VmValue GetPendingIntent(dx::IntrinsicContext& call,
                                          const Context& context, int kind) {
    dx::IntrinsicCall args(call);
    const auto owner = args.NonNullRef(0, "context");
    const auto intent = args.NonNullRef(2, "intent");
    const auto flags = args.Int(3);
    if (call.vm.Model().ObjectClass(intent) !=
        call.vm.Linker().ResolveDescriptor("Landroid/content/Intent;"))
        UnsupportedPending(call, "PendingIntent requires an ordinary API19 Intent");
    if (context->pending_intents_stopping)
        UnsupportedPending(call, "PendingIntent process is stopping");
    if ((flags & ~(kControlFlags | kOneShot | kIntentFillFlags)) != 0)
        UnsupportedPending(call, "unsupported API19 PendingIntent flags");
    const auto package = call.vm.StringUtf8(CallAndroidMethod(call.vm, owner,
        "getPackageName", "()Ljava/lang/String;").ref);
    if (package.empty() || package != context->package_name)
        UnsupportedPending(call, "PendingIntent creator must be the current APK");
    // All ordinary Intent value semantics execute the original BootDex code.
    // Provider-based MIME inference would require a separate capability.
    const auto type = CallAndroidMethod(call.vm, intent, "getType",
                                        "()Ljava/lang/String;").ref;
    const auto data = CallAndroidMethod(call.vm, intent, "getData",
                                        "()Landroid/net/Uri;").ref;
    if (data.IsValid() &&
        !call.vm.Linker().Class(call.vm.Model().ObjectClass(data)).is_boot_dex)
        UnsupportedPending(call, "custom PendingIntent Uri implementations are unsupported");
    if (!type.IsValid() && data.IsValid()) {
        const auto scheme = CallAndroidMethod(call.vm, data, "getScheme",
                                              "()Ljava/lang/String;").ref;
        if (scheme.IsValid() && call.vm.StringUtf8(scheme) == "content")
            UnsupportedPending(call, "PendingIntent content MIME inference is unsupported");
    }
    if (CallAndroidMethod(call.vm, intent, "getSelector",
                          "()Landroid/content/Intent;").ref.IsValid())
        UnsupportedPending(call, "PendingIntent selector resolution is unsupported");

    // Context callbacks above may park. Recheck teardown before registry access.
    if (context->pending_intents_stopping)
        UnsupportedPending(call, "PendingIntent process is stopping");
    // Snapshot the weak registry before calls which can allocate/collect.
    // Temporary roots keep candidate wrappers alive only for this lookup.
    std::vector<DexVmAndroidContext::PendingIntentRecord> candidates;
    std::vector<dx::VmObjectRef> candidate_roots;
    for (const auto& [_, record] : context->pending_intents) {
        if (!record.canceled && record.creator_package == package &&
            record.kind == kind && record.request_code == args.Int(1) &&
            record.flags == (flags & ~kControlFlags)) {
            candidates.push_back(record);
            candidate_roots.push_back(record.object);
        }
    }
    const auto roots = call.vm.ProtectReferences(candidate_roots);
    dx::VmObjectRef existing;
    for (const auto& record : candidates) {
        if (CallAndroidMethod(call.vm, record.intent, "filterEquals",
            "(Landroid/content/Intent;)Z", {dx::VmValue::Ref(intent)}).AsInt() != 0) {
            existing = record.object;
            break;
        }
    }
    if (existing.IsValid()) {
        if ((flags & kCancelCurrent) != 0) {
            context->pending_intents.at(existing.Value()).canceled = true;
            std::erase(context->alarm_operations, existing);
        } else {
            if ((flags & kUpdateCurrent) != 0) {
                const auto snapshot = context->pending_intents.at(existing.Value()).intent;
                static_cast<void>(CallAndroidMethod(call.vm, snapshot, "replaceExtras",
                    "(Landroid/content/Intent;)Landroid/content/Intent;",
                    {dx::VmValue::Ref(intent)}));
            }
            return dx::VmValue::Ref(existing);
        }
    }
    // API19 CANCEL_CURRENT|NO_CREATE returns the old (now canceled) token.
    if ((flags & kNoCreate) != 0) return dx::VmValue::Ref(existing);
    const auto snapshot = CopyPendingIntent(call.vm, intent);
    const auto snapshot_root = call.vm.ProtectReferences(std::array{snapshot});
    const auto object = call.vm.NewIntrinsicInstance("Landroid/app/PendingIntent;");
    context->pending_intents.emplace(object.Value(),
        DexVmAndroidContext::PendingIntentRecord{object, snapshot, package,
            kind, args.Int(1), flags & ~kControlFlags, false});
    return dx::VmValue::Ref(object);
}
} // namespace

Decl Declare_android_app_PendingIntent(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/app/PendingIntent;", "Ljava/lang/Object;",
                                                    {"Landroid/os/Parcelable;"});
    constexpr auto signature = "(Landroid/content/Context;ILandroid/content/Intent;I)Landroid/app/PendingIntent;";
    builder.StaticMethod("getService", signature,
        [context](dx::IntrinsicContext& call) { return GetPendingIntent(call, context, 4); });
    builder.StaticMethod("getBroadcast", signature,
        [context](dx::IntrinsicContext& call) { return GetPendingIntent(call, context, 1); });
    builder.VirtualMethod("cancel", "()V", [context](dx::IntrinsicContext& call) {
        const auto found = context->pending_intents.find(call.receiver.Value());
        if (found == context->pending_intents.end())
            UnsupportedPending(call, "PendingIntent has no process token");
        found->second.canceled = true;
        std::erase(context->alarm_operations, call.receiver);
        return dx::VmValue::Void();
    });
    const auto unsupported = [](dx::IntrinsicContext& call) -> dx::VmValue {
        UnsupportedPending(call, "PendingIntent delivery is unsupported");
    };
    builder.VirtualMethod("send", "()V", unsupported);
    builder.VirtualMethod("send", "(I)V", unsupported);
    builder.VirtualMethod("send", "(Landroid/content/Context;ILandroid/content/Intent;)V", unsupported);
    return std::move(builder).Build();
}

Decl Declare_android_app_AlarmManager(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/app/AlarmManager;", "Ljava/lang/Object;");
    builder.VirtualMethod("cancel", "(Landroid/app/PendingIntent;)V",
        [context](dx::IntrinsicContext& call) {
            const auto operation = dx::IntrinsicCall(call).Ref(0);
            // API19 AlarmManagerService.remove(null) returns without work.
            if (!operation.IsValid()) return dx::VmValue::Void();
            if (!context->pending_intents.contains(operation.Value()))
                UnsupportedPending(call, "alarm operation has no process token");
            // Unlike PendingIntent.cancel(), this leaves the sender valid.
            std::erase(context->alarm_operations, operation);
            return dx::VmValue::Void();
        });
    const auto unsupported = [](dx::IntrinsicContext& call) -> dx::VmValue {
        if (auto* ledger = call.vm.Ledger())
            ledger->RecordUnimplemented("dexvm.alarm_manager", 0);
        throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;",
                              "alarm scheduling is unsupported"};
    };
    for (const auto* name : {"set", "setExact"})
        builder.VirtualMethod(name, "(IJLandroid/app/PendingIntent;)V", unsupported);
    for (const auto* name : {"setRepeating", "setInexactRepeating", "setWindow"})
        builder.VirtualMethod(name, "(IJJLandroid/app/PendingIntent;)V", unsupported);
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics


// ---- migrated from android_app_ProgressDialog.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_app_ProgressDialog(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/app/ProgressDialog;", "Ljava/lang/Object;");
    builder.Constructor("(Landroid/content/Context;)V", WidgetNoopHandler());
    builder.VirtualMethod("setMessage", "(Ljava/lang/CharSequence;)V", WidgetNoopHandler());
    builder.VirtualMethod("setProgressStyle", "(I)V", WidgetNoopHandler());
    builder.FinalMethod("show", "()V", WidgetNoopHandler());
    builder.FinalMethod("dismiss", "()V", WidgetNoopHandler());
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics


// ---- migrated from support_activity.cpp ----
// Managed surface lifecycle callback dispatch. Intrinsic handlers live in
// their per-class declaration files.

#include "shared.h"

#include <algorithm>

namespace ogplay::runtime {
namespace {

// Content revision changes can append an unrelated sibling without retiring this
// holder. Surface liveness is the actual window/root and complete mount chain.
bool SurfaceOwnerCurrent(dexvm::Interpreter& vm, DexVmAndroidContext& context,
                         DexVmAndroidContext::UiOwnerIdentity identity) {
    const auto projection = context.ui_tree.PublishedProjection();
    const auto node = FindViewUiNode(context, identity.view.Value());
    if (!projection || !node || !projection->Get(*node) || !projection->IsVisible(*node)) return false;
    identity.revision = context.ui_content_revision;
    return IsAndroidUiOwnerCurrent(vm, context, identity, false);
}

void SetGlSurfaceAvailability(DexVmAndroidContext& context, const ui::UiNodeId subtree, const bool available) {
    std::vector<ui::UiNodeId> nodes{subtree};
    while (!nodes.empty()) {
        const auto node = nodes.back(); nodes.pop_back();
        const auto* state = context.ui_tree.Get(node);
        if (!state) continue;
        nodes.insert(nodes.end(), state->children.begin(), state->children.end());
        const auto view = ViewObjectForUiNode(context, node);
        if (const auto runtime = context.gl_surface_runtimes.find(view.Value()); runtime != context.gl_surface_runtimes.end()) {
            runtime->second->surface_available = available &&
                android_intrinsics::SurfaceViewReadyForRendering(context, view);
            if (runtime->second->surface_available && runtime->second->stopped) {
                runtime->second->stopped = false;
                runtime->second->paused = false;
            }
        }
    }
}

std::vector<std::uint32_t> SubtreeHolderHandles(
    const DexVmAndroidContext& context, const ui::UiNodeId subtree) {
    std::vector<std::uint32_t> holders;
    std::unordered_set<std::uint32_t> seen;
    std::vector<ui::UiNodeId> pending{subtree};
    while (!pending.empty()) {
        const auto node = pending.back();
        pending.pop_back();
        const auto* state = context.ui_tree.Get(node);
        if (state == nullptr) continue;
        const auto view = ViewObjectForUiNode(context, node);
        if (view.IsValid()) {
            const auto found = context.surface_holders.find(view.Value());
            if (found != context.surface_holders.end() &&
                found->second.IsValid() &&
                seen.insert(found->second.Value()).second) {
                holders.push_back(found->second.Value());
            }
        }
        pending.insert(pending.end(), state->children.rbegin(),
                       state->children.rend());
    }
    return holders;
}

void CompleteSurfaceTraversal(DexVmAndroidContext& context,
    const ui::UiNodeId subtree,
    const std::unordered_set<ui::UiNodeId, ui::UiNodeIdHash>& pending) {
    // A callback can attach another subtree which must wait for the next traversal.
    for (const auto node : pending) {
        if (!context.ui_tree.Get(node) || !context.ui_tree.IsAttached(node) ||
            !context.ui_tree.IsVisible(node)) continue;
        bool incomplete = false;
        for (const auto holder : SubtreeHolderHandles(context, node)) {
            // A creation callback can attach a new SurfaceView before the
            // subsequent changed phase; it has not joined this traversal.
            if (!context.active_surface_holders.contains(holder) ||
                !context.surface_callback_sizes.contains(holder)) {
                incomplete = true;
                break;
            }
        }
        if (incomplete) continue;
        for (auto cursor = std::optional{node}; cursor;) {
            if (*cursor == subtree) {
                context.surface_traversal_pending.erase(node);
                break;
            }
            const auto* state = context.ui_tree.Get(*cursor);
            cursor = state ? state->parent : std::nullopt;
        }
    }
}

std::vector<std::uint32_t> AttachedHolderHandles(
    const DexVmAndroidContext& context) {
    auto root = context.ui_tree.Root();
    for (const auto& record : context.activity_stack)
        if (record.object == context.activity && record.window_root) root = *record.window_root;
    auto holders = SubtreeHolderHandles(context, root);
    std::erase_if(holders, [&](const auto holder) {
        for (const auto& [view, candidate] : context.surface_holders) {
            if (candidate.Value() != holder) continue;
            const auto node = FindViewUiNode(context, view);
            return !node || !context.ui_tree.IsVisible(*node);
        }
        return false;
    });
    return holders;
}

std::optional<std::string> DispatchHolderCallbacks(
    dexvm::Interpreter& vm, DexVmAndroidContext& context, const std::span<const std::uint32_t> holders,
    const SurfaceHolderPhase phase,
    const std::map<std::uint32_t, std::vector<dexvm::VmObjectRef>>* transaction_callbacks = nullptr) {
    namespace dx = dexvm;
    const auto* name = "surfaceCreated";
    std::string descriptor = "(Landroid/view/SurfaceHolder;)V";
    std::vector<dx::VmValue> extra;
    if (phase == SurfaceHolderPhase::changed) {
        name = "surfaceChanged";
        descriptor = "(Landroid/view/SurfaceHolder;III)V";
        // PixelFormat.RGBA_8888: the managed surface really is RGBA8.
        extra = {dx::VmValue::Int(1),
                 dx::VmValue::Int(
                     static_cast<std::int32_t>(context.surface_width)),
                 dx::VmValue::Int(
                     static_cast<std::int32_t>(context.surface_height))};
    } else if (phase == SurfaceHolderPhase::destroyed) {
        name = "surfaceDestroyed";
    }

    const auto activity = context.activity;
    const auto root = AndroidUiTraversalRoot(vm, context);
    const auto generation = context.ui_tree.Generation();
    const auto window_generation = context.window_holder_generation;
    const auto window_holder = context.window_surface_holder;
    const auto width = context.surface_width, height = context.surface_height;
    std::map<std::uint32_t, DexVmAndroidContext::UiOwnerIdentity> identities;
    for (const auto holder : holders) {
        if (context.window_surface_holders.contains(holder) || holder == window_holder.Value()) continue;
        for (const auto& [view, candidate] : context.surface_holders)
            if (candidate.Value() == holder)
                identities.emplace(holder, CaptureAndroidUiOwner(vm, context, dx::VmObjectRef{view}));
    }
    const auto current = [&](std::uint32_t holder) {

        if (activity != context.activity || root != AndroidUiTraversalRoot(vm, context) ||
            generation != context.ui_tree.Generation() || width != context.surface_width ||
            height != context.surface_height)
            return false;
        if (phase == SurfaceHolderPhase::destroyed && context.window_surface_holders.contains(holder))
            return window_generation == context.window_holder_generation;
        if (holder == window_holder.Value())
            return window_holder == context.window_surface_holder &&
                   window_generation == context.window_holder_generation &&
                   (phase == SurfaceHolderPhase::destroyed ||
                    (context.managed_host_surface_open && context.ui_tree.PublishedProjection() &&
                     context.ui_tree.PublishedProjection()->IsVisible(context.ui_tree.Root())));
        const auto found = identities.find(holder);
        if (found == identities.end()) return false;
        const auto binding = context.surface_holders.find(found->second.view.Value());
        if (binding == context.surface_holders.end() || binding->second.Value() != holder) return false;
        return phase == SurfaceHolderPhase::destroyed ||
               (context.managed_host_surface_open && SurfaceOwnerCurrent(vm, context, found->second));
    };
    auto& linker = vm.Linker();
    std::size_t delivered = 0;
    for (const auto holder_handle : holders) {
        if (!current(holder_handle)) {
            RecordAndroidSurfaceDiagnostic(
                vm, context, "ui.surface.holder", "holder rejected " + std::to_string(holder_handle),
                {{"holder", std::uint64_t(holder_handle)}, {"reason", std::string("stale owner/root/mount/buffer")}});
            continue;
        }
        if (phase == SurfaceHolderPhase::created) {
            bool hidden{};
            for (const auto& [view, holder] : context.surface_holders) {
                if (holder.Value() != holder_handle) continue;
                const auto node = FindViewUiNode(context, view);
                hidden = node && !context.ui_tree.IsVisible(*node);
                break;
            }
            if (hidden) continue;
            if (context.active_surface_holders.contains(holder_handle)) continue;
            // A new Surface must receive changed even when its buffer size is unchanged.
            context.surface_callback_sizes.erase(holder_handle);
            // AOSP sets mSurfaceCreated before invoking callbacks.
            context.active_surface_holders.insert(holder_handle);
            {
                RecordAndroidSurfaceDiagnostic(
                    vm, context, "ui.surface.holder", "holder activated",
                    {{"holder", std::uint64_t(holder_handle)},
                     {"kind", std::string(holder_handle == window_holder.Value() ? "window" : "view")},
                     {"generation", std::uint64_t(generation)}},
                    {.mode = core::RateLimitMode::none});
                if (holder_handle == window_holder.Value())
                    context.active_window_holder_generation = window_generation;
                else
                    context.surface_owner_identities[holder_handle] = identities.at(holder_handle);
            }
        } else if (phase == SurfaceHolderPhase::destroyed) {
            if (context.active_surface_holders.erase(holder_handle) == 0U) continue;
            context.surface_owner_identities.erase(holder_handle);
            DetachAndroidMediaSurface(context, holder_handle);
            context.surface_callback_sizes.erase(holder_handle);
            // AOSP clears mSurfaceCreated before invoking callbacks.
        } else if (!context.active_surface_holders.contains(holder_handle)) {
            continue;
        }

        struct DestroyScope {
            DexVmAndroidContext& context;
            std::uint32_t holder;
            bool destroying;
            // AOSP keeps the Surface valid through surfaceDestroyed and
            // releases it afterwards; NDK windows retire at the same point.
            ~DestroyScope() {
                if (!destroying) return;
                if (context.retire_native_surface)
                    context.retire_native_surface(dexvm::VmObjectRef(holder));
                context.destroying_surface_holders.erase(holder);
                {
                    const auto surface = context.holder_surfaces.find(holder);
                    if (surface != context.holder_surfaces.end()) {
                        // Invalidate this endpoint permanently before a later
                        // holder generation can become active. Keep its owner
                        // token until Java release/GC so release remains valid.
                        const auto owner = context.stream_surface_owners.find(surface->second.Value());
                        if (owner != context.stream_surface_owners.end())
                            context.managed_surface_tokens.erase(owner->second);
                        context.holder_surfaces.erase(surface);
                    }
                }
                if (context.window_surface_holders.contains(holder) &&
                    holder != context.window_surface_holder.Value()) {
                    context.window_surface_holders.erase(holder);
                    context.surface_callbacks.erase(holder);
                    context.holder_surfaces.erase(holder);
                }
            }
        } scope{context, holder_handle, phase == SurfaceHolderPhase::destroyed};
        if (scope.destroying) context.destroying_surface_holders.insert(holder_handle);
        if (phase == SurfaceHolderPhase::changed) {
            if (context.update_native_surface) context.update_native_surface(
                dx::VmObjectRef(holder_handle), context.surface_width, context.surface_height);
            const auto size = std::pair{context.surface_width, context.surface_height};
            const auto previous = context.surface_callback_sizes.find(holder_handle);
            if (previous != context.surface_callback_sizes.end() && previous->second == size) continue;
            context.surface_callback_sizes[holder_handle] = size;
        }

        if (phase != SurfaceHolderPhase::destroyed) {
            try {
                android_intrinsics::PublishSurfaceHolderFrame(vm, context, dx::VmObjectRef(holder_handle));
            } catch (const dx::VmJavaThrow& error) {
                RecordAndroidSurfaceDiagnostic(vm, context, "ui.surface.holder", "holder buffer rejected",
                                               {{"holder", std::uint64_t(holder_handle)}, {"reason", error.message}});
                return error.descriptor + ": " + error.message;
            }
        }

        RecordAndroidSurfaceDiagnostic(
            vm, context, "ui.surface.callback", "Surface callback dispatch",
            {{"holder", std::uint64_t(holder_handle)},
             {"phase", std::string(name)},
             {"kind", std::string(context.window_surface_holders.contains(holder_handle) ? "window" : "view")},
             {"width", std::uint64_t(width)},
             {"height", std::uint64_t(height)}},
            {.mode = core::RateLimitMode::none});
        std::vector<dx::VmObjectRef> callbacks;
        if (transaction_callbacks) {
            const auto found = transaction_callbacks->find(holder_handle);
            if (found != transaction_callbacks->end()) callbacks = found->second;
        } else {
            const auto found = context.surface_callbacks.find(holder_handle);
            if (found != context.surface_callbacks.end()) callbacks = found->second;
        }
        // A Java owner transaction shares this callback snapshot across phases.
        const auto callback_roots = vm.ProtectReferences(callbacks);
        for (const auto callback : callbacks) {
            if (!current(holder_handle)) break;
            const auto callback_class = vm.Model().ObjectClass(callback);
            const auto index =
                linker.FindVtableIndex(callback_class, name, descriptor);
            if (!index.has_value()) {
                return std::string("SurfaceHolder.Callback has no ") + name +
                       ": " + linker.Class(callback_class).descriptor;
            }
            std::vector<dx::VmValue> arguments{
                dx::VmValue::Ref(callback),
                dx::VmValue::Ref(dx::VmObjectRef(holder_handle))};
            arguments.insert(arguments.end(), extra.begin(), extra.end());
            const auto outcome = vm.Call(
                linker.Class(callback_class).vtable[*index], arguments);
            ++delivered;
            if (!outcome.exception.IsValid()) {
                if (!current(holder_handle)) break;
                if (phase == SurfaceHolderPhase::changed) {
                    const auto redraw = linker.FindVtableIndex(callback_class,
                        "surfaceRedrawNeeded", "(Landroid/view/SurfaceHolder;)V");
                    if (redraw) {
                        const auto result = vm.Call(linker.Class(callback_class).vtable[*redraw],
                            std::array{dx::VmValue::Ref(callback), dx::VmValue::Ref(dx::VmObjectRef(holder_handle))});
                        if (result.exception.IsValid())
                            return "surfaceRedrawNeeded raised " + result.exception_message;
                        if (!current(holder_handle)) break;
                    }
                }
                continue;
            }
            std::string rendered = std::string(name) + " raised " +
                                   linker.Class(outcome.exception_class)
                                       .descriptor +
                                   ": " + outcome.exception_message;
            for (const auto& entry : outcome.exception_stack) {
                rendered += "\n  at " + entry.class_descriptor + "." +
                            entry.method_name + " (pc " +
                            std::to_string(entry.pc) + ")";
            }
            return rendered;
        }
    }
    if (auto* logger = vm.Log(); logger != nullptr && delivered > 0) {
        logger->Write(core::LogLevel::info, "session.dex_lifecycle",
                      std::string("managed surface ") + name +
                          " delivered to " + std::to_string(delivered) +
                          " holder callback(s)", {}, {},
                      {.mode = core::RateLimitMode::none});
    }
    return std::nullopt;
}

}  // namespace

void DispatchWindowInputQueue(dexvm::Interpreter& vm, DexVmAndroidContext& context, bool created) {
    using namespace android_intrinsics;
    if (!context.window_input_callback.IsValid()) return;
    if (created) {
        if (context.window_input_queue.IsValid()) return;
        if (!context.native_activity)
            throw dexvm::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;", "window input requires NativeActivity runtime"};
        const auto pointer = context.native_activity->InputQueuePointer(context.activity);
        if (pointer.IsNull()) return; // takeInputQueue during onCreate, before loadNativeCode
        const auto queue = vm.NewIntrinsicInstance("Landroid/view/InputQueue;");
        context.window_input_queue = queue;
        const auto field = vm.Linker().FindFieldRecursive(vm.Model().ObjectClass(queue), "mPtr", "I");
        if (!field) throw std::logic_error("InputQueue.mPtr is missing");
        vm.Model().InstanceSlots(queue)[vm.Linker().Field(*field).slot] = {pointer.Value(), dexvm::SlotTag::cat1};
        static_cast<void>(CallAndroidMethod(vm, context.window_input_callback, "onInputQueueCreated",
            "(Landroid/view/InputQueue;)V", std::vector{dexvm::VmValue::Ref(queue)}));
    } else if (context.window_input_queue.IsValid()) {
        const auto queue = context.window_input_queue;
        context.window_input_queue = dexvm::VmObjectRef{};
        const auto root = vm.ProtectReferences(std::array{queue});
        static_cast<void>(CallAndroidMethod(vm, context.window_input_callback, "onInputQueueDestroyed",
            "(Landroid/view/InputQueue;)V", std::vector{dexvm::VmValue::Ref(queue)}));
        const auto field = vm.Linker().FindFieldRecursive(vm.Model().ObjectClass(queue), "mPtr", "I");
        vm.Model().InstanceSlots(queue)[vm.Linker().Field(*field).slot] = {0, dexvm::SlotTag::cat1};
    }
}

void SetWindowInputCallback(dexvm::Interpreter& vm, DexVmAndroidContext& context, dexvm::VmObjectRef callback) {
    if (context.window_input_callback == callback) return;
    const auto root = vm.ProtectReferences(std::array{callback});
    DispatchWindowInputQueue(vm, context, false);
    context.window_input_callback = callback;
    if (context.managed_host_surface_open) DispatchWindowInputQueue(vm, context, true);
}

void SetWindowSurfaceCallback(dexvm::Interpreter& vm, DexVmAndroidContext& context, dexvm::VmObjectRef callback) {
    if (context.window_surface_callback == callback) return;
    ++context.window_holder_generation;
    {
        // Each callback owner gets a distinct holder generation. Retain the old
        // callbacks until the next safe point can deliver destroyed without reentry.
        const auto previous = context.window_surface_holder.Value();
        if (previous && !context.active_surface_holders.contains(previous)) {
            context.window_surface_holders.erase(previous);
            context.surface_callbacks.erase(previous);
            context.holder_surfaces.erase(previous);
        }
        context.window_surface_callback = callback;
        context.window_surface_holder =
            callback.IsValid() ? vm.NewIntrinsicInstance("Landroid/view/SurfaceHolder$Impl;") : dexvm::VmObjectRef{};
        if (callback.IsValid()) {
            context.window_surface_holders.insert(context.window_surface_holder.Value());
            context.surface_callbacks[context.window_surface_holder.Value()] = {callback};
        }
        return;
    }
}

std::optional<std::string> PumpAndroidSurfaceEvents(dexvm::Interpreter& vm, DexVmAndroidContext& context) {
    if (!context.managed_host_surface_open || context.surface_event_dispatching || context.input_presentation ||
        context.ui_layout_dispatching)
        return std::nullopt;
    const dexvm::VmExecutionLockScope execution(vm.ExecutionLock());
    const auto published = context.ui_tree.PublishedProjection();
    // A hidden window only retires holders. Do not consume pending Java layout
    // just to deliver destruction after a lifecycle visibility publication.
    if (!published || published->IsVisible(published->Root()))
        static_cast<void>(EnsureAndroidUiTraversal(vm, context, AndroidUiTraversalReason::BeforeSurfaceCallbacks,
                                                   AndroidUiTraversalPhase::ResolveAndNotifySizes));
    context.surface_event_dispatching = true;

    struct Reset {
        bool& flag;

        ~Reset() { flag = false; }
    } reset{context.surface_event_dispatching};

    const auto pending = context.surface_traversal_pending;
    const auto root = AndroidUiTraversalRoot(vm, context);
    const auto generation = context.ui_tree.Generation();
    const auto window_generation = context.window_holder_generation;
    std::vector<std::uint32_t> holders, retired;
    std::map<std::uint32_t, DexVmAndroidContext::UiOwnerIdentity> identities;
    for (const auto& [view, holder] : context.surface_holders) {
        const auto identity = CaptureAndroidUiOwner(vm, context, dexvm::VmObjectRef{view});
        if (SurfaceOwnerCurrent(vm, context, identity)) {
            holders.push_back(holder.Value());
            identities.emplace(holder.Value(), identity);
        } else {
            RecordAndroidSurfaceDiagnostic(
                vm, context, "ui.surface.holder", "holder inactive " + std::to_string(holder.Value()),
                {{"holder", std::uint64_t(holder.Value())},
                 {"view", std::uint64_t(view)},
                 {"reason", std::string(identity.mounts.empty() || identity.mounts.back().first != identity.root
                                            ? "unmounted"
                                            : "owner or visibility mismatch")}});
        }
    }
    const auto projection = context.ui_tree.PublishedProjection();
    if (context.window_surface_callback.IsValid() && projection && projection->IsVisible(projection->Root()))
        holders.push_back(context.window_surface_holder.Value());
    std::ranges::sort(holders);
    for (const auto holder : context.active_surface_holders) {
        const auto old = context.surface_owner_identities.find(holder);
        if (std::ranges::find(holders, holder) == holders.end() ||
            (old != context.surface_owner_identities.end() && !SurfaceOwnerCurrent(vm, context, old->second)) ||
            (holder == context.window_surface_holder.Value() &&
             context.active_window_holder_generation != context.window_holder_generation))
            retired.push_back(holder);
    }
    std::ranges::sort(retired);
    std::map<std::uint32_t, std::vector<dexvm::VmObjectRef>> callbacks;
    std::vector<dexvm::VmObjectRef> references{context.activity, root};
    for (const auto& [_, identity] : identities)
        for (const auto& [view, _] : identity.mounts)
            references.push_back(view);
    for (const auto* group : {&holders, &retired})
        for (const auto holder : *group) {
            references.emplace_back(holder);
            if (const auto found = context.surface_callbacks.find(holder); found != context.surface_callbacks.end()) {
                callbacks[holder] = found->second;
                references.insert(references.end(), found->second.begin(), found->second.end());
            }
        }
    const auto transaction_roots = vm.ProtectReferences(references);
    if (const auto error = DispatchHolderCallbacks(vm, context, retired, SurfaceHolderPhase::destroyed, &callbacks))
        return error;
    const auto stale = [&] {
        return root != AndroidUiTraversalRoot(vm, context) || generation != context.ui_tree.Generation() ||
               window_generation != context.window_holder_generation;
    };
    if (stale()) return std::nullopt;
    std::erase_if(holders, [&](auto holder) {
        const auto found = identities.find(holder);
        return found != identities.end() && !SurfaceOwnerCurrent(vm, context, found->second);
    });
    if (const auto error = DispatchHolderCallbacks(vm, context, holders, SurfaceHolderPhase::created, &callbacks))
        return error;
    if (stale()) return std::nullopt;
    std::erase_if(holders, [&](auto holder) {
        const auto found = identities.find(holder);
        return found != identities.end() && !SurfaceOwnerCurrent(vm, context, found->second);
    });
    if (const auto error = DispatchHolderCallbacks(vm, context, holders, SurfaceHolderPhase::changed, &callbacks))
        return error;
    if (stale()) return std::nullopt;
    CompleteSurfaceTraversal(context, ActivityContentRoot(context), pending);
    SetGlSurfaceAvailability(context, context.ui_tree.Root(), true);
    return std::nullopt;
}

std::optional<std::string> DispatchSurfaceHolderCallbacks(
    dexvm::Interpreter& vm, DexVmAndroidContext& context,
    const SurfaceHolderPhase phase) {
    if (phase != SurfaceHolderPhase::destroyed) {
        if (phase == SurfaceHolderPhase::created) context.managed_host_surface_open = true;
        DispatchWindowInputQueue(vm, context, true);
        return PumpAndroidSurfaceEvents(vm, context);
    }
    auto pending = context.surface_traversal_pending;
    if (phase == SurfaceHolderPhase::changed) {
        try {
            static_cast<void>(EnsureAndroidUiTraversal(vm, context, AndroidUiTraversalReason::BeforeSurfaceCallbacks,
                                                       AndroidUiTraversalPhase::ResolveAndNotifySizes));
        } catch (const dexvm::VmJavaThrow& error) {
            return error.descriptor + ": " + error.message;
        }
        pending = context.surface_traversal_pending;
    }
    std::vector<std::uint32_t> holders;
    if (phase == SurfaceHolderPhase::created) {
        context.managed_host_surface_open = true;
        try {
            static_cast<void>(EnsureAndroidUiTraversal(vm, context, AndroidUiTraversalReason::BeforeSurfaceCallbacks,
                                                       AndroidUiTraversalPhase::ResolveAndNotifySizes));
        } catch (const dexvm::VmJavaThrow& error) {
            return error.descriptor + ": " + error.message;
        }
        pending = context.surface_traversal_pending;
        holders = AttachedHolderHandles(context);
        if (context.window_surface_callback.IsValid())
            holders.push_back(context.window_surface_holder.Value());
        DispatchWindowInputQueue(vm, context, true);
    } else if (phase == SurfaceHolderPhase::destroyed) {
        context.managed_host_surface_open = false;
        SetGlSurfaceAvailability(context, context.ui_tree.Root(), false);
        DispatchWindowInputQueue(vm, context, false);
        holders.assign(context.active_surface_holders.begin(),
                       context.active_surface_holders.end());
        std::ranges::sort(holders);
    } else {
        holders.assign(context.active_surface_holders.begin(),
                       context.active_surface_holders.end());
        std::ranges::sort(holders);
    }
    const auto error = DispatchHolderCallbacks(vm, context, holders, phase);
    if (!error && phase == SurfaceHolderPhase::changed) {
        CompleteSurfaceTraversal(context, ActivityContentRoot(context), pending);
        SetGlSurfaceAvailability(context, context.ui_tree.Root(), true);
    }
    return error;
}

std::optional<std::string> AttachSurfaceViewSubtree(
    dexvm::Interpreter& vm, DexVmAndroidContext& context,
    const ui::UiNodeId subtree) {
    {
        android_intrinsics::MergeManagedViewTreeObservers(vm, context, ViewObjectForUiNode(context, subtree));
        context.surface_traversal_pending.insert(subtree);
        return std::nullopt;
    }
}

bool SurfaceTraversalPending(const DexVmAndroidContext& context,
                             const ui::UiNodeId node) {
    if (context.surface_traversal_pending.empty()) return false;
    for (auto cursor = std::optional{node}; cursor;) {
        if (context.surface_traversal_pending.contains(*cursor)) return true;
        const auto* state = context.ui_tree.Get(*cursor);
        cursor = state ? state->parent : std::nullopt;
    }
    return false;
}

std::optional<std::string> DetachSurfaceViewSubtree(dexvm::Interpreter&, DexVmAndroidContext& context,
                                                    const ui::UiNodeId subtree) {
    {
        context.surface_traversal_pending.insert(subtree);
        return std::nullopt;
    }
}

std::optional<std::string> RetireSurfaceHolderGeneration(
    dexvm::Interpreter& vm, DexVmAndroidContext& context) {
    const auto error = DispatchSurfaceHolderCallbacks(
        vm, context, SurfaceHolderPhase::destroyed);
    if (error.has_value()) return error;
    context.window_surface_holders.clear();
    context.surface_owner_identities.clear();
    context.window_surface_callback = dexvm::VmObjectRef{};
    context.window_surface_holder = dexvm::VmObjectRef{};
    context.window_input_callback = dexvm::VmObjectRef{};
    context.window_input_queue = dexvm::VmObjectRef{};
    context.holder_surfaces.clear();
    context.surface_callbacks.clear();
    context.surface_holders.clear();
    context.active_surface_holders.clear();
    context.surface_callback_sizes.clear();
    context.surface_traversal_pending.clear();
    context.managed_host_surface_open = false;
    return std::nullopt;
}

}  // namespace ogplay::runtime

namespace ogplay::runtime {
void AttachAndroidActivityIdentity(dexvm::Interpreter& vm,
                                   const std::shared_ptr<DexVmAndroidContext>& context,
                                   dexvm::VmObjectRef activity,
                                   const std::string& component_name) {
    using namespace android_intrinsics;
    const auto owner = vm.ProtectReferences(std::array{activity});
    auto intent = context->current_intent;
    if (!intent.IsValid()) {
        intent = vm.NewIntrinsicInstance("Landroid/content/Intent;");
        context->current_intent = intent;
        const auto init = vm.Linker().FindDirectMethod(
            vm.Linker().ResolveDescriptor("Landroid/content/Intent;"), "<init>", "()V");
        if (!init)
            throw dx::DexVmError(dx::DexVmErrorReason::unresolved_reference,
                                 "Intent constructor");
        const auto outcome = vm.Call(*init, std::array{dx::VmValue::Ref(intent)});
        if (outcome.exception.IsValid())
            throw dx::VmJavaThrow{vm.Linker().Class(outcome.exception_class).descriptor,
                                  outcome.exception_message, outcome.exception};
    }
    const auto input = vm.ProtectReferences(std::array{intent});
    auto component = CallAndroidMethod(vm, intent, "getComponent",
                                       "()Landroid/content/ComponentName;")
                         .ref;
    if (!component.IsValid()) {
        const auto package = vm.NewStringUtf8(context->package_name);
        const auto package_root = vm.ProtectReferences(std::array{package});
        component =
            NewAndroidComponentName(vm, package, vm.NewStringUtf8(component_name));
        static_cast<void>(CallAndroidMethod(
            vm, intent, "setComponent",
            "(Landroid/content/ComponentName;)Landroid/content/Intent;",
            {dx::VmValue::Ref(component)}));
    }
    // The launch component is stable even if the Intent is subsequently replaced.
    const auto store = [&](const char* name, const char* signature,
                           dx::VmObjectRef value) {
        const auto field = vm.Linker().FindFieldRecursive(
            vm.Linker().ResolveDescriptor("Landroid/app/Activity;"), name, signature);
        if (!field)
            throw dx::DexVmError(dx::DexVmErrorReason::unresolved_reference, name);
        vm.Model().InstanceSlots(activity)[vm.Linker().Field(*field).slot] = {
            value.Value(), dx::SlotTag::ref};
    };
    store("mComponent", "Landroid/content/ComponentName;", component);
    store("mIntent", "Landroid/content/Intent;", intent);
    const auto theme = context->activity_themes.find(component_name);
    const auto resource = theme == context->activity_themes.end()
        ? context->application_theme : theme->second;
    if (resource != 0)
        static_cast<void>(CallAndroidMethod(vm, activity, "setTheme", "(I)V",
            {dx::VmValue::Int(static_cast<std::int32_t>(resource))}));
}
} // namespace ogplay::runtime

namespace ogplay::runtime::android_intrinsics {
Decl Declare_android_app_NativeActivity(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/app/NativeActivity;", "Landroid/app/Activity;");
    const auto bind = [&](const char* name, const char* signature) {
        builder.DirectMethod(name, signature, [context, name](dx::IntrinsicContext& call) {
            if (!context->native_activity) {
                if (auto* ledger = call.vm.Ledger()) ledger->RecordUnimplemented("dexvm.native_activity", 0);
                throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;", "NativeActivity requires the current guest process"};
            }
            return context->native_activity->Call(call, name);
        }, dx::kAccPrivate | dx::kAccNative);
    };
    bind("loadNativeCode", "(Ljava/lang/String;Ljava/lang/String;Landroid/os/MessageQueue;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;ILandroid/content/res/AssetManager;[B)I");
    for (const auto* name : {"unloadNativeCode", "onStartNative", "onResumeNative", "onPauseNative", "onStopNative", "onConfigurationChangedNative", "onLowMemoryNative", "onSurfaceDestroyedNative"}) bind(name, "(I)V");
    bind("onSaveInstanceStateNative", "(I)[B");
    bind("onWindowFocusChangedNative", "(IZ)V");
    bind("onSurfaceCreatedNative", "(ILandroid/view/Surface;)V");
    bind("onSurfaceChangedNative", "(ILandroid/view/Surface;III)V");
    bind("onSurfaceRedrawNeededNative", "(ILandroid/view/Surface;)V");
    bind("onInputQueueCreatedNative", "(II)V");
    bind("onInputQueueDestroyedNative", "(II)V");
    bind("onContentRectChangedNative", "(IIIII)V");
    return std::move(builder).Build();
}

}

namespace ogplay::runtime {
void ShutdownPendingIntents(dexvm::Interpreter& vm, DexVmAndroidContext& context) {
    const dexvm::VmExecutionLockScope guard(vm.ExecutionLock());
    context.pending_intents_stopping = true;
    context.alarm_operations.clear();
    context.pending_intents.clear();
}
} // namespace ogplay::runtime
