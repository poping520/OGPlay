#include "ogplay/runtime/dexvm/vm_monitors.h"
// DVM-80: API-family translation unit. Physical consolidation only.

#include "ogplay/runtime/dexvm/io_runtime.h"

// ---- migrated from android_os_AsyncTask.cpp ----
#include <array>
#include <bit>
#include <charconv>
#include <cstddef>
#include <limits>

#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {
Decl Declare_android_os_Debug_MemoryInfo(const Context&, DebugMemoryInfoFields& fields) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/os/Debug$MemoryInfo;");
    builder.MergesBootClass();
    constexpr std::array names{"dalvikPss",          "dalvikSwappablePss", "dalvikPrivateDirty", "dalvikSharedDirty",
                               "dalvikPrivateClean", "dalvikSharedClean",  "dalvikSwappedOut",   "nativePss",
                               "nativeSwappablePss", "nativePrivateDirty", "nativeSharedDirty",  "nativePrivateClean",
                               "nativeSharedClean",  "nativeSwappedOut",   "otherPss",           "otherSwappablePss",
                               "otherPrivateDirty",  "otherSharedDirty",   "otherPrivateClean",  "otherSharedClean",
                               "otherSwappedOut"};
    fields.reserve(22);
    for (const auto* name : names)
        fields.push_back(builder.BoundInstanceField(name, "I", dx::kAccPublic));
    fields.push_back(builder.BoundInstanceField("otherStats", "[I", dx::kAccPrivate));
    return std::move(builder).Build();
}

Decl Declare_android_os_Debug(const Context& context, const DebugMemoryInfoFields& fields) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/os/Debug;", "Ljava/lang/Object;");
    builder.MergesBootClass();
    builder.StaticMethod(
        "getMemoryInfo", "(Landroid/os/Debug$MemoryInfo;)V",
        [context, fields](dx::IntrinsicContext& call) {
            const dx::IntrinsicCall api(call);
            const auto info = api.NonNullRef(0, "memoryInfo");
            auto snapshot = context->process_memory_snapshot;
            try {
                if (!snapshot && context->read_process_memory) snapshot = context->read_process_memory();
                if (snapshot) ValidateGuestProcessMemorySnapshot(*snapshot);
            } catch (const std::exception& error) {
                throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;", error.what()};
            }
            if (!snapshot) {
                if (auto* ledger = call.vm.Ledger()) ledger->RecordUnimplemented("dexvm.debug_process_memory", 0);
                throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;",
                                      "guest process memory facts are unavailable"};
            }
            const auto other = api.GetRef(fields.back(), info);
            if (!other.IsValid() || call.vm.Model().ArrayLength(other) != 147)
                throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;",
                                      "MemoryInfo.otherStats has invalid shape"};
            // All validation precedes the first write; algorithms and ownership
            // of the result/array remain in the unmodified API19 value class.
            for (std::size_t heap = 0; heap < snapshot->heaps.size(); ++heap)
                for (std::size_t column = 0; column < 7; ++column)
                    api.SetInt(fields[heap * 7 + column], info, snapshot->heaps[heap][column]);
            for (std::size_t index = 0; index < snapshot->other_stats.size(); ++index)
                call.vm.Model().SetPrimitiveElement(other, static_cast<JniSize>(index),
                                                    static_cast<std::uint32_t>(snapshot->other_stats[index]));
            return dx::VmValue::Void();
        },
        dx::kAccPublic | dx::kAccStatic | dx::kAccNative);
    return std::move(builder).Build();
}

namespace {

[[nodiscard]] std::string_view BuildProperty(const std::string_view key) {
    if (key == "ro.product.cpu.abi") return "armeabi";
    if (key == "ro.build.tags") return "release-keys";
    if (key == "ro.build.version.release") return "4.4.4";
    if (key == "ro.build.version.sdk") return "19";
    if (key == "ro.build.version.codename") return "REL";
    return {};
}

[[nodiscard]] std::string ReadAndroidProperty(dx::Interpreter& vm, const Context& context, const std::string& key) {
    constexpr std::uint64_t kBytesPerMiB = 1024U * 1024U;
    if (key == "config.disable_renderscript") return "true";
    if (key == "sys.settings_system_version") return std::to_string(context->settings_system_version);
    if (key == "gsm.sim.state") return "ABSENT";
    if (key == "gsm.operator.numeric") return context->network_operator;
    if (key == "ro.opengles.version") return std::to_string(kManagedGlesVersion);
    if (key == "ro.sf.lcd_density") return std::to_string(DeviceDensityDpi(context));
    if (key == "dalvik.vm.heapgrowthlimit")
        return std::to_string(vm.Model().NormalHeapGrowthLimitBytes() / kBytesPerMiB) + "m";
    if (key == "dalvik.vm.heapsize") return std::to_string(vm.Model().MaximumHeapBytes() / kBytesPerMiB) + "m";
    return std::string(BuildProperty(key));
}

template <typename Integer>
[[nodiscard]] Integer ParseBuildProperty(const std::string_view value, const Integer fallback) {
    Integer parsed{};
    const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), parsed);
    return error == std::errc{} && end == value.data() + value.size() ? parsed : fallback;
}

}  // namespace
}  // namespace ogplay::runtime::android_intrinsics

// ---- BootDex native boundary for android_os_SystemProperties.cpp ----

#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_os_SystemProperties(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/os/SystemProperties;");
    builder.MergesBootClass();
    constexpr auto flags = dx::kAccPrivate | dx::kAccNative;
    builder.StaticMethod(
        "native_get", "(Ljava/lang/String;)Ljava/lang/String;",
        [context](dx::IntrinsicContext& call) {
            const auto key = call.vm.StringUtf8(call.arguments[0].ref);
            if (key == "qemu.hw.mainkeys") return MakeString(call, call.vm.GetSystemProperty(key).value_or(""));
            return MakeString(call, ReadAndroidProperty(call.vm, context, key));
        },
        flags);
    builder.StaticMethod(
        "native_get", "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;",
        [context](dx::IntrinsicContext& call) {
            const auto value = ReadAndroidProperty(call.vm, context, call.vm.StringUtf8(call.arguments[0].ref));
            if (!value.empty()) return MakeString(call, value);
            return dx::VmValue::Ref(call.arguments[1].ref.IsValid() ? call.arguments[1].ref
                                                                    : call.vm.NewStringUtf8(""));
        },
        flags);
    builder.StaticMethod(
        "native_get_int", "(Ljava/lang/String;I)I",
        [context](dx::IntrinsicContext& call) {
            return dx::VmValue::Int(ParseBuildProperty<std::int32_t>(
                ReadAndroidProperty(call.vm, context, call.vm.StringUtf8(call.arguments[0].ref)),
                call.arguments[1].AsInt()));
        },
        flags);
    builder.StaticMethod(
        "native_get_long", "(Ljava/lang/String;J)J",
        [context](dx::IntrinsicContext& call) {
            return dx::VmValue::Long(ParseBuildProperty<std::int64_t>(
                ReadAndroidProperty(call.vm, context, call.vm.StringUtf8(call.arguments[0].ref)),
                call.arguments[1].AsLong()));
        },
        flags);
    builder.StaticMethod(
        "native_get_boolean", "(Ljava/lang/String;Z)Z",
        [context](dx::IntrinsicContext& call) {
            const auto value = ReadAndroidProperty(call.vm, context, call.vm.StringUtf8(call.arguments[0].ref));
            if (value == "1" || value == "y" || value == "yes" || value == "true" || value == "on")
                return dx::VmValue::Int(1);
            if (value == "0" || value == "n" || value == "no" || value == "false" || value == "off")
                return dx::VmValue::Int(0);
            return dx::VmValue::Int(call.arguments[1].AsInt());
        },
        flags);
    const auto unsupported = [](dx::IntrinsicContext& call) -> dx::VmValue {
        if (auto* ledger = call.vm.Ledger()) ledger->RecordUnimplemented("dexvm.system_properties_write", 0);
        throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;",
                              "system property mutation and callbacks are unsupported"};
    };
    builder.StaticMethod("native_set", "(Ljava/lang/String;Ljava/lang/String;)V", unsupported, flags);
    // Read-only properties never publish a change. Java retains the callback list.
    builder.StaticMethod(
        "native_add_change_callback", "()V", [](dx::IntrinsicContext&) { return dx::VmValue::Void(); }, flags);
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

// ---- migrated from android_os_Bundle.cpp ----
// Bundle handlers store key/value pairs in the per-session bundle map
// keyed by the receiver object handle.

namespace ogplay::runtime::android_intrinsics {}  // namespace ogplay::runtime::android_intrinsics

// ---- migrated from android_os_CountDownTimer.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {}  // namespace ogplay::runtime::android_intrinsics

