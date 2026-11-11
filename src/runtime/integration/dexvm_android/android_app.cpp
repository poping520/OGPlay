// DVM-80: API-family translation unit. Physical consolidation only.

// ---- migrated from android_app_Activity.cpp ----
// Activity handlers. setContentView(int) inflates the real binary XML
// layout through the registry inflater into the UiTree;
// presentation-only calls stay no-ops.

#include "ogplay/loader/binary_xml.h"
#include "ogplay/runtime/dexvm/class_loader_facade.h"
#include "ogplay/runtime/integration/native_activity_runtime.h"

#include "catalog.h"
#include "rom_window.h"

namespace ogplay::runtime::android_intrinsics {

namespace {
void RequireNotificationPackage(dx::IntrinsicContext& call, const Context& context) {
    if (!call.arguments[0].ref.IsValid()) throw dx::VmJavaThrow{"Ljava/lang/NullPointerException;", "packageName"};
    if (call.vm.StringUtf8(call.arguments[0].ref) != context->package_name) {
        if (auto* ledger = call.vm.Ledger()) ledger->RecordUnimplemented("dexvm.notification_cancel", 0);
        throw dx::VmJavaThrow{"Ljava/lang/SecurityException;",
                              "notification access is limited to the current application"};
    }
}
}  // namespace

namespace {
std::uint16_t ActivityFactSlot(dx::Interpreter& vm, dx::VmObjectRef object, const char* name, const char* type) {
    const auto field = vm.Linker().FindFieldRecursive(vm.Model().ObjectClass(object), name, type);
    if (!field) throw std::logic_error("ROM activity fact field is unavailable: " + std::string(name));
    return vm.Linker().Field(*field).slot;
}

dx::VmValue ActivityMemoryInfo(dx::IntrinsicContext& call, const Context& context) {
    const auto info = dx::IntrinsicCall(call).NonNullRef(0, "outInfo");
    if (!context->memory_snapshot) {
        if (auto* ledger = call.vm.Ledger()) ledger->RecordUnimplemented("dexvm.activity_memory_query", 0);
        throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;", "guest memory facts are not installed"};
    }
    const auto& memory = *context->memory_snapshot;
    const auto& policy = memory.pressure;
    constexpr std::uint64_t kBytesPerKiB = 1024;
    const auto available = memory.AvailableBytes();
    const auto threshold = policy.home_kb * kBytesPerKiB;
    const auto cached = policy.cached_kb * kBytesPerKiB;
    const auto slots = call.vm.Model().InstanceSlots(info);
    for (const auto& [name, value] : std::array{
             std::pair{"availMem", available},
             std::pair{"totalMem", memory.TotalBytes()},
             std::pair{"threshold", threshold},
             std::pair{"hiddenAppThreshold", cached},
             std::pair{"secondaryServerThreshold", policy.service_kb * kBytesPerKiB},
             std::pair{"visibleAppThreshold", policy.visible_kb * kBytesPerKiB},
             std::pair{"foregroundAppThreshold", policy.foreground_kb * kBytesPerKiB},
         }) {
        const auto slot = ActivityFactSlot(call.vm, info, name, "J");
        slots[slot] = {static_cast<std::uint32_t>(value), dx::SlotTag::wide_lo};
        slots[slot + 1U] = {static_cast<std::uint32_t>(value >> 32U), dx::SlotTag::wide_hi};
    }
    slots[ActivityFactSlot(call.vm, info, "lowMemory", "Z")] = {
        available < threshold + (cached - threshold) / 2U ? 1U : 0U, dx::SlotTag::cat1};
    return dx::VmValue::Void();
}

dx::VmValue RunningAppProcesses(dx::IntrinsicContext& call, const Context& context) {
    auto& vm = call.vm;
    const auto package = vm.NewStringUtf8(context->package_name);
    const auto package_root = vm.ProtectReferences(std::array{package});
    const auto packages = vm.Model().NewObjectArray(vm.Linker().ResolveDescriptor("[Ljava/lang/String;"),
                                                    vm.Linker().ResolveDescriptor("Ljava/lang/String;"), 1);
    const auto packages_root = vm.ProtectReferences(std::array{packages});
    vm.Model().SetObjectElement(packages, 0, package);
    const auto name =
        context->application_process_name.empty() ? package : vm.NewStringUtf8(context->application_process_name);
    const auto info = NewPlatformObject(
        vm, "Landroid/app/ActivityManager$RunningAppProcessInfo;", "(Ljava/lang/String;I[Ljava/lang/String;)V",
        {dx::VmValue::Ref(name), dx::VmValue::Int(kGuestProcessId), dx::VmValue::Ref(packages)});
    const auto info_root = vm.ProtectReferences(std::array{info});
    const auto& stack = context->activity_stack;
    const bool top = !stack.empty() && !stack.back().stopped;
    const bool visible = std::ranges::any_of(stack, [](const auto& activity) { return activity.visible; });
    const auto importance = top ? 100U : visible ? 200U : 400U;
    const auto slots = vm.Model().InstanceSlots(info);
    for (const auto& [field, value] : std::array{
             std::pair{"uid", context->application_uid},
             std::pair{"flags", stack.empty() ? 0U : 4U},
             std::pair{"importance", importance},
             std::pair{"lru", importance == 400U ? 1U : 0U},
         })
        slots[ActivityFactSlot(vm, info, field, "I")] = {value, dx::SlotTag::cat1};
    const auto list = NewPlatformObject(vm, "Ljava/util/ArrayList;", "()V", {});
    static_cast<void>(CallAndroidMethod(vm, list, "add", "(Ljava/lang/Object;)Z", {dx::VmValue::Ref(info)}));
    return dx::VmValue::Ref(list);
}
}  // namespace

Decl Declare_local_BackupService(const Context&) {
    auto builder = dx::IntrinsicClassBuilder::Class("Lorg/ogplay/framework/LocalBackupService;",
                                                    "Landroid/app/backup/IBackupManager$Stub;");
    // There is deliberately no backup transport, queue or restore session.
    const auto disabled = [](dx::IntrinsicContext& call) {
        if (auto* ledger = call.vm.Ledger()) ledger->RecordUnimplemented("dexvm.backup_disabled", 0);
    };
    builder.VirtualMethod("dataChanged", "(Ljava/lang/String;)V", [disabled](dx::IntrinsicContext& call) {
        disabled(call);
        return dx::VmValue::Void();
    });
    builder.VirtualMethod("beginRestoreSession",
                          "(Ljava/lang/String;Ljava/lang/String;)Landroid/app/backup/IRestoreSession;",
                          [disabled](dx::IntrinsicContext& call) {
                              disabled(call);
                              return dx::VmValue::Ref(dx::VmObjectRef{});
                          });
    return std::move(builder).Build();
}

Decl Declare_dalvik_system_DexFile(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Ldalvik/system/DexFile;", "Ljava/lang/Object;");
    builder.MergesBootClass();
    constexpr auto native = dx::kAccPrivate | dx::kAccStatic | dx::kAccNative;
    // One sealed application DEX is already registered with the linker.
    constexpr int application_cookie = 1;
    builder.StaticMethod(
        "openDexFileNative", "(Ljava/lang/String;Ljava/lang/String;I)I",
        [context](dx::IntrinsicContext& call) {
            if (context->sealed_apk_bytes &&
                call.vm.StringUtf8(call.arguments[0].ref) == context->package_resource_path &&
                !call.arguments[1].ref.IsValid() && call.arguments[2].AsInt() == 0)
                return dx::VmValue::Int(application_cookie);
            if (auto* ledger = call.vm.Ledger()) ledger->RecordUnimplemented("dexvm.dex_file.open", 0);
            throw dx::VmJavaThrow{"Ljava/io/IOException;", "DexFile requires the sealed application APK"};
        },
        native);
    builder.StaticMethod(
        "closeDexFile", "(I)V",
        [](dx::IntrinsicContext&) {
            return dx::VmValue::Void();  // The linked DEX remains live until VM shutdown.
        },
        native);
    builder.StaticMethod(
        "defineClassNative", "(Ljava/lang/String;Ljava/lang/ClassLoader;I)Ljava/lang/Class;",
        [](dx::IntrinsicContext& call) {
            auto& vm = call.vm;
            auto name = vm.StringUtf8(call.arguments[0].ref);
            std::replace(name.begin(), name.end(), '.', '/');
            const auto type = vm.Linker().FindClass("L" + name + ";");
            if (call.arguments[2].AsInt() != application_cookie ||
                !vm.ClassLoaders().IsRomApplicationLoader(call.arguments[1].ref) || !type ||
                vm.Linker().ClassHeader(*type).defining_loader != dx::kApplicationLoader)
                throw dx::VmJavaThrow{"Ljava/lang/ClassNotFoundException;", name};
            vm.Linker().EnsureClassLinked(*type);
            vm.Linker().MarkInitiatedBy(*type, dx::kApplicationLoader);
            return dx::VmValue::Ref(vm.Model().ClassObject(*type));
        },
        native);
    return std::move(builder).Build();
}

Decl Declare_local_ActivityService(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Lorg/ogplay/framework/LocalActivityService;",
                                                    "Landroid/app/ActivityManagerNative;");
    builder.VirtualMethod(
        "attachApplication", "(Landroid/app/IApplicationThread;)V", [context](dx::IntrinsicContext& call) {
            auto& vm = call.vm;
            const auto thread = dx::IntrinsicCall(call).NonNullRef(0, "applicationThread");
            SetRomApplicationThread(context, thread);
            const auto info = MakeApplicationInfo(call, context, true);
            const auto info_root = vm.ProtectReferences(std::array{info});
            const auto providers = ApplicationProviders(call, context, info);
            const auto providers_root = vm.ProtectReferences(std::array{providers});
            const auto resources = ResourceObject(call, context, false);
            const auto resources_root = vm.ProtectReferences(std::array{resources});
            const auto config =
                CallAndroidMethod(vm, resources, "getConfiguration", "()Landroid/content/res/Configuration;").ref;
            const auto config_root = vm.ProtectReferences(std::array{config});
            const auto compat_type = vm.Linker().ResolveDescriptor("Landroid/content/res/CompatibilityInfo;");
            const auto initialized = vm.EnsureClassInitialized(compat_type);
            if (initialized.exception.IsValid())
                throw dx::VmJavaThrow{vm.Linker().Class(initialized.exception_class).descriptor,
                                      initialized.exception_message, initialized.exception};
            const auto field = vm.Linker().FindFieldRecursive(compat_type, "DEFAULT_COMPATIBILITY_INFO",
                                                              "Landroid/content/res/CompatibilityInfo;");
            if (!field) throw std::logic_error("CompatibilityInfo default is unavailable");
            const auto compat =
                dx::VmObjectRef{vm.Linker().Class(compat_type).static_storage[vm.Linker().Field(*field).slot]};
            const auto process = vm.NewStringUtf8(
                context->application_process_name.empty() ? context->package_name : context->application_process_name);
            const auto null = dx::VmValue::Ref(dx::VmObjectRef{});
            const auto zero = dx::VmValue::Int(0);
            return CallAndroidMethod(
                vm, thread, "bindApplication",
                "(Ljava/lang/String;Landroid/content/pm/ApplicationInfo;Ljava/util/List;"
                "Landroid/content/ComponentName;Ljava/lang/String;Landroid/os/ParcelFileDescriptor;ZLandroid/os/Bundle;"
                "Landroid/app/IInstrumentationWatcher;Landroid/app/IUiAutomationConnection;IZZZ"
                "Landroid/content/res/Configuration;Landroid/content/res/CompatibilityInfo;Ljava/util/Map;Landroid/os/Bundle;)V",
                {dx::VmValue::Ref(process), dx::VmValue::Ref(info), dx::VmValue::Ref(providers), null, null, null, zero,
                 null, null, null, zero, zero, zero, zero, dx::VmValue::Ref(config), dx::VmValue::Ref(compat), null,
                 null});
        });
    builder.VirtualMethod("getMemoryInfo", "(Landroid/app/ActivityManager$MemoryInfo;)V",
                          [context](dx::IntrinsicContext& call) { return ActivityMemoryInfo(call, context); });
    builder.VirtualMethod("getRunningAppProcesses", "()Ljava/util/List;",
                          [context](dx::IntrinsicContext& call) { return RunningAppProcesses(call, context); });
    builder.VirtualMethod(
        "getDeviceConfigurationInfo", "()Landroid/content/pm/ConfigurationInfo;",
        [context](dx::IntrinsicContext& call) {
            auto& vm = call.vm;
            const auto resources = ResourceObject(call, context, false);
            const auto config =
                CallAndroidMethod(vm, resources, "getConfiguration", "()Landroid/content/res/Configuration;").ref;
            const auto config_root = vm.ProtectReferences(std::array{config});
            const auto info = NewPlatformObject(vm, "Landroid/content/pm/ConfigurationInfo;", "()V", {});
            const auto root = vm.ProtectReferences(std::array{info});
            const auto input = [&](const char* name) {
                return vm.Model().InstanceSlots(config)[ActivityFactSlot(vm, config, name, "I")].bits;
            };
            const auto set = [&](const char* name, std::uint32_t value) {
                vm.Model().InstanceSlots(info)[ActivityFactSlot(vm, info, name, "I")] = {value, dx::SlotTag::cat1};
            };
            const auto keyboard = input("keyboard"), navigation = input("navigation");
            set("reqTouchScreen", input("touchscreen"));
            set("reqKeyboardType", keyboard);
            set("reqNavigation", navigation);
            set("reqInputFeatures", (navigation == 2 || navigation == 3 ? 2U : 0U) | (keyboard > 1 ? 1U : 0U));
            set("reqGlEsVersion", kManagedGlesVersion);
            return dx::VmValue::Ref(info);
        });
    builder.VirtualMethod("checkPermission", "(Ljava/lang/String;II)I", [context](dx::IntrinsicContext& call) {
        const auto permission = call.arguments[0].ref;
        const auto same_process = call.arguments[1].AsInt() == kGuestProcessId &&
                                  static_cast<std::uint32_t>(call.arguments[2].AsInt()) == context->application_uid;
        return dx::VmValue::Int(permission.IsValid() && same_process &&
                                        context->granted_permissions.contains(call.vm.StringUtf8(permission))
                                    ? 0
                                    : -1);
    });
    AddLocalActivityBindingMethods(builder, context);
    AddRomActivityMethods(builder, context);
    AddLocalProviderMethods(builder, context);
    AddLocalIntentSenderMethods(builder, context);
    return std::move(builder).Build();
}

