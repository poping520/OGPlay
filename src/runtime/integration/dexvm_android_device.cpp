// Device service handlers (Log/telephony/Wi-Fi/sensors/Looper/threads)
// plus session-lifetime SharedPreferences. Offline identity is
// deterministic; SMS and network actions fail with accounting.

#include "dexvm_android_internal.h"

#include <array>
#include <cctype>

namespace ogplay::runtime::android_intrinsics {

void RegisterDeviceServices(dx::IntrinsicRegistry& registry,
                            const Context& context) {
    registry.Register("android.log.d", [](dx::IntrinsicContext& call) {
        GuestLog(call, core::LogLevel::debug,
                 call.vm.StringUtf8(call.arguments[0].ref) + ": " +
                     call.vm.StringUtf8(call.arguments[1].ref));
        return dx::VmValue::Int(0);
    });
    registry.Register("android.log.i", [](dx::IntrinsicContext& call) {
        GuestLog(call, core::LogLevel::info,
                 call.vm.StringUtf8(call.arguments[0].ref) + ": " +
                     call.vm.StringUtf8(call.arguments[1].ref));
        return dx::VmValue::Int(0);
    });
    registry.Register("android.log.w", [](dx::IntrinsicContext& call) {
        GuestLog(call, core::LogLevel::warn,
                 call.vm.StringUtf8(call.arguments[0].ref) + ": " +
                     call.vm.StringUtf8(call.arguments[1].ref));
        return dx::VmValue::Int(0);
    });
    registry.Register("android.log.e", [](dx::IntrinsicContext& call) {
        GuestLog(call, core::LogLevel::error,
                 call.vm.StringUtf8(call.arguments[0].ref) + ": " +
                     call.vm.StringUtf8(call.arguments[1].ref));
        return dx::VmValue::Int(0);
    });
    registry.Register("android.audio_manager.get_ringer_mode",
                      [](dx::IntrinsicContext&) {
        return dx::VmValue::Int(2);  // RINGER_MODE_NORMAL
    });
    registry.Register("android.audio_manager.is_music_active",
                      [](dx::IntrinsicContext&) {
        // OGPlay owns the session mixer and does not expose a separate host
        // media session, so no external music is active.
        return dx::VmValue::Int(0);
    });
    registry.Register("android.audio_manager.get_stream_max_volume",
                      [](dx::IntrinsicContext&) {
        return dx::VmValue::Int(15);
    });
    registry.Register("android.audio_manager.set_stream_volume",
                      [](dx::IntrinsicContext&) {
        return dx::VmValue::Void();
    });
    registry.Register("android.wifi.is_enabled", [](dx::IntrinsicContext&) {
        return dx::VmValue::Int(0);
    });
    registry.Register("android.wifi.get_state", [](dx::IntrinsicContext&) {
        return dx::VmValue::Int(1);  // WIFI_STATE_DISABLED
    });
    registry.Register("android.wifi.set_enabled",
                      [](dx::IntrinsicContext&) {
        // The platform has no radio to enable; the call truthfully fails.
        return dx::VmValue::Int(0);
    });
    registry.Register("android.wifi.get_connection_info",
                      [](dx::IntrinsicContext&) {
        return dx::VmValue::Ref(dx::VmObjectRef{});
    });
    registry.Register("android.wifi.create_lock",
                      [](dx::IntrinsicContext& call) {
        return dx::VmValue::Ref(call.vm.NewIntrinsicInstance(
            "Landroid/net/wifi/WifiManager$WifiLock;"));
    });
    registry.Register("android.telephony.empty_string",
                      [](dx::IntrinsicContext& call) {
        // Absent-SIM answers are the empty string per the platform docs.
        return MakeString(call, "");
    });
    registry.Register("android.telephony.false",
                      [](dx::IntrinsicContext&) {
        return dx::VmValue::Int(0);
    });
    registry.Register("android.telephony.get_sim_state",
                      [](dx::IntrinsicContext&) {
        return dx::VmValue::Int(1);  // SIM_STATE_ABSENT
    });
    registry.Register("android.telephony.get_phone_type",
                      [](dx::IntrinsicContext&) {
        return dx::VmValue::Int(0);  // PHONE_TYPE_NONE
    });
    registry.Register("android.telephony.listen",
                      [context](dx::IntrinsicContext& call) {
        const auto listener = call.arguments[0].ref;
        const auto events = call.arguments[1].AsInt();
        if (!listener.IsValid()) {
            throw dx::DexVmError(
                dx::DexVmErrorReason::invalid_operand,
                "TelephonyManager.listen requires a listener");
        }
        if (events == 0) {
            context->telephony_listeners.erase(listener.Value());
        } else {
            context->telephony_listeners[listener.Value()] = events;
        }
        return dx::VmValue::Void();
    });
    registry.Register("android.sensor.get_type", [](dx::IntrinsicContext&) {
        return dx::VmValue::Int(1);  // TYPE_ACCELEROMETER
    });
    registry.Register("android.sensor_manager.get_default",
                      [](dx::IntrinsicContext&) {
        // No host sensors: games observe the documented "no sensor" result.
        return dx::VmValue::Ref(dx::VmObjectRef{});
    });
    registry.Register("android.sensor_manager.register",
                      [](dx::IntrinsicContext&) {
        return dx::VmValue::Int(0);
    });
    registry.Register("android.sensor_manager.unregister",
                      [](dx::IntrinsicContext&) {
        return dx::VmValue::Void();
    });
    registry.Register("android.telephony.get_device_id",
                      [context](dx::IntrinsicContext& call) {
        return MakeString(call, context->device_id);
    });
    registry.Register("android.telephony.get_software_version",
                      [context](dx::IntrinsicContext& call) {
        return MakeString(call, context->device_software_version);
    });
    registry.Register("android.telephony.get_line1_number",
                      [context](dx::IntrinsicContext& call) {
        return MakeString(call, context->line_number);
    });
    registry.Register("android.telephony.get_network_operator",
                      [context](dx::IntrinsicContext& call) {
        return MakeString(call, context->network_operator);
    });
    registry.Register("android.locale.get_default",
                      [context](dx::IntrinsicContext& call) {
        return dx::VmValue::Ref(Singleton(call, context, "locale",
                                          "Ljava/util/Locale;"));
    });
    registry.Register("android.locale.get_iso3_language",
                      [context](dx::IntrinsicContext& call) {
        return MakeString(call, context->iso3_language);
    });
    registry.Register("android.locale.get_iso3_country",
                      [context](dx::IntrinsicContext& call) {
        return MakeString(call, context->iso3_country);
    });
    registry.Register("android.locale.get_country",
                      [context](dx::IntrinsicContext& call) {
        return MakeString(call, context->iso_country);
    });
    registry.Register("android.thread.sleep",
                      [context](dx::IntrinsicContext& call) {
        // Unified deterministic time: sleeping advances published uptime.
        context->uptime_millis += call.arguments[0].AsLong();
        return dx::VmValue::Void();
    });
    registry.Register("android.looper.noop", [](dx::IntrinsicContext&) {
        return dx::VmValue::Void();
    });
    registry.Register("android.handler.init", [](dx::IntrinsicContext&) {
        return dx::VmValue::Void();
    });
    registry.Register("android.handler.handle_message_noop",
                      [](dx::IntrinsicContext&) {
        return dx::VmValue::Void();
    });
    registry.Register("android.handler.obtain_message",
                      [](dx::IntrinsicContext& call) {
        return dx::VmValue::Ref(
            call.vm.NewIntrinsicInstance("Landroid/os/Message;"));
    });
    // Single VM host thread: messages deliver synchronously through the
    // handler's handleMessage override.
    const auto deliver_message = [](dx::IntrinsicContext& call,
                                    const dx::VmObjectRef handler,
                                    const dx::VmObjectRef message) {
        auto& vm = call.vm;
        auto& linker = vm.Linker();
        if (!handler.IsValid()) {
            throw dx::VmJavaThrow{"Ljava/lang/NullPointerException;",
                                  "message has no delivery target"};
        }
        const auto handler_class = vm.Model().ObjectClass(handler);
        const auto index = linker.FindVtableIndex(
            handler_class, "handleMessage", "(Landroid/os/Message;)V");
        if (!index.has_value()) {
            throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;",
                                  "handler has no handleMessage"};
        }
        const auto outcome = vm.Call(
            linker.Class(handler_class).vtable[*index],
            std::vector<dx::VmValue>{dx::VmValue::Ref(handler),
                                     dx::VmValue::Ref(message)});
        if (outcome.exception.IsValid()) {
            throw dx::VmJavaThrow{"Ljava/lang/RuntimeException;",
                                  "handleMessage raised: " +
                                      outcome.exception_message};
        }
    };
    registry.Register("android.handler.send_message",
                      [deliver_message](dx::IntrinsicContext& call) {
        deliver_message(call, call.receiver, call.arguments[0].ref);
        return dx::VmValue::Int(1);
    });
    registry.Register("android.handler.dispatch_message",
                      [deliver_message](dx::IntrinsicContext& call) {
        deliver_message(call, call.receiver, call.arguments[0].ref);
        return dx::VmValue::Void();
    });
    registry.Register("android.looper.get_main_looper",
                      [context](dx::IntrinsicContext& call) {
        return dx::VmValue::Ref(Singleton(call, context, "main_looper",
                                          "Landroid/os/Looper;"));
    });
    // Message slot layout follows the catalog field order:
    // what/arg1/arg2/obj/target.
    const auto make_message = [](dx::IntrinsicContext& call,
                                 const std::int32_t what,
                                 const dx::VmObjectRef object,
                                 const dx::VmObjectRef target) {
        const auto message =
            call.vm.NewIntrinsicInstance("Landroid/os/Message;");
        const auto slots = call.vm.Model().InstanceSlots(message);
        slots[0] = {static_cast<std::uint32_t>(what), dx::SlotTag::cat1};
        slots[3] = {object.Value(), dx::SlotTag::ref};
        slots[4] = {target.Value(), dx::SlotTag::ref};
        return message;
    };
    registry.Register("android.handler.obtain_message_what",
                      [make_message](dx::IntrinsicContext& call) {
        return dx::VmValue::Ref(make_message(call,
                                             call.arguments[0].AsInt(),
                                             dx::VmObjectRef{},
                                             call.receiver));
    });
    registry.Register("android.handler.obtain_message_what_obj",
                      [make_message](dx::IntrinsicContext& call) {
        return dx::VmValue::Ref(make_message(call,
                                             call.arguments[0].AsInt(),
                                             call.arguments[1].ref,
                                             call.receiver));
    });
    registry.Register("android.message.obtain_static",
                      [make_message](dx::IntrinsicContext& call) {
        return dx::VmValue::Ref(make_message(call,
                                             call.arguments[1].AsInt(),
                                             call.arguments[2].ref,
                                             call.arguments[0].ref));
    });
    registry.Register("android.message.send_to_target",
                      [deliver_message](dx::IntrinsicContext& call) {
        const auto slots = call.vm.Model().InstanceSlots(call.receiver);
        deliver_message(call, dx::VmObjectRef(slots[4].bits),
                        call.receiver);
        return dx::VmValue::Void();
    });
    registry.Register("android.handler.post",
                      [](dx::IntrinsicContext& call) {
        auto& vm = call.vm;
        auto& linker = vm.Linker();
        const auto runnable = call.arguments[0].ref;
        if (!runnable.IsValid()) {
            throw dx::VmJavaThrow{"Ljava/lang/NullPointerException;",
                                  "posted Runnable is null"};
        }
        const auto runnable_class = vm.Model().ObjectClass(runnable);
        const auto index =
            linker.FindVtableIndex(runnable_class, "run", "()V");
        if (!index.has_value()) {
            throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;",
                                  "posted object has no run()"};
        }
        const auto outcome = vm.Call(
            linker.Class(runnable_class).vtable[*index],
            std::vector<dx::VmValue>{dx::VmValue::Ref(runnable)});
        if (outcome.exception.IsValid()) {
            throw dx::VmJavaThrow{"Ljava/lang/RuntimeException;",
                                  "posted run() raised: " +
                                      outcome.exception_message};
        }
        return dx::VmValue::Int(1);
    });
    registry.Register("android.thread.init_runnable",
                      [context](dx::IntrinsicContext& call) {
        context->java_threads[call.receiver.Value()] =
            DexVmAndroidContext::JavaThreadState{call.arguments[0].ref,
                                                 false, false};
        return dx::VmValue::Void();
    });
    registry.Register("android.thread.start",
                      [context](dx::IntrinsicContext& call) {
        const auto found =
            context->java_threads.find(call.receiver.Value());
        if (found == context->java_threads.end()) {
            throw dx::VmJavaThrow{
                "Ljava/lang/IllegalThreadStateException;",
                "thread has no runnable target"};
        }
        if (found->second.started) {
            throw dx::VmJavaThrow{
                "Ljava/lang/IllegalThreadStateException;",
                "thread started twice"};
        }
        found->second.started = true;
        context->java_thread_queue.push_back(call.receiver);
        return dx::VmValue::Void();
    });
    registry.Register("android.thread.join",
                      [context](dx::IntrinsicContext& call) {
        // Cooperative model: join runs the target to completion now.
        const auto error =
            RunJavaThreadNow(call.vm, *context, call.receiver);
        if (error.has_value()) {
            throw dx::VmJavaThrow{"Ljava/lang/RuntimeException;", *error};
        }
        return dx::VmValue::Void();
    });
    registry.Register("android.timer.init", [](dx::IntrinsicContext&) {
        return dx::VmValue::Void();
    });
    registry.Register("android.timer.schedule",
                      [context](dx::IntrinsicContext& call) {
        // One-shot task on the cooperative queue; the delay collapses to
        // the next lifecycle frame boundary (deterministic clock).
        const auto task = call.arguments[0].ref;
        if (!task.IsValid()) {
            throw dx::VmJavaThrow{"Ljava/lang/NullPointerException;",
                                  "scheduled TimerTask is null"};
        }
        context->java_threads[task.Value()] =
            DexVmAndroidContext::JavaThreadState{task, true, false};
        context->java_thread_queue.push_back(task);
        return dx::VmValue::Void();
    });
    registry.Register("android.timer.schedule_repeating",
                      [](dx::IntrinsicContext&) -> dx::VmValue {
        // Unbounded repetition cannot terminate under the cooperative
        // model; recorded gap, explicit failure.
        throw dx::VmJavaThrow{
            "Ljava/lang/UnsupportedOperationException;",
            "repeating Timer.schedule is not provided"};
    });
    registry.Register("android.timer.cancel",
                      [context](dx::IntrinsicContext&) {
        // Cancels everything still pending (per-timer task tracking is
        // not kept; a single installer timer is the observed use).
        context->java_thread_queue.clear();
        return dx::VmValue::Void();
    });
    registry.Register("android.thread.is_alive",
                      [context](dx::IntrinsicContext& call) {
        const auto found =
            context->java_threads.find(call.receiver.Value());
        const bool alive = found != context->java_threads.end() &&
                           found->second.started &&
                           !found->second.finished;
        return dx::VmValue::Int(alive ? 1 : 0);
    });
    registry.Register("android.thread.set_priority",
                      [context](dx::IntrinsicContext& call) {
        const auto priority = call.arguments[0].AsInt();
        if (priority < 1 || priority > 10) {
            throw dx::VmJavaThrow{
                "Ljava/lang/IllegalArgumentException;",
                "Thread priority must be between 1 and 10"};
        }
        auto& state = context->java_threads[call.receiver.Value()];
        state.priority = priority;
        return dx::VmValue::Void();
    });
    registry.Register("android.url_encoder.encode",
                      [](dx::IntrinsicContext& call) {
        auto charset = call.vm.StringUtf8(call.arguments[1].ref);
        for (auto& byte : charset) {
            byte = static_cast<char>(std::toupper(
                static_cast<unsigned char>(byte)));
        }
        if (charset != "UTF-8" && charset != "UTF8") {
            throw dx::VmJavaThrow{
                "Ljava/io/UnsupportedEncodingException;", charset};
        }
        constexpr std::array<char, 16> kHex = {
            '0', '1', '2', '3', '4', '5', '6', '7',
            '8', '9', 'A', 'B', 'C', 'D', 'E', 'F'};
        std::string encoded;
        for (const auto byte :
             call.vm.StringUtf8(call.arguments[0].ref)) {
            const auto value = static_cast<unsigned char>(byte);
            const bool safe =
                (value >= 'a' && value <= 'z') ||
                (value >= 'A' && value <= 'Z') ||
                (value >= '0' && value <= '9') || value == '-' ||
                value == '_' || value == '.' || value == '*';
            if (safe) {
                encoded.push_back(static_cast<char>(value));
            } else if (value == ' ') {
                encoded.push_back('+');
            } else {
                encoded.push_back('%');
                encoded.push_back(kHex[value >> 4U]);
                encoded.push_back(kHex[value & 0x0FU]);
            }
        }
        return MakeString(call, encoded);
    });
}
[[nodiscard]] std::unordered_map<std::string,
                                 DexVmAndroidContext::PreferenceValue>&