// ---- migrated from android_os_Environment.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_local_MountService(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Lorg/ogplay/framework/LocalMountService;",
                                                    "Landroid/os/storage/IMountService$Stub;");
    builder.VirtualMethod(
        "getVolumeList", "()[Landroid/os/storage/StorageVolume;", [context](dx::IntrinsicContext& call) {
            const auto path = call.vm.NewStringUtf8(context->external_storage_root);
            const auto file =
                NewPlatformObject(call.vm, "Ljava/io/File;", "(Ljava/lang/String;)V", {dx::VmValue::Ref(path)});
            const auto file_root = call.vm.ProtectReferences(std::array{file});
            const auto user = NewPlatformObject(call.vm, "Landroid/os/UserHandle;", "(I)V", {dx::VmValue::Int(0)});
            const auto volume = NewPlatformObject(call.vm, "Landroid/os/storage/StorageVolume;",
                                                  "(Ljava/io/File;IZZZIZJLandroid/os/UserHandle;)V",
                                                  {dx::VmValue::Ref(file), dx::VmValue::Int(0), dx::VmValue::Int(1),
                                                   dx::VmValue::Int(0), dx::VmValue::Int(0), dx::VmValue::Int(0),
                                                   dx::VmValue::Int(0), dx::VmValue::Long(0), dx::VmValue::Ref(user)});
            const auto volume_root = call.vm.ProtectReferences(std::array{volume});
            auto& linker = call.vm.Linker();
            const auto array =
                call.vm.Model().NewObjectArray(linker.ResolveDescriptor("[Landroid/os/storage/StorageVolume;"),
                                               linker.ResolveDescriptor("Landroid/os/storage/StorageVolume;"), 1);
            call.vm.Model().SetObjectElement(array, 0, volume);
            return dx::VmValue::Ref(array);
        });
    builder.VirtualMethod(
        "getVolumeState", "(Ljava/lang/String;)Ljava/lang/String;", [context](dx::IntrinsicContext& call) {
            const auto path = dx::IntrinsicCall(call).NonNullRef(0, "mountPoint");
            const auto actual = call.vm.StringUtf8(path);
            const auto canonical = context->vfs ? context->vfs->CanonicalPath(context->external_storage_root)
                                                : context->external_storage_root;
            return MakeString(call,
                              actual == canonical || actual == context->external_storage_root ? "mounted" : "unknown");
        });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

// ---- migrated from android_os_Handler.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_os_Binder(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/os/Binder;");
    builder.MergesBootClass();
    constexpr auto public_native = dx::kAccPublic | dx::kAccStatic | dx::kAccFinal | dx::kAccNative;
    constexpr auto private_native = dx::kAccPrivate | dx::kAccFinal | dx::kAccNative;
    builder.StaticMethod(
        "getCallingPid", "()I", [](dx::IntrinsicContext&) { return dx::VmValue::Int(kGuestProcessId); }, public_native);
    builder.StaticMethod(
        "getCallingUid", "()I",
        [context](dx::IntrinsicContext&) {
            return dx::VmValue::Int(static_cast<std::int32_t>(context->application_uid));
        },
        public_native);
    const auto process_identity = [context] {
        return (static_cast<std::uint64_t>(context->application_uid) << 32U) | kGuestProcessId;
    };
    builder.StaticMethod(
        "clearCallingIdentity", "()J",
        [process_identity](dx::IntrinsicContext&) {
            return dx::VmValue::Long(std::bit_cast<std::int64_t>(process_identity()));
        },
        public_native);
    builder.StaticMethod(
        "restoreCallingIdentity", "(J)V",
        [process_identity](dx::IntrinsicContext& call) {
            if (std::bit_cast<std::uint64_t>(call.arguments[0].AsLong()) != process_identity())
                throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;", "invalid local Binder identity"};
            return dx::VmValue::Void();
        },
        public_native);
    builder.StaticMethod(
        "setThreadStrictModePolicy", "(I)V",
        [context](dx::IntrinsicContext& call) {
            context->binder_threads[call.vm.CurrentContextToken()].strict_mode_policy = call.arguments[0].AsInt();
            return dx::VmValue::Void();
        },
        public_native);
    builder.StaticMethod(
        "getThreadStrictModePolicy", "()I",
        [context](dx::IntrinsicContext& call) {
            return dx::VmValue::Int(context->binder_threads[call.vm.CurrentContextToken()].strict_mode_policy);
        },
        public_native);
    builder.StaticMethod(
        "flushPendingCommands", "()V", [](dx::IntrinsicContext&) { return dx::VmValue::Void(); }, public_native);
    builder.StaticMethod(
        "joinThreadPool", "()V",
        [](dx::IntrinsicContext& call) -> dx::VmValue {
            if (auto* ledger = call.vm.Ledger()) ledger->RecordUnimplemented("android.os.Binder.joinThreadPool()V", 0);
            throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;",
                                  "remote Binder thread pool is unsupported"};
        },
        public_native);
    builder.DirectMethod("init", "()V", [](dx::IntrinsicContext&) { return dx::VmValue::Void(); }, private_native);
    builder.DirectMethod("destroy", "()V", [](dx::IntrinsicContext&) { return dx::VmValue::Void(); }, private_native);
    return std::move(builder).Build();
}

namespace {
constexpr char kLocalServiceManager[] = "Lorg/ogplay/framework/LocalServiceManager;";

struct LocalService {
    const char* name;
    const char* descriptor;
    const char* stub;
};

constexpr std::array kLocalServices{
    LocalService{"activity", "Lorg/ogplay/framework/LocalActivityService;", "Landroid/app/ActivityManagerNative;"},
    LocalService{"alarm", "Lorg/ogplay/framework/LocalAlarmService;", "Landroid/app/IAlarmManager$Stub;"},
    LocalService{"audio", "Lorg/ogplay/framework/LocalAudioService;", "Landroid/media/IAudioService$Stub;"},
    LocalService{"backup", "Lorg/ogplay/framework/LocalBackupService;", "Landroid/app/backup/IBackupManager$Stub;"},
    LocalService{"clipboard", "Lorg/ogplay/framework/LocalClipboardService;", "Landroid/content/IClipboard$Stub;"},
    LocalService{"connectivity", "Lorg/ogplay/framework/LocalConnectivityService;",
                 "Landroid/net/IConnectivityManager$Stub;"},
    LocalService{"content", "Lorg/ogplay/framework/LocalContentService;", "Landroid/content/IContentService$Stub;"},
    LocalService{"display", "Lorg/ogplay/framework/LocalDisplayService;",
                 "Landroid/hardware/display/IDisplayManager$Stub;"},
    LocalService{"iphonesubinfo", "Lorg/ogplay/framework/LocalPhoneSubscriberService;",
                 "Lcom/android/internal/telephony/IPhoneSubInfo$Stub;"},
    LocalService{"isms", "Lorg/ogplay/framework/LocalSmsService;", "Lcom/android/internal/telephony/ISms$Stub;"},
    LocalService{"location", "Lorg/ogplay/framework/LocalLocationService;", "Landroid/location/ILocationManager$Stub;"},
    LocalService{"mount", "Lorg/ogplay/framework/LocalMountService;", "Landroid/os/storage/IMountService$Stub;"},
    LocalService{"notification", "Lorg/ogplay/framework/LocalNotificationService;",
                 "Landroid/app/INotificationManager$Stub;"},
    LocalService{"package", "Lorg/ogplay/framework/LocalPackageService;", "Landroid/content/pm/IPackageManager$Stub;"},
    LocalService{"phone", "Lorg/ogplay/framework/LocalTelephonyService;",
                 "Lcom/android/internal/telephony/ITelephony$Stub;"},
    LocalService{"power", "Lorg/ogplay/framework/LocalPowerService;", "Landroid/os/IPowerManager$Stub;"},
    LocalService{"telephony.registry", "Lorg/ogplay/framework/LocalTelephonyRegistry;",
                 "Lcom/android/internal/telephony/ITelephonyRegistry$Stub;"},
    LocalService{"uimode", "Lorg/ogplay/framework/LocalUiModeService;", "Landroid/app/IUiModeManager$Stub;"},
    LocalService{"usb", "Lorg/ogplay/framework/LocalUsbService;", "Landroid/hardware/usb/IUsbManager$Stub;"},
    LocalService{"vibrator", "Lorg/ogplay/framework/LocalVibratorService;", "Landroid/os/IVibratorService$Stub;"},
    LocalService{"wifi", "Lorg/ogplay/framework/LocalWifiService;", "Landroid/net/wifi/IWifiManager$Stub;"},
    LocalService{"window", "Lorg/ogplay/framework/LocalWindowService;", "Landroid/view/IWindowManager$Stub;"},
};

dx::VmValue RequireLocalBinderCall(dx::Interpreter& vm, const dx::VmCallOutcome& outcome) {
    if (outcome.exception.IsValid())
        throw dx::VmJavaThrow{vm.Linker().Class(outcome.exception_class).descriptor, outcome.exception_message,
                              outcome.exception};
    return outcome.value;
}

dx::VmObjectRef ConstructLocalBinder(dx::Interpreter& vm, const char* descriptor, const char* base) {
    const auto binder = vm.NewIntrinsicInstance(descriptor);
    const auto root = vm.ProtectReferences(std::array{binder});
    const auto parent = vm.Linker().ResolveDescriptor(base);
    const auto init = vm.Linker().FindDirectMethod(parent, "<init>", "()V");
    if (!init) throw std::logic_error("local Binder base constructor is unavailable");
    static_cast<void>(RequireLocalBinderCall(vm, vm.Call(*init, std::array{dx::VmValue::Ref(binder)})));
    return binder;
}
}  // namespace