dx::VmObjectRef ActivityManagerForContext(dx::IntrinsicContext& call, const Context& context) {
    const auto found = context->singletons.find("activity");
    if (found != context->singletons.end()) return found->second;
    const auto owner = context->application_base_context.IsValid() ? context->application_base_context : call.receiver;
    const auto object =
        NewPlatformObject(call.vm, "Landroid/app/ActivityManager;", "(Landroid/content/Context;Landroid/os/Handler;)V",
                          {dx::VmValue::Ref(owner), dx::VmValue::Ref(dx::VmObjectRef{})});
    return context->singletons.try_emplace("activity", object).first->second;
}

Decl Declare_local_UiModeService(const Context&) {
    auto builder = dx::IntrinsicClassBuilder::Class("Lorg/ogplay/framework/LocalUiModeService;",
                                                    "Landroid/app/IUiModeManager$Stub;");
    for (const auto* method : {"enableCarMode", "disableCarMode", "setNightMode"})
        RejectUnsupportedServiceMethod(builder, "android.app.IUiModeManager", method, "(I)V");
    for (const auto* method : {"getCurrentModeType", "getNightMode"})
        RejectUnsupportedServiceMethod(builder, "android.app.IUiModeManager", method, "()I");
    return std::move(builder).Build();
}

Decl Declare_local_NotificationService(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Lorg/ogplay/framework/LocalNotificationService;",
                                                    "Landroid/app/INotificationManager$Stub;");
    const auto require_owner = [context](dx::IntrinsicContext& call, std::size_t user) {
        RequireNotificationPackage(call, context);
        if (call.arguments[user].AsInt() != 0)
            throw dx::VmJavaThrow{"Ljava/lang/SecurityException;", "notification access outside the current user"};
    };
    builder.VirtualMethod("cancelNotificationWithTag", "(Ljava/lang/String;Ljava/lang/String;II)V",
                          [require_owner](dx::IntrinsicContext& call) {
                              require_owner(call, 3);
                              return dx::VmValue::Void();
                          });
    builder.VirtualMethod("cancelAllNotifications", "(Ljava/lang/String;I)V",
                          [require_owner](dx::IntrinsicContext& call) {
                              require_owner(call, 1);
                              return dx::VmValue::Void();
                          });
    builder.VirtualMethod("enqueueNotificationWithTag",
                          "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;ILandroid/app/Notification;[II)V",
                          [require_owner](dx::IntrinsicContext& call) -> dx::VmValue {
                              require_owner(call, 6);
                              if (auto* ledger = call.vm.Ledger())
                                  ledger->RecordUnimplemented("dexvm.notification_post", 0);
                              throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;",
                                                    "notification publication is disabled"};
                          });
    return std::move(builder).Build();
}

