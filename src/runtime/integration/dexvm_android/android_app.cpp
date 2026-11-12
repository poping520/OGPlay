#include "ogplay/loader/dex.h"
#include "ogplay/runtime/dexvm/class_name_codec.h"
#include "ogplay/loader/binary_xml.h"
#include "ogplay/runtime/dexvm/class_loader_facade.h"
#include "ogplay/runtime/integration/native_activity_runtime.h"

#include "catalog.h"
#include "rom_activity.h"
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
    const auto* activities = context->rom_activities.get();
    const std::span<const RomActivityState::Record> stack =
        activities ? std::span<const RomActivityState::Record>(activities->stack)
                   : std::span<const RomActivityState::Record>{};
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

Decl Declare_local_BackupService_Class(const Context&) {
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

Decl Declare_dalvik_system_DexFile_Natives(const Context& context) {
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
    builder.StaticMethod(
        "getClassNameList", "(I)[Ljava/lang/String;",
        [context](dx::IntrinsicContext& call) {
            if (!context->sealed_apk_bytes || call.arguments[0].AsInt() != application_cookie) {
                if (auto* ledger = call.vm.Ledger()) ledger->RecordUnimplemented("dexvm.dex_file.class_names", 0);
                throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "invalid sealed APK cookie"};
            }
            const auto bytes = loader::ReadApkEntry(context->ApkBytes(), context->archive, "classes.dex");
            const auto image = loader::ParseDex(
                std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size()));
            const auto array_type = call.vm.Linker().ResolveDescriptor("[Ljava/lang/String;");
            const auto string_type = call.vm.Linker().ResolveDescriptor("Ljava/lang/String;");
            const auto array =
                call.vm.Model().NewObjectArray(array_type, string_type, static_cast<JniSize>(image.classes.size()));
            const auto roots = call.vm.ProtectReferences(std::array{array});
            for (std::size_t i = 0; i < image.classes.size(); ++i) {
                const auto& descriptor = image.types[image.classes[i].class_type_index].descriptor;
                call.vm.Model().SetObjectElement(array, static_cast<JniSize>(i),
                                                 call.vm.NewStringUtf8(dx::ClassNameCodec::ClassGetName(descriptor)));
            }
            return dx::VmValue::Ref(array);
        },
        native);
    return std::move(builder).Build();
}

Decl Declare_local_ActivityService_Class(const Context& context) {
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

Decl Declare_local_UiModeService_Class(const Context&) {
    auto builder = dx::IntrinsicClassBuilder::Class("Lorg/ogplay/framework/LocalUiModeService;",
                                                    "Landroid/app/IUiModeManager$Stub;");
    for (const auto* method : {"enableCarMode", "disableCarMode", "setNightMode"})
        RejectUnsupportedServiceMethod(builder, "android.app.IUiModeManager", method, "(I)V");
    for (const auto* method : {"getCurrentModeType", "getNightMode"})
        RejectUnsupportedServiceMethod(builder, "android.app.IUiModeManager", method, "()I");
    return std::move(builder).Build();
}

namespace {
void ShowFrontToast(dx::Interpreter& vm, const Context& context) {
    if (context->toasts.empty()) {
        context->toast_deadline_millis = std::numeric_limits<std::int64_t>::max();
        return;
    }
    const auto record = context->toasts.front();
    static_cast<void>(CallAndroidMethod(vm, record.callback, "show", "()V", {}));
    const auto delay = record.duration == 1 ? 3500 : 2000;
    const auto now = context->uptime_millis.load();
    context->toast_deadline_millis = std::min(now, std::numeric_limits<std::int64_t>::max() - delay) + delay;
    NotifyAndroidScheduler(*context);
}

void CancelToast(dx::Interpreter& vm, const Context& context,
                 std::deque<DexVmAndroidContext::ToastRecord>::iterator toast) {
    const auto callback = toast->callback;
    const auto roots = vm.ProtectReferences(std::array{callback});
    context->toasts.erase(toast);
    static_cast<void>(CallAndroidMethod(vm, callback, "hide", "()V", {}));
    ShowFrontToast(vm, context);
}
}  // namespace

void PumpRomToastQueue(dx::Interpreter& vm, const Context& context) {
    if (!context->toasts.empty() && context->uptime_millis.load() >= context->toast_deadline_millis.load())
        CancelToast(vm, context, context->toasts.begin());
}

Decl Declare_local_NotificationService_Class(const Context& context) {
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
    builder.VirtualMethod("enqueueToast", "(Ljava/lang/String;Landroid/app/ITransientNotification;I)V",
                          [context](dx::IntrinsicContext& c) {
                              const auto callback = c.arguments[1].ref;
                              if (!c.arguments[0].ref.IsValid() || !callback.IsValid()) return dx::VmValue::Void();
                              RequireNotificationPackage(c, context);
                              auto existing = std::ranges::find(context->toasts, callback,
                                                                &DexVmAndroidContext::ToastRecord::callback);
                              if (existing == context->toasts.end()) {
                                  if (context->toasts.size() >= 50U) return dx::VmValue::Void();
                                  context->toasts.push_back({callback, c.arguments[2].AsInt()});
                                  existing = std::prev(context->toasts.end());
                              } else {
                                  existing->duration = c.arguments[2].AsInt();
                              }
                              if (existing == context->toasts.begin()) ShowFrontToast(c.vm, context);
                              return dx::VmValue::Void();
                          });
    builder.VirtualMethod(
        "cancelToast", "(Ljava/lang/String;Landroid/app/ITransientNotification;)V", [context](dx::IntrinsicContext& c) {
            const auto callback = c.arguments[1].ref;
            if (!c.arguments[0].ref.IsValid() || !callback.IsValid()) return dx::VmValue::Void();
            RequireNotificationPackage(c, context);
            const auto existing =
                std::ranges::find(context->toasts, callback, &DexVmAndroidContext::ToastRecord::callback);
            if (existing != context->toasts.end()) CancelToast(c.vm, context, existing);
            return dx::VmValue::Void();
        });
    return std::move(builder).Build();
}

Decl Declare_local_WindowService_Class(const Context& context) {
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

namespace {
constexpr std::int32_t kIntentSenderBroadcast = 1;
constexpr std::int32_t kIntentSenderActivity = 2;
constexpr std::int32_t kIntentSenderService = 4;
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
    if ((kind != kIntentSenderBroadcast && kind != kIntentSenderActivity && kind != kIntentSenderService) ||
        args.Int(9) != 0 || args.Ref(2).IsValid() || args.Ref(3).IsValid() || args.Ref(8).IsValid() ||
        call.vm.Model().ArrayLength(intents) != 1)
        UnsupportedPending(
            call, "only current-user PendingIntent tokens for one activity, broadcast or service are supported");
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

Decl Declare_local_IntentSender_Class(const Context&) {
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

Decl Declare_local_AlarmService_Class(const Context& context) {
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

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_app_NativeActivity_Natives(const Context& context) {
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