dx::VmObjectRef NewPlatformObject(dx::Interpreter& vm, const char* type, const char* signature,
                                  std::vector<dx::VmValue> arguments) {
    std::vector<dx::VmObjectRef> refs;
    for (const auto& arg : arguments)
        if (arg.kind == dx::VmValue::Kind::ref) refs.push_back(arg.ref);
    const auto roots = vm.ProtectReferences(refs);
    const auto owner = vm.Linker().ResolveDescriptor(type);
    static_cast<void>(RequireLocalBinderCall(vm, vm.EnsureClassInitialized(owner)));
    const auto object = vm.NewIntrinsicInstance(type);
    const auto root = vm.ProtectReferences(std::array{object});
    const auto init = vm.Linker().FindDirectMethod(owner, "<init>", signature);
    if (!init) throw std::logic_error("platform constructor is unavailable: " + std::string(type));
    arguments.insert(arguments.begin(), dx::VmValue::Ref(object));
    static_cast<void>(RequireLocalBinderCall(vm, vm.Call(*init, arguments)));
    return object;
}

dx::VmObjectRef GetLocalServiceInterface(dx::Interpreter& vm, const char* name, const char* stub,
                                         const char* interface_type) {
    auto& linker = vm.Linker();
    const auto manager = linker.ResolveDescriptor("Landroid/os/ServiceManager;");
    const auto get = linker.FindDirectMethod(manager, "getService", "(Ljava/lang/String;)Landroid/os/IBinder;");
    const auto convert = linker.FindDirectMethod(linker.ResolveDescriptor(stub), "asInterface",
                                                 std::string("(Landroid/os/IBinder;)") + interface_type);
    if (!get || !convert) throw std::logic_error("local Binder service entry is unavailable");
    const auto service_name = vm.NewStringUtf8(name);
    const auto binder = RequireLocalBinderCall(vm, vm.Call(*get, std::array{dx::VmValue::Ref(service_name)})).ref;
    return RequireLocalBinderCall(vm, vm.Call(*convert, std::array{dx::VmValue::Ref(binder)})).ref;
}

void RejectUnsupportedServiceMethod(dx::IntrinsicClassBuilder& builder, std::string_view owner, const char* name,
                                    const char* descriptor) {
    const auto signature = std::string(owner) + "." + name + descriptor;
    builder.VirtualMethod(name, descriptor, [signature](dx::IntrinsicContext& call) -> dx::VmValue {
        if (auto* ledger = call.vm.Ledger()) ledger->RecordUnimplemented(signature, 0);
        throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;", signature};
    });
}

Decl Declare_local_ServiceManager(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class(kLocalServiceManager, "Landroid/os/ServiceManagerNative;");
    const auto lookup = [context](dx::IntrinsicContext& call) {
        const auto name = call.arguments[0].ref.IsValid() ? call.vm.StringUtf8(call.arguments[0].ref) : "null";
        if (const auto cached = context->local_binder_services.find(name);
            cached != context->local_binder_services.end())
            return dx::VmValue::Ref(cached->second);
        for (const auto& service : kLocalServices) {
            if (name != service.name) continue;
            const auto binder = ConstructLocalBinder(call.vm, service.descriptor, service.stub);
            return dx::VmValue::Ref(context->local_binder_services.try_emplace(name, binder).first->second);
        }
        if (auto* ledger = call.vm.Ledger()) ledger->RecordUnimplemented("dexvm.service_manager." + name, 0);
        return dx::VmValue::Ref(dx::VmObjectRef{});
    };
    builder.VirtualMethod("getService", "(Ljava/lang/String;)Landroid/os/IBinder;", lookup);
    builder.VirtualMethod("checkService", "(Ljava/lang/String;)Landroid/os/IBinder;", lookup);
    builder.VirtualMethod("listServices", "()[Ljava/lang/String;", [](dx::IntrinsicContext& call) {
        auto& linker = call.vm.Linker();
        const auto string_type = linker.ResolveDescriptor("Ljava/lang/String;");
        const auto array_type = linker.ResolveDescriptor("[Ljava/lang/String;");
        const auto result = call.vm.Model().NewObjectArray(array_type, string_type, kLocalServices.size());
        const auto root = call.vm.ProtectReferences(std::array{result});
        JniSize index = 0;
        for (const auto& service : kLocalServices)
            call.vm.Model().SetObjectElement(result, index++, call.vm.NewStringUtf8(service.name));
        return dx::VmValue::Ref(result);
    });
    RejectUnsupportedServiceMethod(builder, "android.os.IServiceManager", "addService",
                                   "(Ljava/lang/String;Landroid/os/IBinder;Z)V");
    RejectUnsupportedServiceMethod(builder, "android.os.IServiceManager", "setPermissionController",
                                   "(Landroid/os/IPermissionController;)V");
    return std::move(builder).Build();
}