dx::VmObjectRef NotificationManagerForContext(dx::IntrinsicContext& call, const Context& context) {
    const auto package = CallAndroidMethod(call.vm, call.receiver, "getPackageName", "()Ljava/lang/String;").ref;
    if (!package.IsValid() || call.vm.StringUtf8(package) != context->package_name) {
        if (auto* ledger = call.vm.Ledger()) ledger->RecordUnimplemented("dexvm.notification_cancel", 0);
        throw dx::VmJavaThrow{"Ljava/lang/SecurityException;",
                              "notification manager requires the current application context"};
    }
    const auto found = context->singletons.find("notification");
    if (found != context->singletons.end()) return found->second;
    auto& vm = call.vm;
    const auto type = vm.Linker().ResolveDescriptor("Landroid/app/NotificationManager;");
    const auto require = [&](const dx::VmCallOutcome& outcome) {
        if (outcome.exception.IsValid())
            throw dx::VmJavaThrow{vm.Linker().Class(outcome.exception_class).descriptor, outcome.exception_message,
                                  outcome.exception};
    };
    require(vm.EnsureClassInitialized(type));
    const auto object = vm.NewIntrinsicInstance("Landroid/app/NotificationManager;");
    const auto owner = context->application_base_context.IsValid() ? context->application_base_context : call.receiver;
    const auto roots = vm.ProtectReferences(std::array{object, owner});
    const auto constructor =
        vm.Linker().FindDirectMethod(type, "<init>", "(Landroid/content/Context;Landroid/os/Handler;)V");
    if (!constructor)
        throw dx::DexVmError(dx::DexVmErrorReason::unresolved_reference, "NotificationManager constructor is missing");
    require(vm.Call(*constructor, std::array{dx::VmValue::Ref(object), dx::VmValue::Ref(owner),
                                             dx::VmValue::Ref(dx::VmObjectRef{})}));
    context->singletons.emplace("notification", object);
    return object;
}

Decl Declare_local_WindowService(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Lorg/ogplay/framework/LocalWindowService;",
                                                    "Landroid/view/IWindowManager$Stub;");
    const auto state = [context] {
        return context->keyguard_state_provider ? context->keyguard_state_provider() : AndroidKeyguardState{};
    };
    builder.VirtualMethod("isKeyguardLocked", "()Z",
                          [state](dx::IntrinsicContext&) { return dx::VmValue::Int(state().locked ? 1 : 0); });
    builder.VirtualMethod("isKeyguardSecure", "()Z",
                          [state](dx::IntrinsicContext&) { return dx::VmValue::Int(state().secure ? 1 : 0); });
    builder.VirtualMethod("inKeyguardRestrictedInputMode", "()Z", [state](dx::IntrinsicContext&) {
        return dx::VmValue::Int(state().restricted_input ? 1 : 0);
    });
    // Managed display rotation is fixed. No orientation events can occur.
    builder.VirtualMethod("watchRotation", "(Landroid/view/IRotationWatcher;)I",
                          [](dx::IntrinsicContext&) { return dx::VmValue::Int(0); });
    AddWindowManagerMethods(builder, context);
    return std::move(builder).Build();
}