PreferencesOf(dx::IntrinsicContext& call, const Context& context) {
    const auto found = context->preference_names.find(call.receiver.Value());
    if (found == context->preference_names.end()) {
        throw dx::VmJavaThrow{
            "Ljava/lang/IllegalStateException;",
            "SharedPreferences instance has no backing store"};
    }
    return context->preferences[found->second];
}

// Typed preference getter: absent keys answer the caller's default, a
// type mismatch throws the real ClassCastException.
template <typename ValueType>
[[nodiscard]] std::optional<ValueType> PreferenceValueOf(
    dx::IntrinsicContext& call, const Context& context,
    const std::string& key) {
    auto& store = PreferencesOf(call, context);
    const auto found = store.find(key);
    if (found == store.end()) return std::nullopt;
    const auto* value = std::get_if<ValueType>(&found->second);
    if (value == nullptr) {
        throw dx::VmJavaThrow{"Ljava/lang/ClassCastException;",
                              "preference has another type: " + key};
    }
    return *value;
}

void RegisterSharedPreferences(dx::IntrinsicRegistry& registry,
                               const Context& context) {
    registry.Register("android.context.get_shared_preferences",
                      [context](dx::IntrinsicContext& call) {
        const auto name = call.vm.StringUtf8(call.arguments[0].ref);
        const auto instance =
            Singleton(call, context, "prefs:" + name,
                      "Landroid/content/SharedPreferencesImpl;");
        context->preference_names[instance.Value()] = name;
        return dx::VmValue::Ref(instance);
    });
    registry.Register("android.prefs.edit",
                      [context](dx::IntrinsicContext& call) {
        const auto name =
            context->preference_names.at(call.receiver.Value());
        const auto editor =
            Singleton(call, context, "prefs_editor:" + name,
                      "Landroid/content/SharedPreferencesEditorImpl;");
        context->preference_names[editor.Value()] = name;
        return dx::VmValue::Ref(editor);
    });
    registry.Register("android.prefs.get_boolean",
                      [context](dx::IntrinsicContext& call) {
        const auto key = call.vm.StringUtf8(call.arguments[0].ref);
        const auto value = PreferenceValueOf<bool>(call, context, key);
        return dx::VmValue::Int(value.value_or(
            call.arguments[1].AsInt() != 0) ? 1 : 0);
    });
    registry.Register("android.prefs.get_int",
                      [context](dx::IntrinsicContext& call) {
        const auto key = call.vm.StringUtf8(call.arguments[0].ref);
        const auto value =
            PreferenceValueOf<std::int32_t>(call, context, key);
        return dx::VmValue::Int(
            value.value_or(call.arguments[1].AsInt()));
    });
    registry.Register("android.prefs.get_long",
                      [context](dx::IntrinsicContext& call) {
        const auto key = call.vm.StringUtf8(call.arguments[0].ref);
        const auto value =
            PreferenceValueOf<std::int64_t>(call, context, key);
        return dx::VmValue::Long(
            value.value_or(call.arguments[1].AsLong()));
    });
    registry.Register("android.prefs.get_string",
                      [context](dx::IntrinsicContext& call) {
        const auto key = call.vm.StringUtf8(call.arguments[0].ref);
        const auto value =
            PreferenceValueOf<std::string>(call, context, key);
        if (!value.has_value()) {
            return dx::VmValue::Ref(call.arguments[1].ref);
        }
        return MakeString(call, *value);
    });
    // v1 write semantics: edits apply immediately and commit() truthfully
    // reports the store accepted them (session lifetime, like
    // memory_files); no staged-rollback behaviour is claimed.
    registry.Register("android.prefs_editor.put_boolean",
                      [context](dx::IntrinsicContext& call) {
        PreferencesOf(call, context)
            [call.vm.StringUtf8(call.arguments[0].ref)] =
                call.arguments[1].AsInt() != 0;
        return Self(call);
    });
    registry.Register("android.prefs_editor.put_int",
                      [context](dx::IntrinsicContext& call) {
        PreferencesOf(call, context)
            [call.vm.StringUtf8(call.arguments[0].ref)] =
                call.arguments[1].AsInt();
        return Self(call);
    });
    registry.Register("android.prefs_editor.put_long",
                      [context](dx::IntrinsicContext& call) {
        PreferencesOf(call, context)
            [call.vm.StringUtf8(call.arguments[0].ref)] =
                call.arguments[1].AsLong();
        return Self(call);
    });
    registry.Register("android.prefs_editor.put_string",
                      [context](dx::IntrinsicContext& call) {
        PreferencesOf(call, context)
            [call.vm.StringUtf8(call.arguments[0].ref)] =
                call.vm.StringUtf8(call.arguments[1].ref);
        return Self(call);
    });
    registry.Register("android.prefs_editor.commit",
                      [](dx::IntrinsicContext&) {
        return dx::VmValue::Int(1);
    });
}

}  // namespace ogplay::runtime::android_intrinsics