Decl Declare_com_android_internal_os_BinderInternal(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Lcom/android/internal/os/BinderInternal;");
    builder.MergesBootClass();
    builder.StaticMethod(
        "getContextObject", "()Landroid/os/IBinder;",
        [context](dx::IntrinsicContext& call) {
            constexpr auto key = "binder.context";
            if (const auto found = context->singletons.find(key); found != context->singletons.end())
                return dx::VmValue::Ref(found->second);
            const auto binder =
                ConstructLocalBinder(call.vm, kLocalServiceManager, "Landroid/os/ServiceManagerNative;");
            return dx::VmValue::Ref(context->singletons.try_emplace(key, binder).first->second);
        },
        dx::kAccPublic | dx::kAccStatic | dx::kAccFinal | dx::kAccNative);
    builder.StaticMethod(
        "handleGc", "()V", [](dx::IntrinsicContext&) { return dx::VmValue::Void(); },
        dx::kAccStatic | dx::kAccFinal | dx::kAccNative);
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

namespace ogplay::runtime::android_intrinsics {
Decl Declare_android_os_SystemClock(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/os/SystemClock;");
    builder.MergesBootClass();
    constexpr auto native_flags = dx::kAccPublic | dx::kAccStatic | dx::kAccNative;
    const auto millis = [context](dx::IntrinsicContext&) { return dx::VmValue::Long(context->uptime_millis.load()); };
    builder.StaticMethod("uptimeMillis", "()J", millis, native_flags);
    builder.StaticMethod("elapsedRealtime", "()J", millis, native_flags);
    builder.StaticMethod(
        "elapsedRealtimeNanos", "()J",
        [context](dx::IntrinsicContext&) { return dx::VmValue::Long(context->uptime_millis.load() * 1'000'000LL); },
        native_flags);
    builder.StaticMethod("currentThreadTimeMillis", "()J", millis, native_flags);
    builder.StaticMethod(
        "setCurrentTimeMillis", "(J)Z", [](dx::IntrinsicContext&) { return dx::VmValue::Int(0); }, native_flags);
    return std::move(builder).Build();
}
}  // namespace ogplay::runtime::android_intrinsics

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_os_Parcel(const Context& context) {
    using Backing = DexVmAndroidContext::ParcelBacking;
    constexpr std::size_t kMaxParcelBytes = 16U * 1024U * 1024U;
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/os/Parcel;");
    builder.MergesBootClass();

    const auto require = [context](const std::int32_t raw) -> Backing& {
        if (raw <= 0) throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;", "invalid Parcel token"};
        const auto found = context->parcel_backings.find(static_cast<std::uint32_t>(raw));
        if (found == context->parcel_backings.end())
            throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;", "stale Parcel token"};
        return found->second;
    };
    const auto resize = [](Backing& parcel, const std::size_t size) {
        if (size > kMaxParcelBytes)
            throw dx::VmJavaThrow{"Ljava/lang/OutOfMemoryError;", "Parcel exceeds OGPlay limit"};
        parcel.bytes.resize(size);
        if (parcel.position > size) parcel.position = size;
        std::erase_if(parcel.binders, [size](const auto& record) {
            return record.offset >= size || record.span > size - record.offset;
        });
    };
    const auto write = [resize](Backing& parcel, const std::span<const std::byte> bytes) {
        if (bytes.size() > kMaxParcelBytes - parcel.position)
            throw dx::VmJavaThrow{"Ljava/lang/OutOfMemoryError;", "Parcel exceeds OGPlay limit"};
        const auto end = parcel.position + bytes.size();
        if (end > parcel.bytes.size()) resize(parcel, end);
        std::erase_if(parcel.binders, [start = parcel.position, end](const auto& record) {
            return record.offset < end && start < record.offset + record.span;
        });
        std::copy(bytes.begin(), bytes.end(), parcel.bytes.begin() + static_cast<std::ptrdiff_t>(parcel.position));
        parcel.position = end;
    };
    const auto read = [](Backing& parcel, const std::size_t count) {
        if (count > parcel.bytes.size() - std::min(parcel.position, parcel.bytes.size()))
            throw dx::VmJavaThrow{"Ljava/lang/IndexOutOfBoundsException;", "Parcel underflow"};
        const auto start = parcel.position;
        parcel.position += count;
        return std::span<const std::byte>(parcel.bytes).subspan(start, count);
    };
    const auto write32 = [write](Backing& parcel, const std::uint32_t value) {
        const std::array bytes{std::byte(value), std::byte(value >> 8U), std::byte(value >> 16U),
                               std::byte(value >> 24U)};
        write(parcel, bytes);
    };
    const auto read32 = [read](Backing& parcel) {
        const auto bytes = read(parcel, 4U);
        return std::uint32_t(bytes[0]) | (std::uint32_t(bytes[1]) << 8U) | (std::uint32_t(bytes[2]) << 16U) |
               (std::uint32_t(bytes[3]) << 24U);
    };
    const auto write64 = [write](Backing& parcel, const std::uint64_t value) {
        std::array<std::byte, 8> bytes{};
        for (std::size_t i = 0; i < bytes.size(); ++i)
            bytes[i] = std::byte(value >> (i * 8U));
        write(parcel, bytes);
    };
    const auto read64 = [read](Backing& parcel) {
        const auto bytes = read(parcel, 8U);
        std::uint64_t value{};
        for (std::size_t i = 0; i < bytes.size(); ++i)
            value |= std::uint64_t(bytes[i]) << (i * 8U);
        return value;
    };
    const auto native_flags = dx::kAccPrivate | dx::kAccStatic | dx::kAccNative;

    builder.StaticMethod(
        "nativeCreate", "()I",
        [context](dx::IntrinsicContext&) {
            const auto token = context->next_parcel_token;
            if (token > static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max()))
                throw dx::VmJavaThrow{"Ljava/lang/OutOfMemoryError;", "Parcel token space exhausted"};
            context->parcel_backings.emplace(token, Backing{});
            ++context->next_parcel_token;
            return dx::VmValue::Int(static_cast<std::int32_t>(token));
        },
        native_flags);
    builder.StaticMethod(
        "nativeFreeBuffer", "(I)V",
        [require](dx::IntrinsicContext& call) {
            auto& parcel = require(call.arguments[0].AsInt());
            parcel.bytes.clear();
            parcel.position = 0;
            parcel.binders.clear();
            return dx::VmValue::Void();
        },
        native_flags);
    builder.StaticMethod(
        "nativeDestroy", "(I)V",
        [context](dx::IntrinsicContext& call) {
            const auto token = static_cast<std::uint32_t>(call.arguments[0].AsInt());
            context->parcel_backings.erase(token);
            return dx::VmValue::Void();
        },
        native_flags);
    builder.StaticMethod(
        "nativeDataSize", "(I)I",
        [require](dx::IntrinsicContext& call) {
            return dx::VmValue::Int(static_cast<std::int32_t>(require(call.arguments[0].AsInt()).bytes.size()));
        },
        native_flags);
    builder.StaticMethod(
        "nativeDataAvail", "(I)I",
        [require](dx::IntrinsicContext& call) {
            const auto& p = require(call.arguments[0].AsInt());
            return dx::VmValue::Int(static_cast<std::int32_t>(p.bytes.size() - std::min(p.position, p.bytes.size())));
        },
        native_flags);
    builder.StaticMethod(
        "nativeDataPosition", "(I)I",
        [require](dx::IntrinsicContext& call) {
            return dx::VmValue::Int(static_cast<std::int32_t>(require(call.arguments[0].AsInt()).position));
        },
        native_flags);
    builder.StaticMethod(
        "nativeDataCapacity", "(I)I",
        [require](dx::IntrinsicContext& call) {
            return dx::VmValue::Int(static_cast<std::int32_t>(require(call.arguments[0].AsInt()).bytes.capacity()));
        },
        native_flags);
    builder.StaticMethod(
        "nativeSetDataSize", "(II)V",
        [require, resize](dx::IntrinsicContext& call) {
            if (call.arguments[1].AsInt() < 0) throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "size"};
            resize(require(call.arguments[0].AsInt()), static_cast<std::size_t>(call.arguments[1].AsInt()));
            return dx::VmValue::Void();
        },
        native_flags);
    builder.StaticMethod(
        "nativeSetDataPosition", "(II)V",
        [require](dx::IntrinsicContext& call) {
            auto& p = require(call.arguments[0].AsInt());
            const auto pos = call.arguments[1].AsInt();
            if (pos < 0 || static_cast<std::size_t>(pos) > p.bytes.size())
                throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "position"};
            p.position = static_cast<std::size_t>(pos);
            return dx::VmValue::Void();
        },
        native_flags);
    builder.StaticMethod(
        "nativeSetDataCapacity", "(II)V",
        [require](dx::IntrinsicContext& call) {
            auto& p = require(call.arguments[0].AsInt());
            const auto size = call.arguments[1].AsInt();
            if (size < 0 || static_cast<std::size_t>(size) > kMaxParcelBytes)
                throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "capacity"};
            p.bytes.reserve(static_cast<std::size_t>(size));
            return dx::VmValue::Void();
        },
        native_flags);
    builder.StaticMethod(
        "nativePushAllowFds", "(IZ)Z",
        [require](dx::IntrinsicContext& call) {
            auto& p = require(call.arguments[0].AsInt());
            const bool old = p.allow_fds;
            p.allow_fds = call.arguments[1].AsInt() != 0;
            return dx::VmValue::Int(old ? 1 : 0);
        },
        native_flags);
    builder.StaticMethod(
        "nativeRestoreAllowFds", "(IZ)V",
        [require](dx::IntrinsicContext& call) {
            require(call.arguments[0].AsInt()).allow_fds = call.arguments[1].AsInt() != 0;
            return dx::VmValue::Void();
        },
        native_flags);
    builder.StaticMethod(
        "nativeWriteInt", "(II)V",
        [require, write32](dx::IntrinsicContext& call) {
            write32(require(call.arguments[0].AsInt()), static_cast<std::uint32_t>(call.arguments[1].AsInt()));
            return dx::VmValue::Void();
        },
        native_flags);
    builder.StaticMethod(
        "nativeReadInt", "(I)I",
        [require, read32](dx::IntrinsicContext& call) {
            return dx::VmValue::Int(static_cast<std::int32_t>(read32(require(call.arguments[0].AsInt()))));
        },
        native_flags);
    builder.StaticMethod(
        "nativeWriteLong", "(IJ)V",
        [require, write64](dx::IntrinsicContext& call) {
            write64(require(call.arguments[0].AsInt()), static_cast<std::uint64_t>(call.arguments[1].AsLong()));
            return dx::VmValue::Void();
        },
        native_flags);
    builder.StaticMethod(
        "nativeReadLong", "(I)J",
        [require, read64](dx::IntrinsicContext& call) {
            return dx::VmValue::Long(static_cast<std::int64_t>(read64(require(call.arguments[0].AsInt()))));
        },
        native_flags);
    builder.StaticMethod(
        "nativeWriteFloat", "(IF)V",
        [require, write32](dx::IntrinsicContext& call) {
            write32(require(call.arguments[0].AsInt()), std::bit_cast<std::uint32_t>(call.arguments[1].AsFloat()));
            return dx::VmValue::Void();
        },
        native_flags);
    builder.StaticMethod(
        "nativeReadFloat", "(I)F",
        [require, read32](dx::IntrinsicContext& call) {
            return dx::VmValue::Float(std::bit_cast<float>(read32(require(call.arguments[0].AsInt()))));
        },
        native_flags);
    builder.StaticMethod(
        "nativeWriteDouble", "(ID)V",
        [require, write64](dx::IntrinsicContext& call) {
            write64(require(call.arguments[0].AsInt()), std::bit_cast<std::uint64_t>(call.arguments[1].AsDouble()));
            return dx::VmValue::Void();
        },
        native_flags);
    builder.StaticMethod(
        "nativeReadDouble", "(I)D",
        [require, read64](dx::IntrinsicContext& call) {
            return dx::VmValue::Double(std::bit_cast<double>(read64(require(call.arguments[0].AsInt()))));
        },
        native_flags);
    builder.StaticMethod(
        "nativeWriteString", "(ILjava/lang/String;)V",
        [require, write32, write](dx::IntrinsicContext& call) {
            auto& p = require(call.arguments[0].AsInt());
            const auto ref = call.arguments[1].ref;
            if (!ref.IsValid()) {
                write32(p, 0xffffffffU);
                return dx::VmValue::Void();
            }
            const auto value = call.vm.Model().StringValue(ref);
            write32(p, static_cast<std::uint32_t>(value.size()));
            for (const auto unit : value) {
                const std::array bytes{std::byte(unit), std::byte(unit >> 8U)};
                write(p, bytes);
            }
            const std::array zero{std::byte{0}, std::byte{0}};
            write(p, zero);
            while ((p.position & 3U) != 0U) {
                const std::array pad{std::byte{0}};
                write(p, pad);
            }
            return dx::VmValue::Void();
        },
        native_flags);
    builder.StaticMethod(
        "nativeReadString", "(I)Ljava/lang/String;",
        [require, read32, read](dx::IntrinsicContext& call) {
            auto& p = require(call.arguments[0].AsInt());
            const auto length = read32(p);
            if (length == 0xffffffffU) return dx::VmValue::Ref(dx::VmObjectRef{});
            if (length > (kMaxParcelBytes / 2U))
                throw dx::VmJavaThrow{"Ljava/lang/IndexOutOfBoundsException;", "string length"};
            std::u16string value;
            value.reserve(length);
            for (std::uint32_t i = 0; i < length; ++i) {
                const auto b = read(p, 2U);
                value.push_back(char16_t(std::uint16_t(b[0]) | (std::uint16_t(b[1]) << 8U)));
            }
            static_cast<void>(read(p, 2U));
            while ((p.position & 3U) != 0U)
                static_cast<void>(read(p, 1U));
            return dx::VmValue::Ref(call.vm.Model().NewString(value));
        },
        native_flags);
    builder.StaticMethod(
        "nativeWriteByteArray", "(I[BII)V",
        [require, write32, write](dx::IntrinsicContext& call) {
            auto& p = require(call.arguments[0].AsInt());
            const auto array = call.arguments[1].ref;
            const auto offset = call.arguments[2].AsInt(), length = call.arguments[3].AsInt();
            if (!array.IsValid()) {
                write32(p, 0xffffffffU);
                return dx::VmValue::Void();
            }
            const auto total = call.vm.Model().ArrayLength(array);
            if (offset < 0 || length < 0 || offset > total || length > total - offset)
                throw dx::VmJavaThrow{"Ljava/lang/ArrayIndexOutOfBoundsException;", "byte array range"};
            write32(p, static_cast<std::uint32_t>(length));
            const auto bytes = call.vm.Model().ReadByteRegion(array, offset, length);
            write(p, bytes);
            while ((p.position & 3U) != 0U) {
                const std::array pad{std::byte{0}};
                write(p, pad);
            }
            return dx::VmValue::Void();
        },
        native_flags);
    builder.StaticMethod(
        "nativeCreateByteArray", "(I)[B",
        [require, read32, read](dx::IntrinsicContext& call) {
            auto& p = require(call.arguments[0].AsInt());
            const auto length = read32(p);
            if (length == 0xffffffffU) return dx::VmValue::Ref(dx::VmObjectRef{});
            if (length > kMaxParcelBytes)
                throw dx::VmJavaThrow{"Ljava/lang/IndexOutOfBoundsException;", "array length"};
            const auto bytes = read(p, length);
            const auto result = call.vm.Model().NewPrimitiveArray(call.vm.Linker().ResolveDescriptor("[B"),
                                                                  JniPrimitiveKind::byte, static_cast<JniSize>(length));
            call.vm.Model().WriteByteRegion(result, 0, bytes);
            while ((p.position & 3U) != 0U)
                static_cast<void>(read(p, 1U));
            return dx::VmValue::Ref(result);
        },
        native_flags);
    builder.StaticMethod(
        "nativeWriteStrongBinder", "(ILandroid/os/IBinder;)V",
        [context, require, write](dx::IntrinsicContext& call) {
            if (context->managed_window_tokens.contains(call.arguments[1].ref.Value())) {
                if (auto* ledger = call.vm.Ledger()) ledger->RecordUnimplemented("dexvm.managed_window_token_ipc", 0);
                throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;",
                                      "managed window token cannot be marshalled"};
            }
            auto& p = require(call.arguments[0].AsInt());
            const auto offset = p.position;
            std::array<std::byte, 16> flat{};
            write(p, flat);
            if (call.arguments[1].ref.IsValid()) p.binders.push_back({offset, flat.size(), call.arguments[1].ref});
            return dx::VmValue::Void();
        },
        native_flags);
    builder.StaticMethod(
        "nativeReadStrongBinder", "(I)Landroid/os/IBinder;",
        [require, read](dx::IntrinsicContext& call) {
            auto& p = require(call.arguments[0].AsInt());
            const auto offset = p.position;
            static_cast<void>(read(p, 16U));
            const auto it = std::find_if(p.binders.begin(), p.binders.end(),
                                         [offset](const auto& r) { return r.offset == offset; });
            return dx::VmValue::Ref(it == p.binders.end() ? dx::VmObjectRef{} : it->binder);
        },
        native_flags);
    builder.StaticMethod(
        "nativeHasFileDescriptors", "(I)Z", [](dx::IntrinsicContext&) { return dx::VmValue::Int(0); }, native_flags);
    const auto fd_unsupported = [](dx::IntrinsicContext& call) -> dx::VmValue {
        if (auto* ledger = call.vm.Ledger()) ledger->RecordUnimplemented("dexvm.parcel.file_descriptors", 0);
        throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;", "Parcel file descriptors are unsupported"};
    };
    builder.StaticMethod("nativeWriteFileDescriptor", "(ILjava/io/FileDescriptor;)V", fd_unsupported, native_flags);
    builder.StaticMethod("nativeReadFileDescriptor", "(I)Ljava/io/FileDescriptor;", fd_unsupported, native_flags);
    constexpr auto package_native = dx::kAccStatic | dx::kAccNative;
    builder.StaticMethod(
        "openFileDescriptor", "(Ljava/lang/String;I)Ljava/io/FileDescriptor;",
        [](dx::IntrinsicContext& call) {
            const auto path_ref = dx::IntrinsicCall(call).NonNullRef(0, "path");
            const auto path = call.vm.StringUtf8(path_ref);
            const auto mode = call.arguments[1].AsInt();
            const auto access = mode & 0x30000000;
            try {
                const auto file =
                    call.vm.IO().OpenFile(path, access != 0x20000000, access == 0x20000000 || access == 0x30000000,
                                          (mode & 0x02000000) != 0, (mode & 0x04000000) != 0, (mode & 0x08000000) != 0);
                const auto fd = call.vm.NewIntrinsicInstance("Ljava/io/FileDescriptor;");
                const auto root = call.vm.ProtectReferences(std::array{fd});
                call.vm.IO().SetDescriptor(fd, {dx::IoRuntime::DescriptorKind::vfs_path, path, 0, false, {}, {}, file});
                call.vm.IO().BindFileStream(fd, file, false);
                static_cast<void>(CallAndroidMethod(call.vm, fd, "setInt$", "(I)V", {dx::VmValue::Int(file->handle)}));
                return dx::VmValue::Ref(fd);
            } catch (const dx::IoRuntimeError& error) {
                throw dx::VmJavaThrow{"Ljava/io/FileNotFoundException;", error.what()};
            }
        },
        package_native);
    builder.StaticMethod(
        "dupFileDescriptor", "(Ljava/io/FileDescriptor;)Ljava/io/FileDescriptor;",
        [](dx::IntrinsicContext& call) {
            const auto original = dx::IntrinsicCall(call).NonNullRef(0, "fd");
            const auto handle = CallAndroidMethod(call.vm, original, "getInt$", "()I").AsInt();
            if (handle < 0) throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "bad FileDescriptor"};
            try {
                static_cast<void>(call.vm.IO().ResolveDescriptor(original, handle));
                const auto fd = call.vm.NewIntrinsicInstance("Ljava/io/FileDescriptor;");
                const auto root = call.vm.ProtectReferences(std::array{fd});
                call.vm.IO().DuplicateDescriptor(original, fd);
                static_cast<void>(CallAndroidMethod(call.vm, fd, "setInt$", "(I)V",
                                                    {dx::VmValue::Int(call.vm.IO().Descriptor(fd).file->handle)}));
                return dx::VmValue::Ref(fd);
            } catch (const dx::IoRuntimeError& error) {
                throw dx::VmJavaThrow{"Ljava/io/IOException;", error.what()};
            }
        },
        package_native);
    builder.StaticMethod(
        "closeFileDescriptor", "(Ljava/io/FileDescriptor;)V",
        [](dx::IntrinsicContext& call) {
            const auto fd = dx::IntrinsicCall(call).NonNullRef(0, "fd");
            const auto handle = CallAndroidMethod(call.vm, fd, "getInt$", "()I").AsInt();
            if (handle >= 0) {
                static_cast<void>(CallAndroidMethod(call.vm, fd, "setInt$", "(I)V", {dx::VmValue::Int(-1)}));
                // AOSP Parcel ignores close(2) errors, unlike Posix.close.
                try {
                    static_cast<void>(call.vm.IO().ResolveDescriptor(fd, handle));
                    call.vm.IO().CloseDescriptor(fd);
                } catch (const dx::IoRuntimeError&) {
                }
            }
            return dx::VmValue::Void();
        },
        package_native);
    builder.StaticMethod(
        "clearFileDescriptor", "(Ljava/io/FileDescriptor;)V",
        [](dx::IntrinsicContext& call) {
            const auto fd = dx::IntrinsicCall(call).NonNullRef(0, "fd");
            call.vm.IO().ForgetDescriptor(fd);
            static_cast<void>(CallAndroidMethod(call.vm, fd, "setInt$", "(I)V", {dx::VmValue::Int(-1)}));
            return dx::VmValue::Void();
        },
        package_native);
    builder.StaticMethod(
        "nativeMarshall", "(I)[B",
        [require](dx::IntrinsicContext& call) {
            const auto& p = require(call.arguments[0].AsInt());
            if (!p.binders.empty())
                throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;", "cannot marshall Binder objects"};
            const auto result = call.vm.Model().NewPrimitiveArray(
                call.vm.Linker().ResolveDescriptor("[B"), JniPrimitiveKind::byte, static_cast<JniSize>(p.bytes.size()));
            call.vm.Model().WriteByteRegion(result, 0, p.bytes);
            return dx::VmValue::Ref(result);
        },
        native_flags);
    builder.StaticMethod(
        "nativeUnmarshall", "(I[BII)V",
        [require, resize](dx::IntrinsicContext& call) {
            auto& p = require(call.arguments[0].AsInt());
            const auto array = call.arguments[1].ref;
            const auto offset = call.arguments[2].AsInt(), length = call.arguments[3].AsInt();
            if (!array.IsValid()) throw dx::VmJavaThrow{"Ljava/lang/NullPointerException;", "data"};
            const auto total = call.vm.Model().ArrayLength(array);
            if (offset < 0 || length < 0 || offset > total || length > total - offset)
                throw dx::VmJavaThrow{"Ljava/lang/ArrayIndexOutOfBoundsException;", "unmarshall range"};
            resize(p, static_cast<std::size_t>(length));
            const auto bytes = call.vm.Model().ReadByteRegion(array, offset, length);
            std::copy(bytes.begin(), bytes.end(), p.bytes.begin());
            p.position = 0;
            p.binders.clear();
            return dx::VmValue::Void();
        },
        native_flags);
    builder.StaticMethod(
        "nativeAppendFrom", "(IIII)V",
        [require, write](dx::IntrinsicContext& call) {
            auto& target = require(call.arguments[0].AsInt());
            auto& source = require(call.arguments[1].AsInt());
            const auto offset = call.arguments[2].AsInt(), length = call.arguments[3].AsInt();
            if (offset < 0 || length < 0 || static_cast<std::size_t>(offset) > source.bytes.size() ||
                static_cast<std::size_t>(length) > source.bytes.size() - static_cast<std::size_t>(offset))
                throw dx::VmJavaThrow{"Ljava/lang/IndexOutOfBoundsException;", "append range"};
            const auto source_start = static_cast<std::size_t>(offset),
                       source_end = source_start + static_cast<std::size_t>(length);
            for (const auto& record : source.binders) {
                const bool overlaps = record.offset < source_end && source_start < record.offset + record.span;
                const bool contained = record.offset >= source_start && record.offset + record.span <= source_end;
                if (overlaps && !contained)
                    throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;", "partial Binder record append"};
            }
            const auto source_records = source.binders;
            const auto target_start = target.position;
            const std::vector copy(source.bytes.begin() + offset, source.bytes.begin() + offset + length);
            write(target, copy);
            for (const auto& record : source_records)
                if (record.offset >= source_start && record.offset + record.span <= source_end)
                    target.binders.push_back({target_start + record.offset - source_start, record.span, record.binder});
            return dx::VmValue::Void();
        },
        native_flags);
    builder.StaticMethod(
        "nativeWriteInterfaceToken", "(ILjava/lang/String;)V",
        [context, require, write32, write](dx::IntrinsicContext& call) {
            auto& p = require(call.arguments[0].AsInt());
            // API19 STRICT_MODE_PENALTY_GATHER. Recording a mask does not run detectors.
            write32(p, static_cast<std::uint32_t>(
                           context->binder_threads[call.vm.CurrentContextToken()].strict_mode_policy) |
                           0x100U);
            const auto value = call.vm.Model().StringValue(call.arguments[1].ref);
            write32(p, static_cast<std::uint32_t>(value.size()));
            for (const auto unit : value) {
                const std::array bytes{std::byte(unit), std::byte(unit >> 8U)};
                write(p, bytes);
            }
            const std::array zero{std::byte{0}, std::byte{0}};
            write(p, zero);
            while ((p.position & 3U) != 0U) {
                const std::array pad{std::byte{0}};
                write(p, pad);
            }
            return dx::VmValue::Void();
        },
        native_flags);
    builder.StaticMethod(
        "nativeEnforceInterface", "(ILjava/lang/String;)V",
        [context, require, read32, read](dx::IntrinsicContext& call) {
            auto& p = require(call.arguments[0].AsInt());
            const auto policy = read32(p);
            const auto length = read32(p);
            const auto remaining = p.bytes.size() - p.position;
            if (length > kMaxParcelBytes / 2U || (static_cast<std::size_t>(length) + 1U) * 2U > remaining)
                throw dx::VmJavaThrow{"Ljava/lang/SecurityException;", "invalid Binder interface length"};
            std::u16string actual;
            actual.reserve(length);
            for (std::uint32_t i = 0; i < length; ++i) {
                const auto b = read(p, 2U);
                actual.push_back(char16_t(std::uint16_t(b[0]) | (std::uint16_t(b[1]) << 8U)));
            }
            static_cast<void>(read(p, 2U));
            while ((p.position & 3U) != 0U)
                static_cast<void>(read(p, 1U));
            if (actual != call.vm.Model().StringValue(call.arguments[1].ref))
                throw dx::VmJavaThrow{"Ljava/lang/SecurityException;", "Binder interface mismatch"};
            context->binder_threads[call.vm.CurrentContextToken()].strict_mode_policy =
                static_cast<std::int32_t>(policy);
            return dx::VmValue::Void();
        },
        native_flags);
    return std::move(builder).Build();
}