Decl Declare_android_app_Activity(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/app/Activity;", "Landroid/view/ContextThemeWrapper;");
    builder.MergesBootClass();
    const auto component = builder.BoundInstanceField("mComponent", "Landroid/content/ComponentName;", dx::kAccPrivate);
    const auto intent = builder.BoundInstanceField("mIntent", "Landroid/content/Intent;", 0);
    builder.VirtualMethod("getComponentName", "()Landroid/content/ComponentName;",
                          [component](dx::IntrinsicContext& call) {
                              return dx::VmValue::Ref(dx::IntrinsicCall(call).GetRef(component));
                          });
    builder.VirtualMethod("getLocalClassName", "()Ljava/lang/String;", [component](dx::IntrinsicContext& call) {
        const auto package = CallAndroidMethod(call.vm, call.receiver, "getPackageName", "()Ljava/lang/String;").ref;
        const auto roots = call.vm.ProtectReferences(std::array{package});
        const auto name = CallAndroidMethod(call.vm, dx::IntrinsicCall(call).GetRef(component), "getClassName",
                                            "()Ljava/lang/String;")
                              .ref;
        if (!package.IsValid()) {
            throw dx::VmJavaThrow{"Ljava/lang/NullPointerException;", "Activity package name is null"};
        }
        const auto pkg = call.vm.Model().StringValue(package);
        const auto cls = call.vm.Model().StringValue(name);
        if (!cls.starts_with(pkg) || cls.size() <= pkg.size() || cls[pkg.size()] != '.') return dx::VmValue::Ref(name);
        return dx::VmValue::Ref(call.vm.Model().NewString(cls.substr(pkg.size() + 1)));
    });
    builder.VirtualMethod("getPreferences", "(I)Landroid/content/SharedPreferences;", [](dx::IntrinsicContext& call) {
        const auto name = CallAndroidMethod(call.vm, call.receiver, "getLocalClassName", "()Ljava/lang/String;").ref;
        return CallAndroidMethod(call.vm, call.receiver, "getSharedPreferences",
                                 "(Ljava/lang/String;I)Landroid/content/SharedPreferences;",
                                 {dx::VmValue::Ref(name), call.arguments[0]});
    });
    // OGPlay only creates top-level application activities; embedded child
    // activities and ActivityGroup are outside the process compatibility boundary.
    builder.VirtualMethod(
        "isChild", "()Z", [](dx::IntrinsicContext&) { return dx::VmValue::Int(0); }, dx::kAccPublic | dx::kAccFinal);
    const auto lifecycle_noop = dx::IntrinsicHandler([](dx::IntrinsicContext&) { return dx::VmValue::Void(); });
    builder.VirtualMethod("onLowMemory", "()V", lifecycle_noop);
    builder.VirtualMethod("onRestart", "()V", lifecycle_noop, dx::kAccProtected);
    builder.VirtualMethod("onWindowFocusChanged", "(Z)V", lifecycle_noop);
    builder.VirtualMethod("hasWindowFocus", "()Z", [context](dx::IntrinsicContext& call) {
        return dx::VmValue::Int(context->window_has_focus.load() &&
                                context->window_focus_activity.load() == call.receiver.Value());
    });
    builder.VirtualMethod("onConfigurationChanged", "(Landroid/content/res/Configuration;)V", lifecycle_noop);
    const auto window = builder.BoundInstanceField("mWindow", "Landroid/view/Window;", dx::kAccPrivate);
    context->ui_java_activity_window = window;
    builder.FinalMethod("getWindow", "()Landroid/view/Window;", [window](dx::IntrinsicContext& call) {
        dx::IntrinsicCall fields(call);
        auto value = fields.GetRef(window);
        if (!value.IsValid()) {
            value = call.vm.NewIntrinsicInstance("Landroid/view/Window;");
            const auto owner = call.vm.Linker().FindFieldRecursive(call.vm.Model().ObjectClass(value), "mOgplayOwner",
                                                                   "Landroid/app/Activity;");
            if (!owner) throw dx::DexVmError(dx::DexVmErrorReason::internal_invariant, "Window owner field is missing");
            call.vm.Model().InstanceSlots(value)[call.vm.Linker().Field(*owner).slot] = {call.receiver.Value(),
                                                                                         dx::SlotTag::ref};
            const auto context_field = call.vm.Linker().FindFieldRecursive(call.vm.Model().ObjectClass(value),
                                                                           "mContext", "Landroid/content/Context;");
            if (!context_field)
                throw dx::DexVmError(dx::DexVmErrorReason::internal_invariant, "Window context field is missing");
            call.vm.Model().InstanceSlots(value)[call.vm.Linker().Field(*context_field).slot] = {call.receiver.Value(),
                                                                                                 dx::SlotTag::ref};
            fields.SetRef(window, value);
        }
        return dx::VmValue::Ref(value);
    });
    builder.FinalMethod(
        "getApplication", "()Landroid/app/Application;",
        [context](dx::IntrinsicContext&) { return dx::VmValue::Ref(context->application); },
        dx::kAccPublic | dx::kAccNative);
    builder.FinalMethod("requestWindowFeature", "(I)Z", [](dx::IntrinsicContext&) { return dx::VmValue::Int(1); });
    builder.VirtualMethod("onContentChanged", "()V", lifecycle_noop);
    const auto content_delegate = [](const char* name, const char* signature) {
        return dx::IntrinsicHandler([name, signature](dx::IntrinsicContext& call) {
            const auto window = CallAndroidMethod(call.vm, call.receiver, "getWindow", "()Landroid/view/Window;").ref;
            return CallAndroidMethod(call.vm, window, name, signature,
                                     std::vector<dx::VmValue>{call.arguments.begin(), call.arguments.end()});
        });
    };
    builder.VirtualMethod("setContentView", "(I)V", content_delegate("setContentView", "(I)V"));
    builder.VirtualMethod("setContentView", "(Landroid/view/View;)V",
                          content_delegate("setContentView", "(Landroid/view/View;)V"));
    builder.VirtualMethod(
        "setContentView", "(Landroid/view/View;Landroid/view/ViewGroup$LayoutParams;)V",
        content_delegate("setContentView", "(Landroid/view/View;Landroid/view/ViewGroup$LayoutParams;)V"));
    builder.VirtualMethod(
        "addContentView", "(Landroid/view/View;Landroid/view/ViewGroup$LayoutParams;)V",
        content_delegate("addContentView", "(Landroid/view/View;Landroid/view/ViewGroup$LayoutParams;)V"));
    builder.VirtualMethod("findViewById", "(I)Landroid/view/View;",
                          content_delegate("findViewById", "(I)Landroid/view/View;"));
    builder.VirtualMethod("getIntent", "()Landroid/content/Intent;", [intent](dx::IntrinsicContext& call) {
        return dx::VmValue::Ref(dx::IntrinsicCall(call).GetRef(intent));
    });
    builder.VirtualMethod("setIntent", "(Landroid/content/Intent;)V", [intent](dx::IntrinsicContext& call) {
        dx::IntrinsicCall(call).SetRef(intent, call.arguments[0].ref);
        return dx::VmValue::Void();
    });
    // API19's default onNewIntent is an empty application callback.
    builder.VirtualMethod(
        "onNewIntent", "(Landroid/content/Intent;)V", [](dx::IntrinsicContext&) { return dx::VmValue::Void(); },
        dx::kAccProtected);
    // API 19 Activity runs inline only on the main thread. Calls from a guest
    // worker (notably GLSurfaceView.GLThread) are posted to the main Looper and
    // dispatched by the lifecycle safe-point pump.
    builder.FinalMethod("runOnUiThread", "(Ljava/lang/Runnable;)V", [context](dx::IntrinsicContext& call) {
        const auto runnable = call.arguments[0].ref;
        if (!runnable.IsValid()) {
            throw dx::VmJavaThrow{"Ljava/lang/NullPointerException;", "runOnUiThread action is null"};
        }
        if (call.vm.CurrentContextToken() != 1U) {
            const auto main = EnsureMainLooper(call, context);
            if (!EnqueueHandlerWork(call.vm, context, main, call.receiver, runnable, dx::VmObjectRef{}, 0, true,
                                    context->uptime_millis.load())) {
                throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;", "main Looper is not accepting work"};
            }
            return dx::VmValue::Void();
        }
        auto& vm = call.vm;
        auto& linker = vm.Linker();
        const auto runnable_class = vm.Model().ObjectClass(runnable);
        const auto index = linker.FindVtableIndex(runnable_class, "run", "()V");
        if (!index.has_value()) {
            throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;", "runOnUiThread target has no run() method"};
        }
        const auto outcome =
            vm.Call(linker.Class(runnable_class).vtable[*index], std::vector<dx::VmValue>{dx::VmValue::Ref(runnable)});
        if (outcome.exception.IsValid()) {
            throw dx::VmJavaThrow{linker.Class(outcome.exception_class).descriptor, outcome.exception_message,
                                  outcome.exception};
        }
        return dx::VmValue::Void();
    });
    builder.FinalMethod("setVolumeControlStream", "(I)V", [](dx::IntrinsicContext&) { return dx::VmValue::Void(); });
    builder.FinalMethod("setRequestedOrientation", "(I)V", [context](dx::IntrinsicContext& call) {
        context->requested_orientations[call.receiver.Value()] = call.arguments[0].AsInt();
        return dx::VmValue::Void();
    });
    builder.FinalMethod("getRequestedOrientation", "()I", [context](dx::IntrinsicContext& call) {
        const auto found = context->requested_orientations.find(call.receiver.Value());
        return dx::VmValue::Int(found == context->requested_orientations.end() ? -1 : found->second);
    });
    const auto on_key_false = dx::IntrinsicHandler([](dx::IntrinsicContext&) { return dx::VmValue::Int(0); });
    builder.VirtualMethod("onKeyDown", "(ILandroid/view/KeyEvent;)Z", on_key_false);
    builder.VirtualMethod("onKeyUp", "(ILandroid/view/KeyEvent;)Z", on_key_false);
    builder.VirtualMethod("onUserInteraction", "()V", lifecycle_noop);
    builder.VirtualMethod("dispatchTouchEvent", "(Landroid/view/MotionEvent;)Z", [](dx::IntrinsicContext& call) {
        const auto event = call.arguments[0];
        if (CallAndroidMethod(call.vm, event.ref, "getAction", "()I").AsInt() == 0)
            static_cast<void>(CallAndroidMethod(call.vm, call.receiver, "onUserInteraction", "()V"));
        const auto window = CallAndroidMethod(call.vm, call.receiver, "getWindow", "()Landroid/view/Window;").ref;
        if (CallAndroidMethod(call.vm, window, "superDispatchTouchEvent", "(Landroid/view/MotionEvent;)Z", {event})
                .AsInt())
            return dx::VmValue::Int(1);
        return CallAndroidMethod(call.vm, call.receiver, "onTouchEvent", "(Landroid/view/MotionEvent;)Z", {event});
    });
    builder.VirtualMethod("onTouchEvent", "(Landroid/view/MotionEvent;)Z",
                          [](dx::IntrinsicContext&) { return dx::VmValue::Int(0); });
    builder.DirectMethod(
        "nativeStartActivityForResult", "(Landroid/content/Intent;ILandroid/os/Bundle;)V",
        [context](dx::IntrinsicContext& call) -> dx::VmValue {
            const auto reject = [&] {
                if (auto* ledger = call.vm.Ledger()) ledger->RecordUnimplemented("dexvm.activity_result", 0);
                throw dx::VmJavaThrow{
                    "Ljava/lang/UnsupportedOperationException;",
                    "Activity result launch requires the active caller, no options and standard flags"};
            };
            if (call.arguments[2].ref.IsValid() || (context->activity.IsValid() && call.receiver != context->activity))
                reject();
            if (call.arguments[1].AsInt() >= 0 &&
                (context->renderer.IsValid() || !context->active_surface_holders.empty() ||
                 context->window_surface_callback.IsValid() || AnyVideoPlaying(*context)))
                reject();
            if (!call.arguments[0].ref.IsValid())
                throw dx::VmJavaThrow{"Ljava/lang/NullPointerException;", "intent is null"};
            if (call.arguments[1].AsInt() >= 0 &&
                CallAndroidMethod(call.vm, call.arguments[0].ref, "getFlags", "()I").AsInt() != 0)
                reject();
            return StartAndroidActivity(call, context, call.arguments[1].AsInt());
        },
        dx::kAccPrivate | dx::kAccNative);
    builder.DirectMethod(
        "nativeFinish", "(ILandroid/content/Intent;)V",
        [context](dx::IntrinsicContext& call) {
            if (context->process_stopping)
                throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;", "process is stopping"};
            context->finishing_activity = call.receiver.Value();
            if (std::ranges::any_of(context->activity_stack,
                                    [&](const auto& record) { return record.object == call.receiver; }) &&
                !std::ranges::any_of(context->activity_commands, [&](const auto& command) {
                    return command.kind == DexVmAndroidContext::ActivityCommand::Kind::finish &&
                           command.owner == call.receiver;
                })) {
                context->activity_commands.push_back({DexVmAndroidContext::ActivityCommand::Kind::finish,
                                                      call.receiver,
                                                      call.arguments[1].ref,
                                                      {},
                                                      {},
                                                      -1,
                                                      call.arguments[0].AsInt()});
                context->activity_switch_pending = true;
            }
            return dx::VmValue::Void();
        },
        dx::kAccPrivate | dx::kAccNative);
    // Task groups have independent roots; foreground ordering is not task identity.
    builder.FinalMethod("isTaskRoot", "()Z", [context](dx::IntrinsicContext& call) {
        const auto found = std::find_if(context->activity_stack.begin(), context->activity_stack.end(),
                                        [&](const auto& record) { return record.object == call.receiver; });
        if (found == context->activity_stack.end()) return dx::VmValue::Int(0);
        const auto root = std::find_if(context->activity_stack.begin(), context->activity_stack.end(),
                                       [&](const auto& record) { return record.task_id == found->task_id; });
        return dx::VmValue::Int(root == found ? 1 : 0);
    });
    builder.FinalMethod("getTaskId", "()I", [context](dx::IntrinsicContext& call) {
        const auto found = std::find_if(context->activity_stack.begin(), context->activity_stack.end(),
                                        [&](const auto& record) { return record.object == call.receiver; });
        if (found == context->activity_stack.end())
            return dx::VmValue::Int(-1);  // API19 getTaskForActivity: no live token.
        return dx::VmValue::Int(found->task_id);
    });
    builder.VirtualMethod(
        "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;", [context](dx::IntrinsicContext& call) {
            if (call.arguments[0].ref.IsValid()) {
                const auto name = call.vm.StringUtf8(call.arguments[0].ref);
                if (name == "window")
                    return dx::VmValue::Ref(AndroidActivityWindowManager(call.vm, *context, call.receiver));
                if (name == "input_method")
                    return dx::VmValue::Ref(
                        Singleton(call, context, "input_method", "Landroid/view/inputmethod/InputMethodManager;"));
            }
            const auto base = call.vm.Linker().ResolveDescriptor("Landroid/view/ContextThemeWrapper;");
            const auto slot =
                call.vm.Linker().FindVtableIndex(base, "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;");
            if (!slot) throw std::logic_error("ContextThemeWrapper.getSystemService is not linked");
            const auto result = call.vm.Call(call.vm.Linker().Class(base).vtable[*slot],
                                             std::array{dx::VmValue::Ref(call.receiver), call.arguments[0]});
            if (result.exception.IsValid())
                throw dx::VmJavaThrow{call.vm.Linker().Class(result.exception_class).descriptor,
                                      result.exception_message, result.exception};
            return result.value;
        });
    builder.FinalMethod("getWindowManager", "()Landroid/view/WindowManager;", [context](dx::IntrinsicContext& call) {
        return dx::VmValue::Ref(AndroidActivityWindowManager(call.vm, *context, call.receiver));
    });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

// ---- migrated from android_app_AlertDialog_Builder.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_app_AlertDialog_Builder(const Context&) {
    return std::move(dx::IntrinsicClassBuilder::Class("Landroid/app/AlertDialog$Builder;").MergesBootClass()).Build();
}

