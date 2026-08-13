#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

namespace {

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
    dx::IntrinsicClassBuilder builder("Landroid/content/Context;");
    builder.Super("Ljava/lang/Object;");
    builder.Virtual("<init>", "()V", [](dx::IntrinsicContext&) {
        return dx::VmValue::Void();
    });
    builder.Virtual("getAssets", "()Landroid/content/res/AssetManager;",
        [context](dx::IntrinsicContext& call) {
            return dx::VmValue::Ref(Singleton(
                call, context, "assets", "Landroid/content/res/AssetManager;"));
        });
    builder.Virtual("getPackageName", "()Ljava/lang/String;",
        [context](dx::IntrinsicContext& call) {
            return MakeString(call, context->package_name);
        });
    builder.Virtual("getResources", "()Landroid/content/res/Resources;",
        [context](dx::IntrinsicContext& call) {
            return dx::VmValue::Ref(Singleton(call, context, "resources",
                "Landroid/content/res/Resources;"));
        });
    builder.Virtual("getSystemService",
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
    builder.Virtual("registerReceiver",
        "(Landroid/content/BroadcastReceiver;Landroid/content/IntentFilter;)Landroid/content/Intent;",
        [](dx::IntrinsicContext&) {
            // Sticky broadcast lookup: nothing pending on this platform.
            return dx::VmValue::Ref(dx::VmObjectRef{});
        });
    builder.Virtual("startActivity", "(Landroid/content/Intent;)V",
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
            context->current_intent = intent;
            return dx::VmValue::Void();
        });
    builder.Virtual("getSharedPreferences", "(Ljava/lang/String;I)Landroid/content/SharedPreferences;",
        [context](dx::IntrinsicContext& call) {
            const auto name = call.vm.StringUtf8(call.arguments[0].ref);
            const auto instance = Singleton(
                call, context, "prefs:" + name,
                "Landroid/content/SharedPreferencesImpl;");
            context->preference_names[instance.Value()] = name;
            LoadPreferencesOnce(context, name);
            return dx::VmValue::Ref(instance);
        });
    builder.Virtual("getContentResolver", "()Landroid/content/ContentResolver;",
        [context](dx::IntrinsicContext& call) {
            return dx::VmValue::Ref(
                Singleton(call, context, "content_resolver",
                          "Landroid/content/ContentResolver;"));
        });
    builder.Virtual("sendBroadcast", "(Landroid/content/Intent;)V",
        [](dx::IntrinsicContext& call) {
            // No other process exists; the broadcast truthfully has no
            // audience. Logged so silent drops stay visible.
            GuestLog(call, core::LogLevel::debug,
                     "sendBroadcast dropped: no receivers on this platform");
            return dx::VmValue::Void();
        });
    builder.Virtual("getExternalFilesDir", "(Ljava/lang/String;)Ljava/io/File;",
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
    builder.Virtual("startService",
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