Decl Declare_local_PowerService(const Context&) {
    auto builder =
        dx::IntrinsicClassBuilder::Class("Lorg/ogplay/framework/LocalPowerService;", "Landroid/os/IPowerManager$Stub;");
    builder.VirtualMethod("isScreenOn", "()Z", [](dx::IntrinsicContext&) { return dx::VmValue::Int(1); });
    builder.VirtualMethod(
        "acquireWakeLock", "(Landroid/os/IBinder;ILjava/lang/String;Ljava/lang/String;Landroid/os/WorkSource;)V",
        [](dx::IntrinsicContext& call) {
            const auto level = call.arguments[1].AsInt() & 0xffff;
            if ((level != 1 && level != 6 && level != 10 && level != 26) || call.arguments[4].ref.IsValid()) {
                if (auto* ledger = call.vm.Ledger()) ledger->RecordUnimplemented("dexvm.power.acquire_wake_lock", 0);
                throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;",
                                      "unsupported wake lock level or work source"};
            }
            // WakeLock owns held state, reference count and timeout in ROM. The
            // process-local service does not need a host power resource.
            return dx::VmValue::Void();
        });
    builder.VirtualMethod("releaseWakeLock", "(Landroid/os/IBinder;I)V",
                          [](dx::IntrinsicContext&) { return dx::VmValue::Void(); });
    return std::move(builder).Build();
}