Decl Declare_android_app_AlertDialog(const Context&) {
    return std::move(
               dx::IntrinsicClassBuilder::Class("Landroid/app/AlertDialog;", "Landroid/app/Dialog;").MergesBootClass())
        .Build();
}

Decl Declare_android_app_Dialog(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/app/Dialog;", "Ljava/lang/Object;",
                                                    {"Landroid/content/DialogInterface;"});
    builder.MergesBootClass();
    const auto generation = builder.BoundInstanceField("mGeneration", "J", dx::kAccPrivate);
    builder.DirectMethod(
        "nativeShow",
        "(Landroid/content/Context;Landroid/app/Activity;Ljava/lang/CharSequence;Ljava/lang/CharSequence;[Ljava/lang/CharSequence;J)V",
        [context, generation](dx::IntrinsicContext& call) {
            ShowAndroidDialog(call, context, generation);
            return dx::VmValue::Void();
        },
        dx::kAccPrivate | dx::kAccNative);
    builder.DirectMethod(
        "nativeDismiss", "()V",
        [context](dx::IntrinsicContext& call) {
            DismissAndroidDialog(*context, call.receiver);
            return dx::VmValue::Void();
        },
        dx::kAccPrivate | dx::kAccNative);
    builder.DirectMethod(
        "nativeVisible", "(Z)V",
        [context](dx::IntrinsicContext& call) {
            SetAndroidDialogVisible(*context, call.receiver, call.arguments[0].AsInt() != 0);
            return dx::VmValue::Void();
        },
        dx::kAccPrivate | dx::kAccNative);
    builder.DirectMethod(
        "nativeScroll", "(I)Z",
        [context](dx::IntrinsicContext& call) {
            return dx::VmValue::Int(ScrollAndroidDialog(*context, call.receiver, call.arguments[0].AsInt()));
        },
        dx::kAccPrivate | dx::kAccNative);
    builder.DirectMethod(
        "nativeFocus", "(I)I",
        [context](dx::IntrinsicContext& call) {
            return dx::VmValue::Int(FocusAndroidDialog(*context, call.receiver, call.arguments[0].AsInt()));
        },
        dx::kAccPrivate | dx::kAccNative);
    builder.StaticMethod(
        "nativeUiDegraded", "(Ljava/lang/String;)V",
        [](dx::IntrinsicContext& call) -> dx::VmValue {
            RecordUiDegraded(call.vm, call.vm.StringUtf8(call.arguments[0].ref));
            return dx::VmValue::Void();
        },
        dx::kAccStatic | dx::kAccNative);
    builder.StaticMethod(
        "nativeUnsupported", "(Ljava/lang/String;)V",
        [](dx::IntrinsicContext& call) -> dx::VmValue {
            if (auto* ledger = call.vm.Ledger()) ledger->RecordUnimplemented("runtime.ui.dialog.presentation", 0);
            throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;",
                                  "unsupported Dialog option: " + call.vm.StringUtf8(call.arguments[0].ref)};
        },
        dx::kAccStatic | dx::kAccNative);
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
constexpr std::int32_t kIntentFillFlags = 0xff;  // API19 FILL_IN_*.

[[noreturn]] void UnsupportedPending(dx::IntrinsicContext& call, const char* message) {
    if (auto* ledger = call.vm.Ledger()) ledger->RecordUnimplemented("dexvm.pending_intent", 0);
    throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;", message};
}

[[nodiscard]] dx::VmObjectRef CopyPendingIntent(dx::Interpreter& vm, dx::VmObjectRef source) {
    const auto source_root = vm.ProtectReferences(std::array{source});
    const auto initialized = vm.EnsureClassInitialized(vm.Linker().ResolveDescriptor("Landroid/content/Intent;"));
    if (initialized.exception.IsValid())
        throw dx::VmJavaThrow{vm.Linker().Class(initialized.exception_class).descriptor, initialized.exception_message,
                              initialized.exception};
    const auto snapshot = vm.NewIntrinsicInstance("Landroid/content/Intent;");
    const auto roots = vm.ProtectReferences(std::array{snapshot});
    const auto init =
        vm.Linker().FindDirectMethod(vm.Model().ObjectClass(snapshot), "<init>", "(Landroid/content/Intent;)V");
    if (!init) throw dx::DexVmError(dx::DexVmErrorReason::internal_invariant, "Intent copy constructor is not linked");
    const auto outcome = vm.Call(*init, std::vector{dx::VmValue::Ref(snapshot), dx::VmValue::Ref(source)});
    if (outcome.exception.IsValid())
        throw dx::VmJavaThrow{vm.Linker().Class(outcome.exception_class).descriptor, outcome.exception_message,
                              outcome.exception};
    return snapshot;
}

