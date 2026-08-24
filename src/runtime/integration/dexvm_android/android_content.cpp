// DVM-80: API-family translation unit. Physical consolidation only.

// ---- migrated from android_content_BroadcastReceiver.cpp ----
// BroadcastReceiver is inert: the session never dispatches broadcasts, so
// the base onReceive stays a recorded no-op games may override.

#include "catalog.h"

namespace ogplay::runtime::android_intrinsics::dvm80_android_content_BroadcastReceiver {

Decl Declare_android_content_BroadcastReceiver(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/content/BroadcastReceiver;", "Ljava/lang/Object;");
    builder.Constructor("()V",
        [](dx::IntrinsicContext&) { return dx::VmValue::Void(); });
    builder.VirtualMethod("onReceive",
        "(Landroid/content/Context;Landroid/content/Intent;)V",
        [](dx::IntrinsicContext&) { return dx::VmValue::Void(); });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

namespace ogplay::runtime::android_intrinsics::
    dvm80_android_content_pm_PackageManager {
Decl Declare_android_content_pm_PackageManager_NameNotFoundException(
    const Context& context);
}

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_content_pm_PackageManager_NameNotFoundException(
    const Context& context) {
    return dvm80_android_content_pm_PackageManager::
        Declare_android_content_pm_PackageManager_NameNotFoundException(
            context);
}

}  // namespace ogplay::runtime::android_intrinsics

namespace ogplay::runtime::android_intrinsics {
Decl Declare_android_content_BroadcastReceiver(const Context& context) {
    return dvm80_android_content_BroadcastReceiver::Declare_android_content_BroadcastReceiver(context);
}
}  // namespace ogplay::runtime::android_intrinsics

// ---- migrated from android_content_ContentResolver.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics::dvm80_android_content_ContentResolver {

Decl Declare_android_content_ContentResolver(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/content/ContentResolver;", "Ljava/lang/Object;");
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

namespace ogplay::runtime::android_intrinsics {
Decl Declare_android_content_ContentResolver(const Context& context) {
    return dvm80_android_content_ContentResolver::Declare_android_content_ContentResolver(context);
}
}  // namespace ogplay::runtime::android_intrinsics

// ---- migrated from android_content_Context.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics::dvm80_android_content_Context {

namespace {

[[nodiscard]] bool EnsureDirectory(const Context& context,
                                   const std::string& path) {
    if (context->vfs == nullptr) return false;
    for (std::size_t cursor = 1; cursor <= path.size(); ++cursor) {
        if (cursor != path.size() && path[cursor] != '/') continue;
        const auto prefix = path.substr(0, cursor);
        try {
            const auto info = context->vfs->Stat(prefix);
            if (!info.is_directory) return false;
            continue;
        } catch (const VfsError&) {
        }
        try {
            context->vfs->CreateDirectory(prefix);
        } catch (const VfsError&) {
            return false;
        }
    }
    return true;
}

// Reads a preferences file once per name. Damaged XML is a real failure.
void LoadPreferencesOnce(const Context& context, const std::string& name) {
    if (context->preferences_loaded[name]) return;
    context->preferences_loaded[name] = true;
    if (context->vfs == nullptr) return;
    try {
        context->preferences[name] =
            LoadPreferences(*context->vfs, PreferencesPathOf(context, name));
    } catch (const PreferencesXmlError& error) {
        throw dx::VmJavaThrow{
            "Ljava/lang/IllegalStateException;",
            std::string("SharedPreferences file is not readable: ") +
                error.what()};
    }
}

}  // namespace

Decl Declare_android_content_Context(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/content/Context;", "Ljava/lang/Object;");
    builder.Constructor("()V", [](dx::IntrinsicContext&) {
        return dx::VmValue::Void();
    });
    builder.FinalMethod("getAssets", "()Landroid/content/res/AssetManager;",
        [context](dx::IntrinsicContext& call) {
            return dx::VmValue::Ref(Singleton(
                call, context, "assets", "Landroid/content/res/AssetManager;"));
        });
    builder.FinalMethod("getPackageName", "()Ljava/lang/String;",
        [context](dx::IntrinsicContext& call) {
            return MakeString(call, context->package_name);
        });
    builder.FinalMethod("getPackageManager",
        "()Landroid/content/pm/PackageManager;",
        [context](dx::IntrinsicContext& call) {
            return dx::VmValue::Ref(Singleton(
                call, context, "package_manager",
                "Landroid/content/pm/PackageManager;"));
        });
    builder.FinalMethod("getApplicationContext", "()Landroid/content/Context;",
        [context](dx::IntrinsicContext& call) {
            // One guest process owns one application Context; Activity
            // wrappers may come and go without changing this identity.
            if (context->application.IsValid()) {
                return dx::VmValue::Ref(context->application);
            }
            return dx::VmValue::Ref(Singleton(
                call, context, "application_context",
                "Landroid/content/Context;"));
        });
    builder.FinalMethod("getFilesDir", "()Ljava/io/File;",
        [context](dx::IntrinsicContext& call) {
            const auto path = "/data/data/" + context->package_name +
                              "/files";
            if (!EnsureDirectory(context, path)) {
                return dx::VmValue::Ref(dx::VmObjectRef{});
            }
            constexpr auto key = "context_files_directory";
            const auto found = context->singletons.find(key);
            if (found != context->singletons.end()) {
                return dx::VmValue::Ref(found->second);
            }
            const auto file = call.vm.NewIntrinsicInstance("Ljava/io/File;");
            const auto slots = call.vm.Model().InstanceSlots(file);
            slots[0] = {call.vm.NewStringUtf8(path).Value(),
                        dx::SlotTag::ref};
            context->singletons.emplace(key, file);
            return dx::VmValue::Ref(file);
        });
    builder.FinalMethod("getResources", "()Landroid/content/res/Resources;",
        [context](dx::IntrinsicContext& call) {
            return dx::VmValue::Ref(Singleton(call, context, "resources",
                "Landroid/content/res/Resources;"));
        });
    builder.FinalMethod("getSystemService",
        "(Ljava/lang/String;)Ljava/lang/Object;",
        [context](dx::IntrinsicContext& call) {
            const auto name = call.vm.StringUtf8(call.arguments[0].ref);
            if (name == "phone") {
                return dx::VmValue::Ref(Singleton(
                    call, context, "phone",
                    "Landroid/telephony/TelephonyManager;"));
            }
            if (name == "audio") {
                return dx::VmValue::Ref(Singleton(
                    call, context, "audio", "Landroid/media/AudioManager;"));
            }
            if (name == "wifi") {
                return dx::VmValue::Ref(Singleton(
                    call, context, "wifi", "Landroid/net/wifi/WifiManager;"));
            }
            if (name == "sensor") {
                return dx::VmValue::Ref(Singleton(
                    call, context, "sensor",
                    "Landroid/hardware/SensorManager;"));
            }
            if (name == "connectivity") {
                return dx::VmValue::Ref(Singleton(
                    call, context, "connectivity",
                    "Landroid/net/ConnectivityManager;"));
            }
            if (name == "input_method") {
                return dx::VmValue::Ref(Singleton(
                    call, context, "input_method",
                    "Landroid/view/inputmethod/InputMethodManager;"));
            }
            if (name == "window") {
                return dx::VmValue::Ref(Singleton(
                    call, context, "window_manager",
                    "Landroid/view/WindowManagerImpl;"));
            }
            throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;",
                                  "system service is not provided: " + name};
        });
    builder.FinalMethod("registerReceiver",
        "(Landroid/content/BroadcastReceiver;Landroid/content/IntentFilter;)Landroid/content/Intent;",
        [context](dx::IntrinsicContext& call) {
            const auto receiver = call.arguments[0].ref;
            if (receiver.IsValid()) {
                context->broadcast_receivers[call.receiver.Value()].insert(
                    receiver.Value());
            }
            // Sticky broadcast lookup: nothing pending on this platform.
            return dx::VmValue::Ref(dx::VmObjectRef{});
        });
    builder.FinalMethod("unregisterReceiver",
        "(Landroid/content/BroadcastReceiver;)V",
        [context](dx::IntrinsicContext& call) {
            const auto receiver = call.arguments[0].ref;
            const auto owner = context->broadcast_receivers.find(
                call.receiver.Value());
            if (!receiver.IsValid() ||
                owner == context->broadcast_receivers.end() ||
                owner->second.erase(receiver.Value()) == 0U) {
                throw dx::VmJavaThrow{
                    "Ljava/lang/IllegalArgumentException;",
                    "Receiver not registered"};
            }
            if (owner->second.empty()) {
                context->broadcast_receivers.erase(owner);
            }
            return dx::VmValue::Void();
        });
    builder.FinalMethod("startActivity", "(Landroid/content/Intent;)V",
        [context](dx::IntrinsicContext& call) -> dx::VmValue {
            // In-process activity switch: only intents with an explicit
            // component that resolves to a dex activity are supported;
            // anything else (external apps, market links, ...) stays an
            // explicit failure.
            const auto intent = call.arguments[0].ref;
            const auto component =
                context->intent_components.find(intent.Value());
            if (component == context->intent_components.end()) {
                throw dx::VmJavaThrow{
                    "Ljava/lang/UnsupportedOperationException;",
                    "startActivity without an in-package component is outside "
                    "the compatibility scope"};
            }
            context->pending_activity_descriptor = component->second;
            context->activity_switch_pending = true;
            context->current_intent = intent;
            return dx::VmValue::Void();
        });
    builder.FinalMethod("getSharedPreferences", "(Ljava/lang/String;I)Landroid/content/SharedPreferences;",
        [context](dx::IntrinsicContext& call) {
            const auto name = call.vm.StringUtf8(call.arguments[0].ref);
            const auto instance = Singleton(
                call, context, "prefs:" + name,
                "Landroid/content/SharedPreferencesImpl;");
            context->preference_names[instance.Value()] = name;
            LoadPreferencesOnce(context, name);
            return dx::VmValue::Ref(instance);
        });
    builder.FinalMethod("getContentResolver", "()Landroid/content/ContentResolver;",
        [context](dx::IntrinsicContext& call) {
            return dx::VmValue::Ref(
                Singleton(call, context, "content_resolver",
                          "Landroid/content/ContentResolver;"));
        });
    builder.FinalMethod("sendBroadcast", "(Landroid/content/Intent;)V",
        [](dx::IntrinsicContext& call) {
            // No other process exists; the broadcast truthfully has no
            // audience. Logged so silent drops stay visible.
            GuestLog(call, core::LogLevel::debug,
                     "sendBroadcast dropped: no receivers on this platform");
            return dx::VmValue::Void();
        });
    builder.FinalMethod("getExternalFilesDir", "(Ljava/lang/String;)Ljava/io/File;",
        [context](dx::IntrinsicContext& call) {
            // Platform layout under the external mount; a null type argument
            // answers the package files root.
            auto path = context->external_storage_root + "/Android/data/" +
                        context->package_name + "/files";
            const auto type = call.arguments[0].ref;
            if (type.IsValid()) {
                path += "/" + call.vm.StringUtf8(type);
            }
            const auto file = call.vm.NewIntrinsicInstance("Ljava/io/File;");
            const auto slots = call.vm.Model().InstanceSlots(file);
            slots[0] = {call.vm.NewStringUtf8(path).Value(), dx::SlotTag::ref};
            return dx::VmValue::Ref(file);
        });
    builder.FinalMethod("startService",
        "(Landroid/content/Intent;)Landroid/content/ComponentName;",
        [](dx::IntrinsicContext& call) {
            GuestLog(call, core::LogLevel::debug,
                     "startService answered null: no services on this "
                     "platform");
            return dx::VmValue::Ref(dx::VmObjectRef{});
        });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

namespace ogplay::runtime::android_intrinsics {
Decl Declare_android_content_Context(const Context& context) {
    return dvm80_android_content_Context::Declare_android_content_Context(context);
}
}  // namespace ogplay::runtime::android_intrinsics

// ---- migrated from android_content_DialogInterface_OnCancelListener.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics::dvm80_android_content_DialogInterface_OnCancelListener {

Decl Declare_android_content_DialogInterface_OnCancelListener(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Interface("Landroid/content/DialogInterface$OnCancelListener;");
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

namespace ogplay::runtime::android_intrinsics {
Decl Declare_android_content_DialogInterface_OnCancelListener(const Context& context) {
    return dvm80_android_content_DialogInterface_OnCancelListener::Declare_android_content_DialogInterface_OnCancelListener(context);
}
}  // namespace ogplay::runtime::android_intrinsics

// ---- migrated from android_content_DialogInterface_OnClickListener.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics::dvm80_android_content_DialogInterface_OnClickListener {

Decl Declare_android_content_DialogInterface_OnClickListener(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Interface("Landroid/content/DialogInterface$OnClickListener;");
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

namespace ogplay::runtime::android_intrinsics {
Decl Declare_android_content_DialogInterface_OnClickListener(const Context& context) {
    return dvm80_android_content_DialogInterface_OnClickListener::Declare_android_content_DialogInterface_OnClickListener(context);
}
}  // namespace ogplay::runtime::android_intrinsics

// ---- migrated from android_content_DialogInterface_OnDismissListener.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics::dvm80_android_content_DialogInterface_OnDismissListener {

Decl Declare_android_content_DialogInterface_OnDismissListener(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Interface("Landroid/content/DialogInterface$OnDismissListener;");
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

namespace ogplay::runtime::android_intrinsics {
Decl Declare_android_content_DialogInterface_OnDismissListener(const Context& context) {
    return dvm80_android_content_DialogInterface_OnDismissListener::Declare_android_content_DialogInterface_OnDismissListener(context);
}
}  // namespace ogplay::runtime::android_intrinsics

// ---- migrated from android_content_Intent.cpp ----
// Intent handlers keep component targets and typed extras in the session
// context maps; flag/category setters are fluent no-ops.

#include "catalog.h"

namespace ogplay::runtime::android_intrinsics::dvm80_android_content_Intent {

Decl Declare_android_content_Intent(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/content/Intent;", "Ljava/lang/Object;");
    const auto intent_init = [](dx::IntrinsicContext&) {
        return dx::VmValue::Void();
    };
    const auto set_flags = [](dx::IntrinsicContext& call) {
        return Self(call);
    };
    builder.Constructor("(Ljava/lang/String;)V", intent_init);
    builder.Constructor("()V", intent_init);
    builder.Constructor("(Ljava/lang/String;Landroid/net/Uri;)V",
        intent_init);
    builder.Constructor("(Landroid/content/Context;Ljava/lang/Class;)V",
        [context](dx::IntrinsicContext& call) {
            const auto class_object = call.arguments[1].ref;
            const auto target = call.vm.Model().ClassOfClassObject(class_object);
            context->intent_components[call.receiver.Value()] =
                call.vm.Linker().Class(target).descriptor;
            return dx::VmValue::Void();
        });
    builder.FinalMethod("setClassName",
        "(Ljava/lang/String;Ljava/lang/String;)Landroid/content/Intent;",
        [context](dx::IntrinsicContext& call) {
            auto dotted = call.vm.StringUtf8(call.arguments[1].ref);
            std::string descriptor = "L";
            for (const auto unit : dotted) {
                descriptor.push_back(unit == '.' ? '/' : unit);
            }
            descriptor.push_back(';');
            context->intent_components[call.receiver.Value()] =
                std::move(descriptor);
            return Self(call);
        });
    builder.FinalMethod("addFlags", "(I)Landroid/content/Intent;", set_flags);
    builder.FinalMethod("putExtra",
        "(Ljava/lang/String;I)Landroid/content/Intent;",
        [context](dx::IntrinsicContext& call) {
            context->intent_int_extras[call.receiver.Value()]
                [call.vm.StringUtf8(call.arguments[0].ref)] =
                    call.arguments[1].AsInt();
            return Self(call);
        });
    builder.FinalMethod("putExtra",
        "(Ljava/lang/String;Ljava/lang/String;)Landroid/content/Intent;",
        [context](dx::IntrinsicContext& call) {
            context->intent_string_extras[call.receiver.Value()]
                [call.vm.StringUtf8(call.arguments[0].ref)] =
                    call.vm.StringUtf8(call.arguments[1].ref);
            return Self(call);
        });
    builder.FinalMethod("getStringExtra",
        "(Ljava/lang/String;)Ljava/lang/String;",
        [context](dx::IntrinsicContext& call) {
            const auto extras =
                context->intent_string_extras.find(call.receiver.Value());
            if (extras != context->intent_string_extras.end()) {
                const auto found = extras->second.find(
                    call.vm.StringUtf8(call.arguments[0].ref));
                if (found != extras->second.end()) {
                    return MakeString(call, found->second);
                }
            }
            return dx::VmValue::Ref(dx::VmObjectRef{});
        });
    builder.FinalMethod("getIntExtra", "(Ljava/lang/String;I)I",
        [context](dx::IntrinsicContext& call) {
            const auto extras =
                context->intent_int_extras.find(call.receiver.Value());
            if (extras != context->intent_int_extras.end()) {
                const auto found = extras->second.find(
                    call.vm.StringUtf8(call.arguments[0].ref));
                if (found != extras->second.end()) {
                    return dx::VmValue::Int(found->second);
                }
            }
            return dx::VmValue::Int(call.arguments[1].AsInt());
        });
    builder.FinalMethod("removeExtra", "(Ljava/lang/String;)V",
        [context](dx::IntrinsicContext& call) {
            const auto intent = call.receiver.Value();
            const auto name = call.vm.StringUtf8(call.arguments[0].ref);
            const auto remove = [intent, &name](auto& extras) {
                const auto values = extras.find(intent);
                if (values == extras.end()) return;
                values->second.erase(name);
                if (values->second.empty()) extras.erase(values);
            };
            remove(context->intent_string_extras);
            remove(context->intent_int_extras);
            return dx::VmValue::Void();
        });
    builder.FinalMethod("addCategory",
        "(Ljava/lang/String;)Landroid/content/Intent;", set_flags);
    builder.FinalMethod("getAction", "()Ljava/lang/String;",
        [](dx::IntrinsicContext&) {
            return dx::VmValue::Ref(dx::VmObjectRef{});
        });
    builder.FinalMethod("getExtras", "()Landroid/os/Bundle;",
        [](dx::IntrinsicContext&) {
            return dx::VmValue::Ref(dx::VmObjectRef{});
        });
    builder.FinalMethod("setFlags", "(I)Landroid/content/Intent;", set_flags);
    builder.FinalMethod("setDataAndType",
        "(Landroid/net/Uri;Ljava/lang/String;)Landroid/content/Intent;",
        [](dx::IntrinsicContext& call) { return Self(call); });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

namespace ogplay::runtime::android_intrinsics {
Decl Declare_android_content_Intent(const Context& context) {
    return dvm80_android_content_Intent::Declare_android_content_Intent(context);
}
}  // namespace ogplay::runtime::android_intrinsics

// ---- migrated from android_content_IntentFilter.cpp ----
#include "catalog.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <string_view>

namespace {

std::int32_t ParseJavaInt(const std::string_view text) {
    const auto fail = [&]() -> void {
        throw ogplay::runtime::dexvm::VmJavaThrow{
            "Ljava/lang/NumberFormatException;",
            "invalid IntentFilter authority port: " + std::string(text)};
    };
    if (text.empty()) fail();
    std::size_t cursor{};
    bool negative{};
    if (text[cursor] == '+' || text[cursor] == '-') {
        negative = text[cursor++] == '-';
        if (cursor == text.size()) fail();
    }
    constexpr std::uint64_t kPositiveLimit =
        static_cast<std::uint64_t>(std::numeric_limits<std::int32_t>::max());
    constexpr std::uint64_t kNegativeLimit = kPositiveLimit + 1U;
    const auto limit = negative ? kNegativeLimit : kPositiveLimit;
    std::uint64_t value{};
    for (; cursor < text.size(); ++cursor) {
        const auto ch = text[cursor];
        if (ch < '0' || ch > '9') fail();
        const auto digit = static_cast<std::uint64_t>(ch - '0');
        if (value > (limit - digit) / 10U) fail();
        value = value * 10U + digit;
    }
    const auto signed_value = negative ? -static_cast<std::int64_t>(value)
                                       : static_cast<std::int64_t>(value);
    return static_cast<std::int32_t>(signed_value);
}

}  // namespace

namespace ogplay::runtime::android_intrinsics::dvm80_android_content_IntentFilter {

Decl Declare_android_content_IntentFilter(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/content/IntentFilter;", "Ljava/lang/Object;");
    builder.Constructor("(Ljava/lang/String;)V",
        [context](dx::IntrinsicContext& call) {
            context->intent_filter_schemes.erase(call.receiver.Value());
            context->intent_filter_authorities.erase(call.receiver.Value());
            return dx::VmValue::Void();
        });
    builder.Constructor("()V",
        [context](dx::IntrinsicContext& call) {
            context->intent_filter_schemes.erase(call.receiver.Value());
            context->intent_filter_authorities.erase(call.receiver.Value());
            return dx::VmValue::Void();
        });
    builder.FinalMethod("addAction", "(Ljava/lang/String;)V",
        [](dx::IntrinsicContext&) { return dx::VmValue::Void(); });
    builder.FinalMethod("addDataScheme", "(Ljava/lang/String;)V",
        [context](dx::IntrinsicContext& call) {
            if (!call.arguments[0].ref.IsValid()) {
                throw dx::VmJavaThrow{"Ljava/lang/NullPointerException;",
                                      "IntentFilter data scheme is null"};
            }
            const auto scheme = call.vm.StringUtf8(call.arguments[0].ref);
            auto& schemes =
                context->intent_filter_schemes[call.receiver.Value()];
            if (std::find(schemes.begin(), schemes.end(), scheme) ==
                schemes.end()) {
                schemes.push_back(scheme);
            }
            return dx::VmValue::Void();
        });
    builder.FinalMethod("addDataAuthority",
        "(Ljava/lang/String;Ljava/lang/String;)V",
        [context](dx::IntrinsicContext& call) {
            if (!call.arguments[0].ref.IsValid()) {
                throw dx::VmJavaThrow{"Ljava/lang/NullPointerException;",
                                      "IntentFilter authority host is null"};
            }
            auto host = call.vm.StringUtf8(call.arguments[0].ref);
            const auto port = call.arguments[1].ref.IsValid()
                ? ParseJavaInt(call.vm.StringUtf8(call.arguments[1].ref))
                : -1;
            const auto wildcard = !host.empty() && host.front() == '*';
            context->intent_filter_authorities[call.receiver.Value()].push_back(
                DexVmAndroidContext::IntentFilterAuthority{
                    .original_host = host,
                    .match_host = wildcard ? host.substr(1) : std::move(host),
                    .wildcard = wildcard,
                    .port = port,
                });
            return dx::VmValue::Void();
        });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

namespace ogplay::runtime::android_intrinsics {
Decl Declare_android_content_IntentFilter(const Context& context) {
    return dvm80_android_content_IntentFilter::Declare_android_content_IntentFilter(context);
}
}  // namespace ogplay::runtime::android_intrinsics

// ---- migrated from android_content_pm_PackageManager.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics::dvm80_android_content_pm_PackageManager {

namespace {

constexpr std::int32_t kGetMetaData = 0x00000080;
constexpr std::int32_t kGetPermissions = 0x00001000;
constexpr std::int32_t kPermissionGranted = 0;
constexpr std::int32_t kPermissionDenied = -1;

[[nodiscard]] const dx::LinkedField& Field(dx::IntrinsicContext& call,
                                           const dx::VmObjectRef object,
                                           const std::string_view name,
                                           const std::string_view descriptor) {
    const auto field = call.vm.Linker().FindFieldRecursive(
        call.vm.Model().ObjectClass(object), std::string(name),
        std::string(descriptor));
    if (!field.has_value()) {
        throw dx::DexVmError(dx::DexVmErrorReason::internal_invariant,
                             "PackageManager field is not linked: " +
                                 std::string(name));
    }
    return call.vm.Linker().Field(*field);
}

void SetInt(dx::IntrinsicContext& call, const dx::VmObjectRef object,
            const std::string_view name, const std::int32_t value) {
    const auto& field = Field(call, object, name, "I");
    call.vm.Model().InstanceSlots(object)[field.slot] = {
        static_cast<std::uint32_t>(value), dx::SlotTag::cat1};
}

void SetBoolean(dx::IntrinsicContext& call, const dx::VmObjectRef object,
                const std::string_view name, const bool value) {
    const auto& field = Field(call, object, name, "Z");
    call.vm.Model().InstanceSlots(object)[field.slot] = {
        value ? 1U : 0U, dx::SlotTag::cat1};
}

void SetRef(dx::IntrinsicContext& call, const dx::VmObjectRef object,
            const std::string_view name, const std::string_view descriptor,
            const dx::VmObjectRef value) {
    const auto& field = Field(call, object, name, descriptor);
    call.vm.Model().InstanceSlots(object)[field.slot] = {
        value.Value(), dx::SlotTag::ref};
}

[[nodiscard]] dx::VmObjectRef String(dx::IntrinsicContext& call,
                                     const std::string& value) {
    return call.vm.NewStringUtf8(value);
}

[[nodiscard]] std::string RequiredString(dx::IntrinsicContext& call,
                                         const std::size_t argument,
                                         const std::string_view name) {
    dx::IntrinsicCall typed(call);
    return call.vm.StringUtf8(typed.NonNullRef(argument, name));
}

void RequireCurrentPackage(const Context& context,
                           const std::string_view package_name) {
    if (package_name != context->package_name) {
        throw dx::VmJavaThrow{
            "Landroid/content/pm/PackageManager$NameNotFoundException;",
            std::string(package_name)};
    }
}

void RequireFlags(const std::int32_t flags, const std::int32_t supported,
                  const std::string_view method) {
    if ((flags & ~supported) != 0) {
        throw dx::VmJavaThrow{
            "Ljava/lang/UnsupportedOperationException;",
            std::string(method) + " flags are outside the bounded API19 " +
                "PackageManager surface: " + std::to_string(flags)};
    }
}

[[nodiscard]] dx::VmObjectRef MakeMetaData(dx::IntrinsicContext& call,
                                           const Context& context) {
    const auto bundle =
        call.vm.NewIntrinsicInstance("Landroid/os/Bundle;");
    auto& values = context->bundles[bundle.Value()];
    for (const auto& [name, value] : context->application_meta_data) {
        if (const auto* integer = std::get_if<std::int32_t>(&value)) {
            values.emplace(name, *integer);
        } else {
            values.emplace(name, std::get<std::string>(value));
        }
    }
    return bundle;
}

[[nodiscard]] dx::VmObjectRef MakeApplicationInfo(
    dx::IntrinsicContext& call, const Context& context,
    const std::int32_t flags) {
    const auto info = call.vm.NewIntrinsicInstance(
        "Landroid/content/pm/ApplicationInfo;");
    const auto package = String(call, context->package_name);
    const auto application_name = String(call, context->application_class_name);
    SetRef(call, info, "name", "Ljava/lang/String;", application_name);
    SetRef(call, info, "packageName", "Ljava/lang/String;", package);
    SetRef(call, info, "className", "Ljava/lang/String;", application_name);
    SetRef(call, info, "processName", "Ljava/lang/String;", package);
    SetInt(call, info, "icon", static_cast<std::int32_t>(context->application_icon));
    SetInt(call, info, "uid", static_cast<std::int32_t>(context->application_uid));
    SetInt(call, info, "targetSdkVersion",
           static_cast<std::int32_t>(context->target_sdk_version));
    SetBoolean(call, info, "enabled", true);
    SetRef(call, info, "dataDir", "Ljava/lang/String;",
           String(call, "/data/data/" + context->package_name));
    if (context->application_label.has_value()) {
        if (const auto* resource = std::get_if<std::uint32_t>(
                &*context->application_label)) {
            SetInt(call, info, "labelRes", static_cast<std::int32_t>(*resource));
        } else {
            SetRef(call, info, "nonLocalizedLabel", "Ljava/lang/CharSequence;",
                   String(call, std::get<std::string>(*context->application_label)));
        }
    }
    if ((flags & kGetMetaData) != 0) {
        SetRef(call, info, "metaData", "Landroid/os/Bundle;",
               MakeMetaData(call, context));
    }
    return info;
}

[[nodiscard]] dx::VmObjectRef MakeStringArray(
    dx::IntrinsicContext& call, const std::vector<std::string>& values) {
    const auto array_class =
        call.vm.Linker().ResolveDescriptor("[Ljava/lang/String;");
    const auto string_class =
        call.vm.Linker().ResolveDescriptor("Ljava/lang/String;");
    const auto array = call.vm.Model().NewObjectArray(
        array_class, string_class, static_cast<JniSize>(values.size()));
    JniSize index{};
    for (const auto& value : values) {
        call.vm.Model().SetObjectElement(array, index++, String(call, value));
    }
    return array;
}

[[nodiscard]] std::string ApplicationPackageName(
    dx::IntrinsicContext& call, const dx::VmObjectRef info) {
    const auto& field = Field(call, info, "packageName", "Ljava/lang/String;");
    const auto slot = call.vm.Model().InstanceSlots(info)[field.slot];
    if (slot.tag != dx::SlotTag::ref || slot.bits == 0U) return {};
    return call.vm.StringUtf8(dx::VmObjectRef{static_cast<std::uint32_t>(slot.bits)});
}

}  // namespace

Decl Declare_android_content_pm_PackageItemInfo(const Context&) {
    auto builder = dx::IntrinsicClassBuilder::Class(
        "Landroid/content/pm/PackageItemInfo;", "Ljava/lang/Object;");
    builder.Constructor("()V", [](dx::IntrinsicContext&) {
        return dx::VmValue::Void();
    });
    builder.InstanceField("name", "Ljava/lang/String;")
        .InstanceField("packageName", "Ljava/lang/String;")
        .InstanceField("labelRes", "I")
        .InstanceField("nonLocalizedLabel", "Ljava/lang/CharSequence;")
        .InstanceField("icon", "I")
        .InstanceField("logo", "I")
        .InstanceField("metaData", "Landroid/os/Bundle;");
    return std::move(builder).Build();
}

Decl Declare_android_content_pm_ApplicationInfo(const Context&) {
    auto builder = dx::IntrinsicClassBuilder::Class(
        "Landroid/content/pm/ApplicationInfo;",
        "Landroid/content/pm/PackageItemInfo;");
    builder.Constructor("()V", [](dx::IntrinsicContext&) {
        return dx::VmValue::Void();
    });
    builder.InstanceField("taskAffinity", "Ljava/lang/String;")
        .InstanceField("permission", "Ljava/lang/String;")
        .InstanceField("processName", "Ljava/lang/String;")
        .InstanceField("className", "Ljava/lang/String;")
        .InstanceField("descriptionRes", "I")
        .InstanceField("theme", "I")
        .InstanceField("flags", "I")
        .InstanceField("sourceDir", "Ljava/lang/String;")
        .InstanceField("publicSourceDir", "Ljava/lang/String;")
        .InstanceField("dataDir", "Ljava/lang/String;")
        .InstanceField("nativeLibraryDir", "Ljava/lang/String;")
        .InstanceField("uid", "I")
        .InstanceField("targetSdkVersion", "I")
        .InstanceField("enabled", "Z");
    return std::move(builder).Build();
}

Decl Declare_android_content_pm_PackageInfo(const Context&) {
    auto builder = dx::IntrinsicClassBuilder::Class(
        "Landroid/content/pm/PackageInfo;", "Ljava/lang/Object;");
    builder.Constructor("()V", [](dx::IntrinsicContext&) {
        return dx::VmValue::Void();
    });
    builder.InstanceField("packageName", "Ljava/lang/String;")
        .InstanceField("versionCode", "I")
        .InstanceField("versionName", "Ljava/lang/String;")
        .InstanceField("applicationInfo", "Landroid/content/pm/ApplicationInfo;")
        .InstanceField("requestedPermissions", "[Ljava/lang/String;");
    return std::move(builder).Build();
}

Decl Declare_android_content_pm_PackageManager_NameNotFoundException(
    const Context&) {
    auto builder = dx::IntrinsicClassBuilder::Class(
        "Landroid/content/pm/PackageManager$NameNotFoundException;",
        "Ljava/lang/Exception;");
    builder.Constructor("()V", [](dx::IntrinsicContext&) {
        return dx::VmValue::Void();
    });
    builder.Constructor("(Ljava/lang/String;)V", [](dx::IntrinsicContext&) {
        return dx::VmValue::Void();
    });
    return std::move(builder).Build();
}

Decl Declare_android_content_pm_PackageManager(const Context& context) {
    constexpr std::uint32_t kPublicAbstract = 0x0401U;
    auto builder = dx::IntrinsicClassBuilder::Class(
        "Landroid/content/pm/PackageManager;", "Ljava/lang/Object;", {},
        kPublicAbstract);
    builder.ConstantInt("GET_META_DATA", "I", kGetMetaData, 0x0019U)
        .ConstantInt("GET_PERMISSIONS", "I", kGetPermissions, 0x0019U)
        .ConstantInt("PERMISSION_GRANTED", "I", kPermissionGranted, 0x0019U)
        .ConstantInt("PERMISSION_DENIED", "I", kPermissionDenied, 0x0019U)
        .ConstantString("FEATURE_TOUCHSCREEN", "android.hardware.touchscreen",
                        0x0019U)
        .ConstantString("FEATURE_SCREEN_LANDSCAPE",
                        "android.hardware.screen.landscape", 0x0019U)
        .ConstantString("FEATURE_SCREEN_PORTRAIT",
                        "android.hardware.screen.portrait", 0x0019U);
    builder.VirtualMethod(
        "getApplicationInfo",
        "(Ljava/lang/String;I)Landroid/content/pm/ApplicationInfo;",
        [context](dx::IntrinsicContext& call) {
            const auto package = RequiredString(call, 0U, "packageName");
            const auto flags = call.arguments[1].AsInt();
            RequireCurrentPackage(context, package);
            RequireFlags(flags, kGetMetaData, "getApplicationInfo");
            return dx::VmValue::Ref(MakeApplicationInfo(call, context, flags));
        });
    builder.VirtualMethod(
        "getPackageInfo",
        "(Ljava/lang/String;I)Landroid/content/pm/PackageInfo;",
        [context](dx::IntrinsicContext& call) {
            const auto package = RequiredString(call, 0U, "packageName");
            const auto flags = call.arguments[1].AsInt();
            RequireCurrentPackage(context, package);
            RequireFlags(flags, kGetMetaData | kGetPermissions,
                         "getPackageInfo");
            const auto info = call.vm.NewIntrinsicInstance(
                "Landroid/content/pm/PackageInfo;");
            SetRef(call, info, "packageName", "Ljava/lang/String;",
                   String(call, context->package_name));
            SetInt(call, info, "versionCode",
                   static_cast<std::int32_t>(context->package_version_code));
            SetRef(call, info, "versionName", "Ljava/lang/String;",
                   String(call, context->package_version_name));
            SetRef(call, info, "applicationInfo",
                   "Landroid/content/pm/ApplicationInfo;",
                   MakeApplicationInfo(call, context, flags & kGetMetaData));
            if ((flags & kGetPermissions) != 0) {
                SetRef(call, info, "requestedPermissions", "[Ljava/lang/String;",
                       MakeStringArray(call, context->requested_permissions));
            }
            return dx::VmValue::Ref(info);
        });
    builder.VirtualMethod(
        "getApplicationLabel",
        "(Landroid/content/pm/ApplicationInfo;)Ljava/lang/CharSequence;",
        [context](dx::IntrinsicContext& call) {
            dx::IntrinsicCall typed(call);
            const auto info = typed.NonNullRef(0U, "info");
            RequireCurrentPackage(context, ApplicationPackageName(call, info));
            if (context->application_label.has_value()) {
                if (const auto* literal = std::get_if<std::string>(
                        &*context->application_label)) {
                    return MakeString(call, *literal);
                }
                return dx::VmValue::Ref(call.vm.Model().NewString(
                    ResolveUiString(
                        *context,
                        std::get<std::uint32_t>(*context->application_label))));
            }
            return MakeString(call, context->package_name);
        });
    builder.VirtualMethod(
        "checkPermission", "(Ljava/lang/String;Ljava/lang/String;)I",
        [context](dx::IntrinsicContext& call) {
            const auto permission = RequiredString(call, 0U, "permissionName");
            const auto package = RequiredString(call, 1U, "packageName");
            return dx::VmValue::Int(
                package == context->package_name &&
                        context->granted_permissions.contains(permission)
                    ? kPermissionGranted
                    : kPermissionDenied);
        });
    builder.VirtualMethod(
        "hasSystemFeature", "(Ljava/lang/String;)Z",
        [context](dx::IntrinsicContext& call) {
            const auto feature = RequiredString(call, 0U, "name");
            return dx::VmValue::Int(
                context->system_features.contains(feature) ? 1 : 0);
        });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

namespace ogplay::runtime::android_intrinsics {
Decl Declare_android_content_pm_PackageItemInfo(const Context& context) {
    return dvm80_android_content_pm_PackageManager::Declare_android_content_pm_PackageItemInfo(context);
}
Decl Declare_android_content_pm_ApplicationInfo(const Context& context) {
    return dvm80_android_content_pm_PackageManager::Declare_android_content_pm_ApplicationInfo(context);
}
Decl Declare_android_content_pm_PackageInfo(const Context& context) {
    return dvm80_android_content_pm_PackageManager::Declare_android_content_pm_PackageInfo(context);
}
Decl Declare_android_content_pm_PackageManager(const Context& context) {
    return dvm80_android_content_pm_PackageManager::Declare_android_content_pm_PackageManager(context);
}
}  // namespace ogplay::runtime::android_intrinsics

// ---- migrated from android_content_SharedPreferences_Editor.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics::dvm80_android_content_SharedPreferences_Editor {

Decl Declare_android_content_SharedPreferences_Editor(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Interface("Landroid/content/SharedPreferences$Editor;");
    builder.FinalMethod("putBoolean", "(Ljava/lang/String;Z)Landroid/content/SharedPreferences$Editor;", PrefsEditorPutBooleanHandler(context));
    builder.FinalMethod("putInt", "(Ljava/lang/String;I)Landroid/content/SharedPreferences$Editor;", PrefsEditorPutIntHandler(context));
    builder.FinalMethod("putLong", "(Ljava/lang/String;J)Landroid/content/SharedPreferences$Editor;", PrefsEditorPutLongHandler(context));
    builder.FinalMethod("putString", "(Ljava/lang/String;Ljava/lang/String;)Landroid/content/SharedPreferences$Editor;", PrefsEditorPutStringHandler(context));
    builder.FinalMethod("commit", "()Z", PrefsEditorCommitHandler(context));
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

namespace ogplay::runtime::android_intrinsics {
Decl Declare_android_content_SharedPreferences_Editor(const Context& context) {
    return dvm80_android_content_SharedPreferences_Editor::Declare_android_content_SharedPreferences_Editor(context);
}
}  // namespace ogplay::runtime::android_intrinsics

// ---- migrated from android_content_SharedPreferences.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics::dvm80_android_content_SharedPreferences {

Decl Declare_android_content_SharedPreferences(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Interface("Landroid/content/SharedPreferences;");
    builder.FinalMethod("edit", "()Landroid/content/SharedPreferences$Editor;", PrefsEditHandler(context));
    builder.FinalMethod("getBoolean", "(Ljava/lang/String;Z)Z", PrefsGetBooleanHandler(context));
    builder.FinalMethod("getInt", "(Ljava/lang/String;I)I", PrefsGetIntHandler(context));
    builder.FinalMethod("getLong", "(Ljava/lang/String;J)J", PrefsGetLongHandler(context));
    builder.FinalMethod("getString", "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;", PrefsGetStringHandler(context));
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

namespace ogplay::runtime::android_intrinsics {
Decl Declare_android_content_SharedPreferences(const Context& context) {
    return dvm80_android_content_SharedPreferences::Declare_android_content_SharedPreferences(context);
}
}  // namespace ogplay::runtime::android_intrinsics

// ---- migrated from android_content_SharedPreferencesEditorImpl.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics::dvm80_android_content_SharedPreferencesEditorImpl {

Decl Declare_android_content_SharedPreferencesEditorImpl(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/content/SharedPreferencesEditorImpl;", "Ljava/lang/Object;", {"Landroid/content/SharedPreferences$Editor;"});
    builder.FinalMethod("putBoolean", "(Ljava/lang/String;Z)Landroid/content/SharedPreferences$Editor;", PrefsEditorPutBooleanHandler(context));
    builder.FinalMethod("putInt", "(Ljava/lang/String;I)Landroid/content/SharedPreferences$Editor;", PrefsEditorPutIntHandler(context));
    builder.FinalMethod("putLong", "(Ljava/lang/String;J)Landroid/content/SharedPreferences$Editor;", PrefsEditorPutLongHandler(context));
    builder.FinalMethod("putString", "(Ljava/lang/String;Ljava/lang/String;)Landroid/content/SharedPreferences$Editor;", PrefsEditorPutStringHandler(context));
    builder.FinalMethod("commit", "()Z", PrefsEditorCommitHandler(context));
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

namespace ogplay::runtime::android_intrinsics {
Decl Declare_android_content_SharedPreferencesEditorImpl(const Context& context) {
    return dvm80_android_content_SharedPreferencesEditorImpl::Declare_android_content_SharedPreferencesEditorImpl(context);
}
}  // namespace ogplay::runtime::android_intrinsics

// ---- migrated from android_content_SharedPreferencesImpl.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics::dvm80_android_content_SharedPreferencesImpl {

Decl Declare_android_content_SharedPreferencesImpl(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/content/SharedPreferencesImpl;", "Ljava/lang/Object;", {"Landroid/content/SharedPreferences;"});
    builder.FinalMethod("edit", "()Landroid/content/SharedPreferences$Editor;", PrefsEditHandler(context));
    builder.FinalMethod("getBoolean", "(Ljava/lang/String;Z)Z", PrefsGetBooleanHandler(context));
    builder.FinalMethod("getInt", "(Ljava/lang/String;I)I", PrefsGetIntHandler(context));
    builder.FinalMethod("getLong", "(Ljava/lang/String;J)J", PrefsGetLongHandler(context));
    builder.FinalMethod("getString", "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;", PrefsGetStringHandler(context));
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

namespace ogplay::runtime::android_intrinsics {
Decl Declare_android_content_SharedPreferencesImpl(const Context& context) {
    return dvm80_android_content_SharedPreferencesImpl::Declare_android_content_SharedPreferencesImpl(context);
}
}  // namespace ogplay::runtime::android_intrinsics