Decl Declare_local_VibratorService(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Lorg/ogplay/framework/LocalVibratorService;",
                                                    "Landroid/os/IVibratorService$Stub;");
    builder.VirtualMethod("hasVibrator", "()Z", [](dx::IntrinsicContext&) { return dx::VmValue::Int(0); });
    builder.VirtualMethod("vibrate", "(ILjava/lang/String;JLandroid/os/IBinder;)V",
                          [context](dx::IntrinsicContext& call) {
                              const auto milliseconds = call.arguments[2].AsLong();
                              if (milliseconds < 0)
                                  throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "milliseconds"};
                              const auto uid = call.arguments[0].AsInt();
                              if (uid != static_cast<std::int32_t>(context->application_uid))
                                  throw dx::VmJavaThrow{"Ljava/lang/SecurityException;", "foreign vibration owner"};
                              context->last_vibration_millis = milliseconds;
                              return dx::VmValue::Void();
                          });
    RejectUnsupportedServiceMethod(builder, "android.os.IVibratorService", "vibratePattern",
                                   "(ILjava/lang/String;[JILandroid/os/IBinder;)V");
    builder.VirtualMethod("cancelVibrate", "(Landroid/os/IBinder;)V", [context](dx::IntrinsicContext&) {
        context->last_vibration_millis = 0;
        return dx::VmValue::Void();
    });
    return std::move(builder).Build();
}