[[nodiscard]] dx::VmValue GetPendingIntent(dx::IntrinsicContext& call, const Context& context) {
    dx::IntrinsicCall args(call);
    const auto kind = args.Int(0);
    const auto request_code = args.Int(4);
    const auto flags = args.Int(7);
    const auto intents = args.NonNullRef(5, "intents");
    if ((kind != 1 && kind != 4) || args.Int(9) != 0 || args.Ref(2).IsValid() || args.Ref(3).IsValid() ||
        args.Ref(8).IsValid() || call.vm.Model().ArrayLength(intents) != 1)
        UnsupportedPending(call, "only current-user broadcast and service PendingIntent tokens are supported");
    const auto intent = call.vm.Model().GetObjectElement(intents, JniSize{0});
    if (!intent.IsValid()) throw dx::VmJavaThrow{"Ljava/lang/NullPointerException;", "intent == null"};
    if (call.vm.Model().ObjectClass(intent) != call.vm.Linker().ResolveDescriptor("Landroid/content/Intent;"))
        UnsupportedPending(call, "PendingIntent requires an ordinary API19 Intent");
    if (context->pending_intents_stopping) UnsupportedPending(call, "PendingIntent process is stopping");
    if ((flags & ~(kControlFlags | kOneShot | kIntentFillFlags)) != 0)
        UnsupportedPending(call, "unsupported API19 PendingIntent flags");
    const auto package = call.vm.StringUtf8(args.NonNullRef(1, "packageName"));
    if (package.empty() || package != context->package_name)
        UnsupportedPending(call, "PendingIntent creator must be the current APK");
    // All ordinary Intent value semantics execute the original BootDex code.
    // Provider-based MIME inference would require a separate capability.
    const auto type = CallAndroidMethod(call.vm, intent, "getType", "()Ljava/lang/String;").ref;
    const auto data = CallAndroidMethod(call.vm, intent, "getData", "()Landroid/net/Uri;").ref;
    if (data.IsValid() && !call.vm.Linker().Class(call.vm.Model().ObjectClass(data)).is_boot_dex)
        UnsupportedPending(call, "custom PendingIntent Uri implementations are unsupported");
    if (!type.IsValid() && data.IsValid()) {
        const auto scheme = CallAndroidMethod(call.vm, data, "getScheme", "()Ljava/lang/String;").ref;
        if (scheme.IsValid() && call.vm.StringUtf8(scheme) == "content")
            UnsupportedPending(call, "PendingIntent content MIME inference is unsupported");
    }
    if (CallAndroidMethod(call.vm, intent, "getSelector", "()Landroid/content/Intent;").ref.IsValid())
        UnsupportedPending(call, "PendingIntent selector resolution is unsupported");

    // Context callbacks above may park. Recheck teardown before registry access.
    if (context->pending_intents_stopping) UnsupportedPending(call, "PendingIntent process is stopping");
    for (;;) {
        // Snapshot the weak registry before calls which can allocate/collect.
        // Temporary roots keep candidate wrappers alive only for this lookup.
        std::vector<DexVmAndroidContext::PendingIntentRecord> candidates;
        std::vector<dx::VmObjectRef> candidate_roots;
        for (const auto& [_, record] : context->pending_intents) {
            if (!record.canceled && record.creator_package == package && record.kind == kind &&
                record.request_code == request_code && record.flags == (flags & ~kControlFlags)) {
                candidates.push_back(record);
                candidate_roots.push_back(record.object);
            }
        }
        const auto roots = call.vm.ProtectReferences(candidate_roots);
        dx::VmObjectRef existing;
        for (const auto& record : candidates) {
            if (CallAndroidMethod(call.vm, record.intent, "filterEquals", "(Landroid/content/Intent;)Z",
                                  {dx::VmValue::Ref(intent)})
                    .AsInt() != 0) {
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
        const auto object = NewPlatformObject(call.vm, "Lorg/ogplay/framework/LocalIntentSender;", "()V", {});
        // Copying the ROM Intent or allocating the wrapper can park this caller.
        // Candidate roots prevent their collection; retry if another caller has
        // published a new eligible token while this snapshot was being built.
        if (context->pending_intents_stopping) UnsupportedPending(call, "PendingIntent process is stopping");
        const bool published =
            std::any_of(context->pending_intents.begin(), context->pending_intents.end(), [&](const auto& entry) {
                const auto& record = entry.second;
                return !record.canceled && record.creator_package == package && record.kind == kind &&
                       record.request_code == request_code && record.flags == (flags & ~kControlFlags) &&
                       std::none_of(candidates.begin(), candidates.end(),
                                    [&](const auto& old) { return old.object == record.object; });
            });
        if (published) continue;
        context->pending_intents.emplace(
            object.Value(), DexVmAndroidContext::PendingIntentRecord{object, snapshot, package, kind, request_code,
                                                                     flags & ~kControlFlags, false});
        return dx::VmValue::Ref(object);
    }
}
}  // namespace

void AddLocalIntentSenderMethods(dx::IntrinsicClassBuilder& builder, const Context& context) {
    builder.VirtualMethod("getIntentSender",
                          "(ILjava/lang/String;Landroid/os/IBinder;Ljava/lang/String;I[Landroid/content/Intent;"
                          "[Ljava/lang/String;ILandroid/os/Bundle;I)Landroid/content/IIntentSender;",
                          [context](dx::IntrinsicContext& call) { return GetPendingIntent(call, context); });
    builder.VirtualMethod("cancelIntentSender", "(Landroid/content/IIntentSender;)V",
                          [context](dx::IntrinsicContext& call) {
                              const auto token = call.arguments[0].ref;
                              const auto found = context->pending_intents.find(token.Value());
                              if (found == context->pending_intents.end())
                                  UnsupportedPending(call, "PendingIntent has no process token");
                              found->second.canceled = true;
                              std::erase(context->alarm_operations, token);
                              return dx::VmValue::Void();
                          });
}

Decl Declare_local_IntentSender(const Context&) {
    auto builder = dx::IntrinsicClassBuilder::Class("Lorg/ogplay/framework/LocalIntentSender;",
                                                    "Landroid/content/IIntentSender$Stub;");
    builder.Constructor("()V", [](dx::IntrinsicContext& call) {
        const auto parent = call.vm.Linker().ResolveDescriptor("Landroid/content/IIntentSender$Stub;");
        const auto init = call.vm.Linker().FindDirectMethod(parent, "<init>", "()V");
        if (!init) throw std::logic_error("IIntentSender Stub constructor is unavailable");
        const auto result = call.vm.Call(*init, std::array{dx::VmValue::Ref(call.receiver)});
        if (result.exception.IsValid()) call.vm.SetPendingException(result.exception);
        return dx::VmValue::Void();
    });
    builder.VirtualMethod(
        "send", "(ILandroid/content/Intent;Ljava/lang/String;Landroid/content/IIntentReceiver;Ljava/lang/String;)I",
        [](dx::IntrinsicContext& call) -> dx::VmValue {
            UnsupportedPending(call, "PendingIntent delivery is unsupported");
        });
    return std::move(builder).Build();
}

Decl Declare_local_AlarmService(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Lorg/ogplay/framework/LocalAlarmService;",
                                                    "Landroid/app/IAlarmManager$Stub;");
    builder.VirtualMethod("remove", "(Landroid/app/PendingIntent;)V", [context](dx::IntrinsicContext& call) {
        const auto operation = call.arguments[0].ref;
        if (!operation.IsValid()) return dx::VmValue::Void();
        const auto token = CallAndroidMethod(call.vm, operation, "getTarget", "()Landroid/content/IIntentSender;").ref;
        if (!context->pending_intents.contains(token.Value()))
            UnsupportedPending(call, "alarm operation has no process token");
        std::erase(context->alarm_operations, token);
        return dx::VmValue::Void();
    });
    builder.VirtualMethod("set", "(IJJJLandroid/app/PendingIntent;Landroid/os/WorkSource;)V",
                          [](dx::IntrinsicContext& call) -> dx::VmValue {
                              if (auto* ledger = call.vm.Ledger())
                                  ledger->RecordUnimplemented("dexvm.alarm_manager", 0);
                              throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;",
                                                    "alarm scheduling is unsupported"};
                          });
    RejectUnsupportedServiceMethod(builder, "android.app.IAlarmManager", "setTime", "(J)V");
    RejectUnsupportedServiceMethod(builder, "android.app.IAlarmManager", "setTimeZone", "(Ljava/lang/String;)V");
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
    if (context.surface_view_visibility_fields) {
        dexvm::IntrinsicContext call{vm, identity.view, {}};
        if (!dexvm::IntrinsicCall(call).GetInt((*context.surface_view_visibility_fields)[2])) return false;
    }
    identity.revision = context.ui_content_revision;
    return IsAndroidUiOwnerCurrent(vm, context, identity, false);
}

void SetGlSurfaceAvailability(DexVmAndroidContext& context, const ui::UiNodeId subtree, const bool available) {
    std::vector<ui::UiNodeId> nodes{subtree};
    while (!nodes.empty()) {
        const auto node = nodes.back();
        nodes.pop_back();
        const auto* state = context.ui_tree.Get(node);
        if (!state) continue;
        nodes.insert(nodes.end(), state->children.begin(), state->children.end());
        const auto view = ViewObjectForUiNode(context, node);
        if (const auto runtime = context.gl_surface_runtimes.find(view.Value());
            runtime != context.gl_surface_runtimes.end()) {
            runtime->second->surface_available =
                available && android_intrinsics::SurfaceViewReadyForRendering(context, view);
            if (runtime->second->surface_available && runtime->second->stopped) {
                runtime->second->stopped = false;
                runtime->second->paused = false;
            }
        }
    }
}

