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
    auto builder = dx::IntrinsicClassBuilder::Class(
        "Landroid/app/Application;", "Landroid/content/ContextWrapper;");
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
    builder.VirtualMethod("onDestroy", "()V", lifecycle_noop,
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
    builder.FinalMethod("setContentView", "(Landroid/view/View;)V",
        [context](dx::IntrinsicContext& call) {
            const auto view = call.arguments[0].ref;
            if (!context->activity_stack.empty() && call.receiver != context->activity) {
                if (auto* ledger = call.vm.Ledger()) ledger->RecordUnimplemented("dexvm.activity_result", 0);
                throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;", "background Activity cannot replace foreground content"};
            }
            if (!view.IsValid()) {
                throw dx::VmJavaThrow{"Ljava/lang/NullPointerException;",
                                      "content view is null"};
            }
            const auto node = EnsureViewUiNode(
                *context, view, UiClassForObject(call.vm, view));
            const auto window_root = ActivityContentRoot(*context);
            const auto parent = context->ui_tree.Get(node)->parent;
            if (parent.has_value() && *parent != window_root) {
                throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;",
                                      "content view already has a parent"};
            }
            const auto old_content =
                context->ui_tree.Get(window_root)->children;
            for (const auto old_root : old_content) {
                if (old_root == node) continue;
                if (const auto error = DetachSurfaceViewSubtree(
                        call.vm, *context, old_root);
                    error.has_value()) {
                    throw dx::VmJavaThrow{"Ljava/lang/RuntimeException;", *error};
                }
                std::vector<ui::UiNodeId> pending{old_root};
                while (!pending.empty()) {
                    const auto current = pending.back();
                    pending.pop_back();
                    const auto* state = context->ui_tree.Get(current);
                    pending.insert(pending.end(), state->children.begin(),
                                   state->children.end());
                    const auto object = ViewObjectForUiNode(*context, current);
                    if (object.IsValid()) {
                        context->object_to_ui_node.erase(object.Value());
                        context->ui_view_layout_params.erase(object.Value());
                    }
                    context->ui_node_to_object.erase(current);
                    context->ui_click_listeners.erase(current);
                    context->ui_touch_listeners.erase(current);
                }
                context->ui_tree.DestroySubtree(old_root);
            }
            if (!parent.has_value()) {
                context->ui_tree.Attach(window_root, node);
                if (const auto error = context->defer_content_surface_callbacks
                        ? std::optional<std::string>{} : AttachSurfaceViewSubtree(
                        call.vm, *context, node);
                    error.has_value()) {
                    throw dx::VmJavaThrow{"Ljava/lang/RuntimeException;", *error};
                }
            }
            ui::LayoutUiTree(context->ui_tree,
                             {static_cast<std::int32_t>(context->surface_width),
                              static_cast<std::int32_t>(context->surface_height)});
            context->content_view = view;
            return dx::VmValue::Void();
        });
    builder.FinalMethod("setContentView", "(I)V",
        [context](dx::IntrinsicContext& call) {
            const auto layout_id =
                static_cast<std::uint32_t>(call.arguments[0].AsInt());
            if (!context->activity_stack.empty() && call.receiver != context->activity) {
                if (auto* ledger = call.vm.Ledger()) ledger->RecordUnimplemented("dexvm.activity_result", 0);
                throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;", "background Activity cannot replace foreground content"};
            }
            try {
                const auto old_content =
                    context->ui_tree.Get(ActivityContentRoot(*context))->children;
                for (const auto old_root : old_content) {
                    if (const auto error = DetachSurfaceViewSubtree(
                            call.vm, *context, old_root);
                        error.has_value()) {
                        throw dx::VmJavaThrow{"Ljava/lang/RuntimeException;",
                                              *error};
                    }
                }
                context->content_view = InflateUiLayoutResource(
                    call.vm, *context, layout_id, call.receiver);
                const auto node = FindViewUiNode(
                    *context, context->content_view.Value());
                if (node.has_value()) {
                    if (const auto error = context->defer_content_surface_callbacks
                            ? std::optional<std::string>{} : AttachSurfaceViewSubtree(
                            call.vm, *context, *node);
                        error.has_value()) {
                        throw dx::VmJavaThrow{"Ljava/lang/RuntimeException;",
                                              *error};
                    }
                }
                ui::LayoutUiTree(context->ui_tree,
                                 {static_cast<std::int32_t>(context->surface_width),
                                  static_cast<std::int32_t>(context->surface_height)});
            } catch (const std::runtime_error& error) {
                throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;",
                                      std::string("layout inflation failed: ") +
                                          error.what()};
            }
            return dx::VmValue::Void();
        });
    builder.FinalMethod("findViewById", "(I)Landroid/view/View;",
        [context](dx::IntrinsicContext& call) {
            if (call.arguments[0].AsInt() == -1) return dx::VmValue::Ref(dx::VmObjectRef{});
            if (!context->activity_stack.empty()) {
                const auto record = std::find_if(context->activity_stack.begin(), context->activity_stack.end(),
                    [&](const auto& item) { return item.object == call.receiver; });
                const auto content = call.receiver == context->activity ? context->content_view
                    : record != context->activity_stack.end() ? record->content : dx::VmObjectRef{};
                if (!content.IsValid())
                    return dx::VmValue::Ref(dx::VmObjectRef{});
                const auto root = FindViewUiNode(*context, content.Value());
                if (!root) return dx::VmValue::Ref(dx::VmObjectRef{});
                std::vector<ui::UiNodeId> nodes{*root};
                while (!nodes.empty()) {
                    const auto node = nodes.back(); nodes.pop_back();
                    const auto* state = context->ui_tree.Get(node);
                    if (!state) continue;
                    if (state->android_id == call.arguments[0].AsInt())
                        return dx::VmValue::Ref(ViewObjectForUiNode(*context, node));
                    nodes.insert(nodes.end(), state->children.rbegin(), state->children.rend());
                }
                return dx::VmValue::Ref(dx::VmObjectRef{});
            }
            const auto found = context->ui_tree.FindByAndroidId(
                call.arguments[0].AsInt());
            if (!found.has_value()) {
                // Absent id: null is the documented answer.
                return dx::VmValue::Ref(dx::VmObjectRef{});
            }
            return dx::VmValue::Ref(ViewObjectForUiNode(*context, *found));
        });
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
    // AOSP Activity.isTaskRoot: whether this activity is the first one of
    // its task. OGPlay runs one task per process; the manifest launcher
    // opened it, in-process startActivity handoffs did not. The retired
    // shell of a handoff keeps answering true for its own handle, as its
    // token would on the platform.
    builder.FinalMethod("isTaskRoot", "()Z",
        [context](dx::IntrinsicContext& call) {
            return dx::VmValue::Int(
                call.receiver.Value() == context->task_root_activity ? 1
                                                                     : 0);
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

Decl Declare_android_app_AlertDialog_Builder(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/app/AlertDialog$Builder;", "Ljava/lang/Object;");
    builder.Constructor("(Landroid/content/Context;)V", WidgetNoopHandler());
    const auto self = dx::IntrinsicHandler([](dx::IntrinsicContext& call) {
        return Self(call);
    });
    builder.FinalMethod("setTitle", "(Ljava/lang/CharSequence;)Landroid/app/AlertDialog$Builder;", self);
    builder.FinalMethod("setMessage", "(Ljava/lang/CharSequence;)Landroid/app/AlertDialog$Builder;", self);
    builder.FinalMethod("setCancelable", "(Z)Landroid/app/AlertDialog$Builder;", self);
    builder.FinalMethod("setOnKeyListener", "(Landroid/content/DialogInterface$OnKeyListener;)Landroid/app/AlertDialog$Builder;", self);
    builder.FinalMethod("setPositiveButton", "(Ljava/lang/CharSequence;Landroid/content/DialogInterface$OnClickListener;)Landroid/app/AlertDialog$Builder;", self);
    builder.FinalMethod("setItems", "([Ljava/lang/CharSequence;Landroid/content/DialogInterface$OnClickListener;)Landroid/app/AlertDialog$Builder;", self);
    builder.FinalMethod("create", "()Landroid/app/AlertDialog;",
        [](dx::IntrinsicContext& call) {
            return dx::VmValue::Ref(
                call.vm.NewIntrinsicInstance("Landroid/app/AlertDialog;"));
        });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics


// ---- migrated from android_app_AlertDialog.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_app_AlertDialog(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/app/AlertDialog;", "Landroid/app/Dialog;");
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics


// ---- migrated from android_app_Dialog.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_app_Dialog(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/app/Dialog;", "Ljava/lang/Object;");
    builder.Constructor("()V", NeutralHandler('V'));
    builder.FinalMethod("show", "()V", [](dx::IntrinsicContext& call) -> dx::VmValue {
        if (auto* ledger = call.vm.Ledger()) {
            ledger->RecordUnimplemented("runtime.ui.dialog.presentation", 0);
        }
        throw dx::VmJavaThrow{
            "Ljava/lang/UnsupportedOperationException;",
            "Dialog presentation is not implemented"};
    });
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
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics


namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_app_Service(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class(
        "Landroid/app/Service;", "Landroid/content/ContextWrapper;", {},
        dx::kAccPublic | dx::kAccAbstract);
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
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/app/PendingIntent;", "Ljava/lang/Object;");
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
    builder.FinalMethod("setMessage", "(Ljava/lang/CharSequence;)V", WidgetNoopHandler());
    builder.FinalMethod("setProgressStyle", "(I)V", WidgetNoopHandler());
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

void SetGlSurfaceAvailability(DexVmAndroidContext& context, const ui::UiNodeId subtree, const bool available) {
    std::vector<ui::UiNodeId> nodes{subtree};
    while (!nodes.empty()) {
        const auto node = nodes.back(); nodes.pop_back();
        const auto* state = context.ui_tree.Get(node);
        if (!state) continue;
        nodes.insert(nodes.end(), state->children.begin(), state->children.end());
        const auto view = ViewObjectForUiNode(context, node);
        if (const auto runtime = context.gl_surface_runtimes.find(view.Value()); runtime != context.gl_surface_runtimes.end()) {
            runtime->second->surface_available = available && context.ui_tree.IsVisible(node);
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
    dexvm::Interpreter& vm, DexVmAndroidContext& context,
    const std::span<const std::uint32_t> holders,
    const SurfaceHolderPhase phase) {
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

    auto& linker = vm.Linker();
    std::size_t delivered = 0;
    for (const auto holder_handle : holders) {
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
            // AOSP sets mSurfaceCreated before invoking callbacks.
            context.active_surface_holders.insert(holder_handle);
        } else if (phase == SurfaceHolderPhase::destroyed) {
            if (context.active_surface_holders.erase(holder_handle) == 0U) continue;
            context.surface_callback_sizes.erase(holder_handle);
            // AOSP clears mSurfaceCreated before invoking callbacks.
        } else if (!context.active_surface_holders.contains(holder_handle)) {
            continue;
        }

        struct DestroyScope {
            DexVmAndroidContext& context;
            std::uint32_t holder;
            bool destroying;
            ~DestroyScope() { if (destroying) context.destroying_surface_holders.erase(holder); }
        } scope{context, holder_handle, phase == SurfaceHolderPhase::destroyed};
        if (scope.destroying) context.destroying_surface_holders.insert(holder_handle);
        if (phase == SurfaceHolderPhase::changed) {
            const auto size = std::pair{context.surface_width, context.surface_height};
            const auto previous = context.surface_callback_sizes.find(holder_handle);
            if (previous != context.surface_callback_sizes.end() && previous->second == size) continue;
            context.surface_callback_sizes[holder_handle] = size;
        }

        if (phase != SurfaceHolderPhase::destroyed) {
            try {
                android_intrinsics::PublishSurfaceHolderFrame(vm, context, dx::VmObjectRef(holder_handle));
            } catch (const dx::VmJavaThrow& error) {
                return error.descriptor + ": " + error.message;
            }
        }

        const auto found = context.surface_callbacks.find(holder_handle);
        if (found == context.surface_callbacks.end()) continue;
        // SurfaceView.getSurfaceCallbacks() returns a snapshot. Guest code may
        // add/remove registrations while one callback is running.
        const auto callbacks = found->second;
        for (const auto callback : callbacks) {
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
                if (phase == SurfaceHolderPhase::changed) {
                    const auto redraw = linker.FindVtableIndex(callback_class,
                        "surfaceRedrawNeeded", "(Landroid/view/SurfaceHolder;)V");
                    if (redraw) {
                        const auto result = vm.Call(linker.Class(callback_class).vtable[*redraw],
                            std::array{dx::VmValue::Ref(callback), dx::VmValue::Ref(dx::VmObjectRef(holder_handle))});
                        if (result.exception.IsValid())
                            return "surfaceRedrawNeeded raised " + result.exception_message;
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
    const auto root = vm.ProtectReferences(std::array{callback});
    if (context.window_surface_holder.IsValid()) {
        if (const auto error = DispatchHolderCallbacks(vm, context,
                std::array{context.window_surface_holder.Value()}, SurfaceHolderPhase::destroyed); error)
            throw dexvm::VmJavaThrow{"Ljava/lang/IllegalStateException;", *error};
        context.surface_callbacks.erase(context.window_surface_holder.Value());
    }
    context.window_surface_callback = callback;
    if (!callback.IsValid()) return;
    if (!context.window_surface_holder.IsValid())
        context.window_surface_holder = vm.NewIntrinsicInstance("Landroid/view/SurfaceHolder$Impl;");
    context.surface_callbacks[context.window_surface_holder.Value()] = {callback};
    if (context.managed_host_surface_open) {
        for (const auto phase : {SurfaceHolderPhase::created, SurfaceHolderPhase::changed})
            if (const auto error = DispatchHolderCallbacks(vm, context,
                    std::array{context.window_surface_holder.Value()}, phase); error)
                throw dexvm::VmJavaThrow{"Ljava/lang/IllegalStateException;", *error};
    }
}

std::optional<std::string> DispatchSurfaceHolderCallbacks(
    dexvm::Interpreter& vm, DexVmAndroidContext& context,
    const SurfaceHolderPhase phase) {
    std::vector<std::uint32_t> holders;
    if (phase == SurfaceHolderPhase::created) {
        context.managed_host_surface_open = true;
        SetGlSurfaceAvailability(context, context.ui_tree.Root(), true);
        try { static_cast<void>(DispatchAndroidViewSizes(vm,context)); }
        catch (const dexvm::VmJavaThrow& error) { return error.descriptor + ": " + error.message; }
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
    return DispatchHolderCallbacks(vm, context, holders, phase);
}

std::optional<std::string> AttachSurfaceViewSubtree(
    dexvm::Interpreter& vm, DexVmAndroidContext& context,
    const ui::UiNodeId subtree) {
    if (!context.managed_host_surface_open ||
        !context.ui_tree.IsVisible(subtree)) {
        return std::nullopt;
    }
    if (context.ui_layout_dispatching) return std::nullopt;
    SetGlSurfaceAvailability(context, subtree, true);
    try { static_cast<void>(DispatchAndroidViewSizes(vm,context)); }
    catch (const dexvm::VmJavaThrow& error) { return error.descriptor + ": " + error.message; }
    if (!context.ui_tree.IsAttached(subtree)) return std::nullopt;
    const auto holders = SubtreeHolderHandles(context, subtree);
    if (const auto error = DispatchHolderCallbacks(
            vm, context, holders, SurfaceHolderPhase::created);
        error.has_value()) {
        return error;
    }
    return DispatchHolderCallbacks(vm, context, holders,
                                   SurfaceHolderPhase::changed);
}

std::optional<std::string> DetachSurfaceViewSubtree(
    dexvm::Interpreter& vm, DexVmAndroidContext& context,
    const ui::UiNodeId subtree) {
    if (!context.ui_tree.IsAttached(subtree)) return std::nullopt;
    SetGlSurfaceAvailability(context, subtree, false);
    const auto holders = SubtreeHolderHandles(context, subtree);
    return DispatchHolderCallbacks(vm, context, holders,
                                   SurfaceHolderPhase::destroyed);
}

std::optional<std::string> RetireSurfaceHolderGeneration(
    dexvm::Interpreter& vm, DexVmAndroidContext& context) {
    const auto error = DispatchSurfaceHolderCallbacks(
        vm, context, SurfaceHolderPhase::destroyed);
    if (error.has_value()) return error;
    context.window_surface_callback = dexvm::VmObjectRef{};
    context.window_surface_holder = dexvm::VmObjectRef{};
    context.window_input_callback = dexvm::VmObjectRef{};
    context.window_input_queue = dexvm::VmObjectRef{};
    context.holder_surfaces.clear();
    context.surface_callbacks.clear();
    context.surface_holders.clear();
    context.active_surface_holders.clear();
    context.surface_callback_sizes.clear();
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