Decl Declare_ddm_DdmServer() {
    auto builder = dx::IntrinsicClassBuilder::Class("Lorg/apache/harmony/dalvik/ddmc/DdmServer;");
    builder.MergesBootClass();
    // Dalvik has no connected DDMS transport. The Java process name is still retained by DdmHandleAppName.
    builder.StaticMethod(
        "nativeSendChunk", "(I[BII)V", [](dx::IntrinsicContext&) { return dx::VmValue::Void(); },
        dx::kAccStatic | dx::kAccPrivate | dx::kAccNative);
    return std::move(builder).Build();
}

Decl Declare_android_os_Process(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/os/Process;");
    builder.MergesBootClass();
    constexpr auto flags = dx::kAccPublic | dx::kAccStatic | dx::kAccFinal | dx::kAccNative;
    const auto priority = [](dx::IntrinsicContext& call) {
        const auto value = call.arguments.back().AsInt();
        if (value < -20 || value > 19) throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "priority"};
        return dx::VmValue::Void();
    };
    builder.StaticMethod(
        "setArgV0", "(Ljava/lang/String;)V",
        [context](dx::IntrinsicContext& call) {
            context->guest_process_name = call.vm.StringUtf8(dx::IntrinsicCall(call).NonNullRef(0, "name"));
            return dx::VmValue::Void();
        },
        flags);
    builder.StaticMethod("setThreadPriority", "(I)V", priority, flags);
    builder.StaticMethod("setThreadPriority", "(II)V", priority, flags);
    const auto signal = [context](dx::IntrinsicContext& call) -> dx::VmValue {
        if (call.arguments[0].AsInt() != kGuestProcessId || call.arguments[1].AsInt() != 9) {
            if (auto* ledger = call.vm.Ledger()) ledger->RecordUnimplemented("android.os.Process.sendSignal(II)V", 0);
            throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;", "foreign process or unsupported signal"};
        }
        context->exit_requested = true;
        call.vm.Exit(0);
    };
    builder.StaticMethod("sendSignal", "(II)V", signal, flags);
    builder.StaticMethod("sendSignalQuiet", "(II)V", signal, flags);
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

namespace ogplay::runtime {

void RegisterAndroidValueStateTables(dexvm::Interpreter& vm, const std::shared_ptr<DexVmAndroidContext>& context) {
    if (context == nullptr) return;
    android_intrinsics::RegisterGraphicsStateTables(vm, context);
    android_intrinsics::RegisterSkiaStateTables(vm, context);
    const auto configuration = vm.Linker().ResolveDescriptor("Landroid/view/ViewConfiguration;");
    const auto constructor = vm.Linker().FindDirectMethod(configuration, "<init>", "(Landroid/content/Context;)V");
    if (!constructor) throw std::runtime_error("ROM ViewConfiguration constructor unavailable");
    vm.Linker().MutableMethod(*constructor).constructor_inputs = [](dexvm::IntrinsicContext& call) {
        using namespace android_intrinsics;
        const auto resources =
            CallAndroidMethod(call.vm, call.arguments[0].ref, "getResources", "()Landroid/content/res/Resources;").ref;
        const auto mode =
            CallAndroidMethod(call.vm, resources, "getInteger", "(I)I", {dexvm::VmValue::Int(0x010e0049)}).AsInt();
        bool menu = mode == 1;
        if (mode != 1 && mode != 2) {
            bool navigation =
                CallAndroidMethod(call.vm, resources, "getBoolean", "(I)Z", {dexvm::VmValue::Int(0x01110044)})
                    .AsInt() != 0;
            const auto key = call.vm.NewStringUtf8("qemu.hw.mainkeys");
            const auto roots = call.vm.ProtectReferences(std::array{key, resources, call.receiver});
            const auto owner = call.vm.Linker().ResolveDescriptor("Landroid/os/SystemProperties;");
            const auto get = call.vm.Linker().FindDirectMethod(owner, "get", "(Ljava/lang/String;)Ljava/lang/String;");
            if (!get) throw std::runtime_error("SystemProperties.get unavailable");
            const auto value = call.vm.Call(*get, std::array{dexvm::VmValue::Ref(key)});
            if (value.exception.IsValid())
                throw dexvm::VmJavaThrow{call.vm.Linker().Class(value.exception_class).descriptor,
                                         value.exception_message, value.exception};
            const auto text = call.vm.StringUtf8(value.value.ref);
            if (text == "1") navigation = false;
            if (text == "0") navigation = true;
            menu = !navigation;
        }
        const auto type = call.vm.Model().ObjectClass(call.receiver);
        const auto menu_field = call.vm.Linker().FindFieldRecursive(type, "sHasPermanentMenuKey", "Z");
        const auto set_field = call.vm.Linker().FindFieldRecursive(type, "sHasPermanentMenuKeySet", "Z");
        if (!menu_field || !set_field) throw std::runtime_error("ViewConfiguration menu fields unavailable");
        for (const auto [field, value] : {std::pair{*menu_field, menu}, {*set_field, true}}) {
            call.vm.RequireImplementedField(field);
            const auto& info = call.vm.Linker().Field(field);
            if (info.is_static) throw std::runtime_error("ViewConfiguration input must be an instance field");
            call.vm.Model().InstanceSlots(call.receiver)[info.slot] = {value ? 1U : 0U, dexvm::SlotTag::cat1};
        }
        return dexvm::VmValue::Void();
    };
    {
        if (vm.CurrentContextToken() != 1U ||
            (context->ui_java_owner_thread && context->ui_java_owner_thread != std::this_thread::get_id()))
            throw dexvm::VmJavaThrow{"Ljava/lang/IllegalStateException;", "View owner binding requires main context"};
        context->ui_java_owner_thread = std::this_thread::get_id();
        vm.RegisterIntrinsicStateTable({"android.java-view-binding",
                                        [](dexvm::VmObjectRef, const dexvm::VmRootVisitor&) {},
                                        [context](dexvm::VmObjectRef object) {
                                            context->scrollbar_fade_deadlines.erase(object.Value());
                                            context->ui_window_schedules.erase(object);
                                            context->ui_traversal_diagnostics.erase(object);
                                            for (auto& [_, state] : context->ui_window_schedules)
                                                state.requests.erase(object);
                                            const auto found = context->object_to_ui_node.find(object.Value());
                                            if (found == context->object_to_ui_node.end()) return;
                                            const auto node = found->second;
                                            context->ui_node_to_object.erase(node);
                                            context->object_to_ui_node.erase(found);
                                            context->ui_tree.RetireProjectedIdentity(node);
                                        },
                                        {}});
    }
    vm.RegisterIntrinsicStateTable({"android.value",
                                    [context](const dexvm::VmObjectRef owner, const dexvm::VmRootVisitor& visit) {
                                        if (const auto pending = context->pending_intents.find(owner.Value());
                                            pending != context->pending_intents.end())
                                            visit(pending->second.intent);
                                    },
                                    [context](const dexvm::VmObjectRef owner) {
                                        context->pending_intents.erase(owner.Value());
                                    },
                                    {}});
    // The original Parcel.init owns the token in Java. Read that field for GC
    // instead of keeping a second owner-to-token table in the host.
    const auto parcel_type = vm.Linker().FindClass("Landroid/os/Parcel;");
    if (parcel_type) {
        vm.Linker().EnsureClassLinked(*parcel_type);
        const auto ptr = vm.Linker().FindFieldRecursive(*parcel_type, "mNativePtr", "I");
        const auto owns = vm.Linker().FindFieldRecursive(*parcel_type, "mOwnsNativeParcelObject", "Z");
        if (!ptr || !owns) throw std::logic_error("ROM Parcel ownership fields are unavailable");
        const auto parcel_token = [&model = vm.Model(), type = *parcel_type, ptr_slot = vm.Linker().Field(*ptr).slot,
                                   owns_slot = vm.Linker().Field(*owns).slot](dexvm::VmObjectRef ref) {
            if (model.ObjectClass(ref) != type || model.Kind(ref) != dexvm::VmObjectKind::vm_instance) return 0U;
            const auto slots = model.InstanceSlots(ref);
            return slots[owns_slot].bits != 0 ? slots[ptr_slot].bits : 0U;
        };
        vm.RegisterIntrinsicStateTable(
            {"android.parcel",
             [context, parcel_token](dexvm::VmObjectRef ref, const dexvm::VmRootVisitor& visit) {
                 if (context->parcel_backings.empty()) return;
                 const auto found = context->parcel_backings.find(parcel_token(ref));
                 if (found != context->parcel_backings.end())
                     for (const auto& record : found->second.binders)
                         if (record.binder.IsValid()) visit(record.binder);
             },
             [context, parcel_token](dexvm::VmObjectRef ref) {
                 if (!context->parcel_backings.empty()) context->parcel_backings.erase(parcel_token(ref));
             },
             {}});
    }
}

}  // namespace ogplay::runtime