std::vector<std::uint32_t> SubtreeHolderHandles(const DexVmAndroidContext& context, const ui::UiNodeId subtree) {
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
            if (found != context.surface_holders.end() && found->second.IsValid() &&
                seen.insert(found->second.Value()).second) {
                holders.push_back(found->second.Value());
            }
        }
        pending.insert(pending.end(), state->children.rbegin(), state->children.rend());
    }
    return holders;
}

void CompleteSurfaceTraversal(DexVmAndroidContext& context, const ui::UiNodeId subtree,
                              const std::unordered_set<ui::UiNodeId, ui::UiNodeIdHash>& pending) {
    // A callback can attach another subtree which must wait for the next traversal.
    for (const auto node : pending) {
        if (!context.ui_tree.Get(node) || !context.ui_tree.IsAttached(node) || !context.ui_tree.IsVisible(node))
            continue;
        bool incomplete = false;
        for (const auto holder : SubtreeHolderHandles(context, node)) {
            // A creation callback can attach a new SurfaceView before the
            // subsequent changed phase; it has not joined this traversal.
            if (!context.active_surface_holders.contains(holder) || !context.surface_callback_sizes.contains(holder)) {
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

std::vector<std::uint32_t> AttachedHolderHandles(const DexVmAndroidContext& context) {
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
        extra = {dx::VmValue::Int(1), dx::VmValue::Int(static_cast<std::int32_t>(context.surface_width)),
                 dx::VmValue::Int(static_cast<std::int32_t>(context.surface_height))};
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
            android_intrinsics::ResetManagedCanvas(context, holder_handle);
            // A new Surface must receive changed even when its buffer size is unchanged.
            context.surface_callback_sizes.erase(holder_handle);
            // AOSP sets mSurfaceCreated before invoking callbacks.
            context.active_surface_holders.insert(holder_handle);
            const auto requested = context.surface_requested_formats.find(holder_handle);
            context.surface_active_formats[holder_handle] =
                requested == context.surface_requested_formats.end() ? 1 : requested->second;
            context.surface_callback_formats.erase(holder_handle);
            {
                std::scoped_lock lock(context.surface_pixels_mutex);
                context.surface_buffer_queued[holder_handle] = false;
            }
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
            android_intrinsics::ResetManagedCanvas(context, holder_handle);
            context.surface_owner_identities.erase(holder_handle);
            context.surface_active_formats.erase(holder_handle);
            context.surface_callback_formats.erase(holder_handle);
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
                if (context.retire_native_surface) context.retire_native_surface(dexvm::VmObjectRef(holder));
                context.destroying_surface_holders.erase(holder);
                {
                    std::scoped_lock lock(context.surface_pixels_mutex);
                    context.surface_pixels.erase(holder);
                    context.surface_buffer_queued.erase(holder);
                }
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
        if (phase != SurfaceHolderPhase::destroyed) {
            try {
                const auto size =
                    android_intrinsics::ManagedSurfaceBufferSize(vm, context, dx::VmObjectRef(holder_handle));
                if (context.update_native_surface)
                    context.update_native_surface(dx::VmObjectRef(holder_handle),
                                                  static_cast<std::uint32_t>(size.width),
                                                  static_cast<std::uint32_t>(size.height));
                if (phase == SurfaceHolderPhase::changed) {
                    const auto requested = context.surface_requested_formats.find(holder_handle);
                    const auto format = requested == context.surface_requested_formats.end() ? 1 : requested->second;
                    context.surface_active_formats[holder_handle] = format;
                    extra = {dx::VmValue::Int(format), dx::VmValue::Int(size.width), dx::VmValue::Int(size.height)};
                    const auto dimensions =
                        std::pair{static_cast<std::uint32_t>(size.width), static_cast<std::uint32_t>(size.height)};
                    const auto previous = context.surface_callback_sizes.find(holder_handle);
                    if (previous != context.surface_callback_sizes.end() && previous->second == dimensions &&
                        context.surface_callback_formats.contains(holder_handle) &&
                        context.surface_callback_formats.at(holder_handle) == format)
                        continue;
                    context.surface_callback_formats[holder_handle] = format;
                    context.surface_callback_sizes[holder_handle] = dimensions;
                }
                android_intrinsics::PublishSurfaceHolderFrame(vm, context, dx::VmObjectRef(holder_handle));
            } catch (const dx::VmJavaThrow& error) {
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
        const auto callback_size = phase == SurfaceHolderPhase::changed
                                       ? std::optional{context.surface_callback_sizes.at(holder_handle)}
                                       : std::nullopt;
        for (const auto callback : callbacks) {
            if (!current(holder_handle)) break;
            if (callback_size) {
                const auto size =
                    android_intrinsics::ManagedSurfaceBufferSize(vm, context, dx::VmObjectRef{holder_handle});
                if (static_cast<std::uint32_t>(size.width) != callback_size->first ||
                    static_cast<std::uint32_t>(size.height) != callback_size->second)
                    break;
            }
            const auto callback_class = vm.Model().ObjectClass(callback);
            const auto index = linker.FindVtableIndex(callback_class, name, descriptor);
            if (!index.has_value()) {
                return std::string("SurfaceHolder.Callback has no ") + name + ": " +
                       linker.Class(callback_class).descriptor;
            }
            std::vector<dx::VmValue> arguments{dx::VmValue::Ref(callback),
                                               dx::VmValue::Ref(dx::VmObjectRef(holder_handle))};
            arguments.insert(arguments.end(), extra.begin(), extra.end());
            const auto outcome = vm.Call(linker.Class(callback_class).vtable[*index], arguments);
            ++delivered;
            if (!outcome.exception.IsValid()) {
                if (!current(holder_handle)) break;
                if (phase == SurfaceHolderPhase::changed) {
                    const auto redraw = linker.FindVtableIndex(callback_class, "surfaceRedrawNeeded",
                                                               "(Landroid/view/SurfaceHolder;)V");
                    if (redraw) {
                        const auto result = vm.Call(
                            linker.Class(callback_class).vtable[*redraw],
                            std::array{dx::VmValue::Ref(callback), dx::VmValue::Ref(dx::VmObjectRef(holder_handle))});
                        if (result.exception.IsValid()) return "surfaceRedrawNeeded raised " + result.exception_message;
                        if (!current(holder_handle)) break;
                    }
                }
                continue;
            }
            std::string rendered = std::string(name) + " raised " + linker.Class(outcome.exception_class).descriptor +
                                   ": " + outcome.exception_message;
            for (const auto& entry : outcome.exception_stack) {
                rendered += "\n  at " + entry.class_descriptor + "." + entry.method_name + " (pc " +
                            std::to_string(entry.pc) + ")";
            }
            return rendered;
        }
    }
    if (auto* logger = vm.Log(); logger != nullptr && delivered > 0) {
        logger->Write(core::LogLevel::info, "session.dex_lifecycle",
                      std::string("managed surface ") + name + " delivered to " + std::to_string(delivered) +
                          " holder callback(s)",
                      {}, {}, {.mode = core::RateLimitMode::none});
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
            throw dexvm::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;",
                                     "window input requires NativeActivity runtime"};
        const auto pointer = context.native_activity->InputQueuePointer(context.activity);
        if (pointer.IsNull()) return;  // takeInputQueue during onCreate, before loadNativeCode
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
        const auto holder =
            callback.IsValid() ? vm.NewIntrinsicInstance("Landroid/view/SurfaceHolder$Impl;") : dexvm::VmObjectRef{};
        {
            std::scoped_lock lock(context.surface_pixels_mutex);
            context.window_surface_holder = holder;
        }
        if (callback.IsValid()) {
            context.window_surface_holders.insert(context.window_surface_holder.Value());
            context.surface_callbacks[context.window_surface_holder.Value()] = {callback};
        }
        return;
    }
}

void ReconcileSurfaceViewVisibility(dexvm::Interpreter& vm, DexVmAndroidContext& context, const dexvm::VmObjectRef view,
                                    const bool visible) {
    if (const auto node = FindViewUiNode(context, view.Value())) context.surface_traversal_pending.insert(*node);
    // ROM updateWindow destroys synchronously. Reentrant layout/Surface/input
    // leases and worker calls defer to the next owner safe point instead.
    if (visible || context.surface_event_dispatching || context.ui_layout_dispatching || context.input_presentation ||
        vm.CurrentContextToken() != 1U || context.ui_java_owner_thread != std::this_thread::get_id())
        return;
    const auto found = context.surface_holders.find(view.Value());
    if (found == context.surface_holders.end() || !context.active_surface_holders.contains(found->second.Value()))
        return;
    const auto holder = found->second;
    const auto roots = vm.ProtectReferences(std::array{view, holder});
    context.surface_event_dispatching = true;

    struct Reset {
        bool& flag;

        ~Reset() { flag = false; }
    } reset{context.surface_event_dispatching};

    if (const auto error =
            DispatchHolderCallbacks(vm, context, std::array{holder.Value()}, SurfaceHolderPhase::destroyed))
        throw dexvm::VmJavaThrow{"Ljava/lang/IllegalStateException;", *error};
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
        android_intrinsics::RefreshSurfaceViewVisibility(vm, context, dexvm::VmObjectRef{view});
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
        for (const auto& mount : identity.mounts)
            references.push_back(mount.first);
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

std::optional<std::string> DispatchSurfaceHolderCallbacks(dexvm::Interpreter& vm, DexVmAndroidContext& context,
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
        if (context.window_surface_callback.IsValid()) holders.push_back(context.window_surface_holder.Value());
        DispatchWindowInputQueue(vm, context, true);
    } else if (phase == SurfaceHolderPhase::destroyed) {
        context.managed_host_surface_open = false;
        SetGlSurfaceAvailability(context, context.ui_tree.Root(), false);
        DispatchWindowInputQueue(vm, context, false);
        holders.assign(context.active_surface_holders.begin(), context.active_surface_holders.end());
        std::ranges::sort(holders);
    } else {
        holders.assign(context.active_surface_holders.begin(), context.active_surface_holders.end());
        std::ranges::sort(holders);
    }
    const auto error = DispatchHolderCallbacks(vm, context, holders, phase);
    if (!error && phase == SurfaceHolderPhase::changed) {
        CompleteSurfaceTraversal(context, ActivityContentRoot(context), pending);
        SetGlSurfaceAvailability(context, context.ui_tree.Root(), true);
    }
    return error;
}

std::optional<std::string> AttachSurfaceViewSubtree(dexvm::Interpreter& vm, DexVmAndroidContext& context,
                                                    const ui::UiNodeId subtree) {
    {
        android_intrinsics::MergeManagedViewTreeObservers(vm, context, ViewObjectForUiNode(context, subtree));
        context.surface_traversal_pending.insert(subtree);
        return std::nullopt;
    }
}

bool SurfaceTraversalPending(const DexVmAndroidContext& context, const ui::UiNodeId node) {
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

std::optional<std::string> RetireSurfaceHolderGeneration(dexvm::Interpreter& vm, DexVmAndroidContext& context) {
    const auto error = DispatchSurfaceHolderCallbacks(vm, context, SurfaceHolderPhase::destroyed);
    if (error.has_value()) return error;
    context.window_surface_holders.clear();
    context.surface_owner_identities.clear();
    context.window_surface_callback = dexvm::VmObjectRef{};
    context.window_input_callback = dexvm::VmObjectRef{};
    context.window_input_queue = dexvm::VmObjectRef{};
    context.holder_surfaces.clear();
    context.surface_callbacks.clear();
    context.surface_requested_formats.clear();
    context.surface_active_formats.clear();
    context.surface_callback_formats.clear();
    context.surface_holders.clear();
    context.active_surface_holders.clear();
    {
        std::scoped_lock lock(context.surface_pixels_mutex);
        context.window_surface_holder = dexvm::VmObjectRef{};
        context.surface_buffer_queued.clear();
    }
    context.surface_callback_sizes.clear();
    context.surface_traversal_pending.clear();
    context.managed_host_surface_open = false;
    return std::nullopt;
}

}  // namespace ogplay::runtime

namespace ogplay::runtime {
void AttachAndroidActivityIdentity(dexvm::Interpreter& vm, const std::shared_ptr<DexVmAndroidContext>& context,
                                   dexvm::VmObjectRef activity, const std::string& component_name) {
    using namespace android_intrinsics;
    const auto owner = vm.ProtectReferences(std::array{activity});
    auto intent = context->current_intent;
    if (!intent.IsValid()) {
        intent = vm.NewIntrinsicInstance("Landroid/content/Intent;");
        context->current_intent = intent;
        const auto init =
            vm.Linker().FindDirectMethod(vm.Linker().ResolveDescriptor("Landroid/content/Intent;"), "<init>", "()V");
        if (!init) throw dx::DexVmError(dx::DexVmErrorReason::unresolved_reference, "Intent constructor");
        const auto outcome = vm.Call(*init, std::array{dx::VmValue::Ref(intent)});
        if (outcome.exception.IsValid())
            throw dx::VmJavaThrow{vm.Linker().Class(outcome.exception_class).descriptor, outcome.exception_message,
                                  outcome.exception};
    }
    const auto input = vm.ProtectReferences(std::array{intent});
    auto component = CallAndroidMethod(vm, intent, "getComponent", "()Landroid/content/ComponentName;").ref;
    if (!component.IsValid()) {
        const auto package = vm.NewStringUtf8(context->package_name);
        const auto package_root = vm.ProtectReferences(std::array{package});
        component = NewAndroidComponentName(vm, package, vm.NewStringUtf8(component_name));
        static_cast<void>(CallAndroidMethod(vm, intent, "setComponent",
                                            "(Landroid/content/ComponentName;)Landroid/content/Intent;",
                                            {dx::VmValue::Ref(component)}));
    }
    // The launch component is stable even if the Intent is subsequently replaced.
    const auto store = [&](const char* name, const char* signature, dx::VmObjectRef value) {
        const auto field =
            vm.Linker().FindFieldRecursive(vm.Linker().ResolveDescriptor("Landroid/app/Activity;"), name, signature);
        if (!field) throw dx::DexVmError(dx::DexVmErrorReason::unresolved_reference, name);
        vm.Model().InstanceSlots(activity)[vm.Linker().Field(*field).slot] = {value.Value(), dx::SlotTag::ref};
    };
    store("mComponent", "Landroid/content/ComponentName;", component);
    store("mIntent", "Landroid/content/Intent;", intent);
    const auto theme = context->activity_themes.find(component_name);
    const auto resource = theme == context->activity_themes.end() ? context->application_theme : theme->second;
    if (resource != 0)
        static_cast<void>(CallAndroidMethod(vm, activity, "setTheme", "(I)V",
                                            {dx::VmValue::Int(static_cast<std::int32_t>(resource))}));
}
}  // namespace ogplay::runtime

namespace ogplay::runtime::android_intrinsics {
Decl Declare_android_app_NativeActivity(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/app/NativeActivity;", "Landroid/app/Activity;");
    builder.MergesBootClass();
    const auto bind = [&](const char* name, const char* signature) {
        builder.DirectMethod(
            name, signature,
            [context, name](dx::IntrinsicContext& call) {
                if (!context->native_activity) {
                    if (auto* ledger = call.vm.Ledger()) ledger->RecordUnimplemented("dexvm.native_activity", 0);
                    throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;",
                                          "NativeActivity requires the current guest process"};
                }
                return context->native_activity->Call(call, name);
            },
            dx::kAccPrivate | dx::kAccNative);
    };
    bind(
        "loadNativeCode",
        "(Ljava/lang/String;Ljava/lang/String;Landroid/os/MessageQueue;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;ILandroid/content/res/AssetManager;[B)I");
    for (const auto* name : {"unloadNativeCode", "onStartNative", "onResumeNative", "onPauseNative", "onStopNative",
                             "onConfigurationChangedNative", "onLowMemoryNative", "onSurfaceDestroyedNative"})
        bind(name, "(I)V");
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

}  // namespace ogplay::runtime::android_intrinsics

namespace ogplay::runtime {
void ShutdownPendingIntents(dexvm::Interpreter& vm, DexVmAndroidContext& context) {
    const dexvm::VmExecutionLockScope guard(vm.ExecutionLock());
    context.pending_intents_stopping = true;
    context.alarm_operations.clear();
    context.pending_intents.clear();
}
}  // namespace ogplay::runtime