namespace ogplay::runtime::android_intrinsics {
Decl Declare_android_os_MessageQueue(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/os/MessageQueue;");
    builder.MergesBootClass();
    constexpr auto flags = dx::kAccPrivate | dx::kAccStatic | dx::kAccNative;
    builder.StaticMethod(
        "nativeInit", "()I",
        [context](dx::IntrinsicContext& call) {
            const auto token = context->next_message_queue_token++;
            if (token == 0 || token > 0x7fffffffU)
                throw dx::VmJavaThrow{"Ljava/lang/OutOfMemoryError;", "MessageQueue tokens exhausted"};
            if (context->prepare_native_looper) context->prepare_native_looper(call.vm.CurrentContextToken());
            std::scoped_lock lock(context->scheduler_mutex);
            context->message_queues.emplace(token,
                                            std::make_shared<DexVmAndroidContext::MessageQueueState>(
                                                DexVmAndroidContext::MessageQueueState{call.vm.CurrentContextToken()}));
            return dx::VmValue::Int(static_cast<std::int32_t>(token));
        },
        flags);
    const auto require = [context](std::int32_t token) -> std::shared_ptr<DexVmAndroidContext::MessageQueueState> {
        const auto found = context->message_queues.find(static_cast<std::uint32_t>(token));
        if (found == context->message_queues.end())
            throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;", "stale MessageQueue token"};
        return found->second;
    };
    builder.StaticMethod(
        "nativeDestroy", "(I)V",
        [context, require](dx::IntrinsicContext& call) {
            {
                std::scoped_lock lock(context->scheduler_mutex);
                const auto queue = require(call.arguments[0].AsInt());
                queue->destroyed = true;
                queue->wake = true;
                context->message_queues.erase(static_cast<std::uint32_t>(call.arguments[0].AsInt()));
            }
            NotifyAndroidScheduler(*context);
            return dx::VmValue::Void();
        },
        flags);
    builder.StaticMethod(
        "nativeWake", "(I)V",
        [context, require](dx::IntrinsicContext& call) {
            std::uint64_t thread{};
            {
                std::scoped_lock lock(context->scheduler_mutex);
                const auto queue = require(call.arguments[0].AsInt());
                queue->wake = true;
                thread = queue->context_token;
            }
            if (context->wake_native_looper) context->wake_native_looper(thread);
            context->scheduler_changed.notify_all();
            return dx::VmValue::Void();
        },
        flags);
    builder.StaticMethod(
        "nativeIsIdling", "(I)Z",
        [context, require](dx::IntrinsicContext& call) {
            std::scoped_lock lock(context->scheduler_mutex);
            return dx::VmValue::Int(require(call.arguments[0].AsInt())->idling ? 1 : 0);
        },
        flags);
    builder.StaticMethod(
        "nativePollOnce", "(II)V",
        [context, require](dx::IntrinsicContext& call) {
            call.vm.RestartExecutionBudget();
            const auto looper = RegisterCurrentRomLooper(call.vm, context);
            PumpNativeLooperEvents(call.vm, *context, looper);
            if (call.vm.CurrentContextToken() == 1U && context->main_queue_poll) context->main_queue_poll(call.vm);
            const auto timeout = call.arguments[1].AsInt();
            const auto now = context->uptime_millis.load();
            const auto duration = std::max(timeout, 0);
            const auto deadline = now > std::numeric_limits<std::int64_t>::max() - duration
                                      ? std::numeric_limits<std::int64_t>::max()
                                      : now + duration;
            const auto token = call.arguments[0].AsInt();
            std::shared_ptr<DexVmAndroidContext::MessageQueueState> queue;
            {
                std::scoped_lock lock(context->scheduler_mutex);
                queue = require(token);
                if (queue->context_token != call.vm.CurrentContextToken())
                    throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;", "MessageQueue polled on another thread"};
            }
            auto& execution = call.vm.ExecutionLock();
            const auto depth = execution.ReleaseForBlocking();
            const auto ready = [&] {
                return queue->wake || queue->destroyed || context->scheduler_shutdown ||
                       std::ranges::any_of(context->scheduled_work,
                                           [&](const auto& work) {
                                               return work.looper == looper &&
                                                      work.deadline_millis <= context->uptime_millis.load();
                                           }) ||
                       (timeout >= 0 && context->uptime_millis.load() >= deadline);
            };
            {
                std::scoped_lock lock(context->scheduler_mutex);
                queue->idling = timeout != 0;
            }
            if (context->poll_native_looper) {
                context->poll_native_looper(call.vm.CurrentContextToken(), [&] {
                    std::scoped_lock lock(context->scheduler_mutex);
                    return ready();
                });
            } else {
                std::unique_lock lock(context->scheduler_mutex);
                context->scheduler_changed.wait(lock, ready);
            }
            bool stopped{};
            {
                std::scoped_lock lock(context->scheduler_mutex);
                queue->idling = false;
                queue->wake = false;
                stopped = context->scheduler_shutdown;
            }
            execution.ReacquireAfterBlocking(depth);
            if (stopped) throw dx::DexVmError(dx::DexVmErrorReason::thread_stopped, "MessageQueue shutdown");
            PumpNativeLooperEvents(call.vm, *context, looper);
            if (call.vm.CurrentContextToken() == 1U && context->main_queue_poll) context->main_queue_poll(call.vm);
            return dx::VmValue::Void();
        },
        flags);
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

namespace ogplay::runtime::android_intrinsics {
Decl Declare_android_os_Trace(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/os/Trace;");
    builder.MergesBootClass();
    constexpr auto flags = dx::kAccPrivate | dx::kAccStatic | dx::kAccNative;
    builder.StaticMethod(
        "nativeGetEnabledTags", "()J",
        [context](dx::IntrinsicContext&) { return dx::VmValue::Long(context->trace_enabled ? 8 : 0); }, flags);
    builder.StaticMethod(
        "nativeSetAppTracingAllowed", "(Z)V", [](dx::IntrinsicContext&) { return dx::VmValue::Void(); }, flags);
    builder.StaticMethod(
        "nativeSetTracingEnabled", "(Z)V",
        [context](dx::IntrinsicContext& call) {
            context->trace_enabled = call.arguments[0].AsInt() != 0;
            return dx::VmValue::Void();
        },
        flags);
    const auto trace = [](const char* operation) {
        return [operation](dx::IntrinsicContext& call) {
            if (call.arguments[0].AsLong() != 8) {
                if (auto* ledger = call.vm.Ledger()) ledger->RecordUnimplemented("dexvm.trace.tag", 0);
                throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;", "unsupported native trace tag"};
            }
            std::string line = std::string("android.view.trace.") + operation;
            if (call.arguments.size() > 1) line += " " + call.vm.StringUtf8(call.arguments[1].ref);
            if (call.arguments.size() > 2) line += " value=" + std::to_string(call.arguments[2].AsInt());
            GuestLog(call, core::LogLevel::debug, line);
            return dx::VmValue::Void();
        };
    };
    builder.StaticMethod("nativeTraceBegin", "(JLjava/lang/String;)V", trace("begin"), flags);
    builder.StaticMethod("nativeTraceEnd", "(J)V", trace("end"), flags);
    builder.StaticMethod("nativeAsyncTraceBegin", "(JLjava/lang/String;I)V", trace("async.begin"), flags);
    builder.StaticMethod("nativeAsyncTraceEnd", "(JLjava/lang/String;I)V", trace("async.end"), flags);
    builder.StaticMethod("nativeTraceCounter", "(JLjava/lang/String;I)V", trace("counter"), flags);
    return std::move(builder).Build();
}
}  // namespace ogplay::runtime::android_intrinsics
