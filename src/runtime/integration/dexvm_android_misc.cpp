// Remaining platform handlers: Intent/Bundle/Toast/date-time/identity
// and the non-goal surfaces (SMS, network, billing) that fail with
// accounting instead of pretending to succeed.

#include <chrono>

#include "dexvm_android_internal.h"

namespace ogplay::runtime::android_intrinsics {

void RegisterMisc(dx::IntrinsicRegistry& registry, const Context& context) {
    registry.Register("android.pair.init", [](dx::IntrinsicContext& call) {
        const auto slots = call.vm.Model().InstanceSlots(call.receiver);
        slots[0] = {call.arguments[0].ref.Value(), dx::SlotTag::ref};
        slots[1] = {call.arguments[1].ref.Value(), dx::SlotTag::ref};
        return dx::VmValue::Void();
    });
  registry.Register("android.bundle.init",
                    [context](dx::IntrinsicContext &call) {
                      context->bundles.try_emplace(call.receiver.Value());
        return dx::VmValue::Void();
    });
  registry.Register(
      "android.bundle.get", [context](dx::IntrinsicContext &call) {
        const auto bundle = context->bundles.find(call.receiver.Value());
        if (bundle == context->bundles.end()) {
        return dx::VmValue::Ref(dx::VmObjectRef{});
        }
        const auto value =
            bundle->second.find(call.vm.StringUtf8(call.arguments[0].ref));
        if (value == bundle->second.end()) {
          return dx::VmValue::Ref(dx::VmObjectRef{});
        }
        if (const auto *text = std::get_if<std::string>(&value->second)) {
          return MakeString(call, *text);
        }
        if (const auto *object = std::get_if<dx::VmObjectRef>(&value->second)) {
          return dx::VmValue::Ref(*object);
        }
        return dx::VmValue::Ref(dx::VmObjectRef{});
    });
  registry.Register("android.bundle.get_int", [context](
                                                  dx::IntrinsicContext &call) {
    const auto &values = context->bundles[call.receiver.Value()];
    const auto found = values.find(call.vm.StringUtf8(call.arguments[0].ref));
    const auto *value = found == values.end()
                            ? nullptr
                            : std::get_if<std::int32_t>(&found->second);
    return dx::VmValue::Int(value == nullptr ? 0 : *value);
    });
  registry.Register(
      "android.bundle.get_string", [context](dx::IntrinsicContext &call) {
        const auto &values = context->bundles[call.receiver.Value()];
        const auto found =
            values.find(call.vm.StringUtf8(call.arguments[0].ref));
        const auto *value = found == values.end()
                                ? nullptr
                                : std::get_if<std::string>(&found->second);
        return value == nullptr ? dx::VmValue::Ref(dx::VmObjectRef{})
                                : MakeString(call, *value);
    });
  registry.Register(
      "android.bundle.put_string", [context](dx::IntrinsicContext &call) {
        context->bundles[call.receiver.Value()]
                        [call.vm.StringUtf8(call.arguments[0].ref)] =
            call.vm.StringUtf8(call.arguments[1].ref);
        return dx::VmValue::Void();
    });
  registry.Register(
      "android.bundle.put_int", [context](dx::IntrinsicContext &call) {
        context->bundles[call.receiver.Value()]
                        [call.vm.StringUtf8(call.arguments[0].ref)] =
            call.arguments[1].AsInt();
        return dx::VmValue::Void();
    });
  registry.Register("android.bundle.get_long", [context](
                                                   dx::IntrinsicContext &call) {
    const auto &values = context->bundles[call.receiver.Value()];
    const auto found = values.find(call.vm.StringUtf8(call.arguments[0].ref));
    const auto *value = found == values.end()
                            ? nullptr
                            : std::get_if<std::int64_t>(&found->second);
    return dx::VmValue::Long(value == nullptr ? 0 : *value);
  });
  registry.Register(
      "android.bundle.put_long", [context](dx::IntrinsicContext &call) {
        context->bundles[call.receiver.Value()]
                        [call.vm.StringUtf8(call.arguments[0].ref)] =
            call.arguments[1].AsLong();
        return dx::VmValue::Void();
    });
  registry.Register(
      "android.bundle.get_byte_array", [context](dx::IntrinsicContext &call) {
        const auto &values = context->bundles[call.receiver.Value()];
        const auto found =
            values.find(call.vm.StringUtf8(call.arguments[0].ref));
        const auto *value = found == values.end()
                                ? nullptr
                                : std::get_if<dx::VmObjectRef>(&found->second);
        return dx::VmValue::Ref(value == nullptr ? dx::VmObjectRef{} : *value);
      });
  registry.Register(
      "android.bundle.put_byte_array", [context](dx::IntrinsicContext &call) {
        context->bundles[call.receiver.Value()]
                        [call.vm.StringUtf8(call.arguments[0].ref)] =
            call.arguments[1].ref;
        return dx::VmValue::Void();
    });
  registry.Register(
      "android.bundle.contains", [context](dx::IntrinsicContext &call) {
        const auto &values = context->bundles[call.receiver.Value()];
        return dx::VmValue::Int(
            values.contains(call.vm.StringUtf8(call.arguments[0].ref)) ? 1 : 0);
      });
  registry.Register("android.bundle.clear",
                    [context](dx::IntrinsicContext &call) {
                      context->bundles[call.receiver.Value()].clear();
        return dx::VmValue::Void();
    });
  registry.Register("android.sax.factory_instance",
                    [](dx::IntrinsicContext &call) {
                      return dx::VmValue::Ref(call.vm.NewIntrinsicInstance(
                          "Ljavax/xml/parsers/SAXParserFactory;"));
                    });
  registry.Register("android.sax.new_parser", [](dx::IntrinsicContext &call) {
    return dx::VmValue::Ref(
        call.vm.NewIntrinsicInstance("Ljavax/xml/parsers/SAXParser;"));
  });
  registry.Register("android.sax.get_reader", [](dx::IntrinsicContext &call) {
    return dx::VmValue::Ref(
        call.vm.NewIntrinsicInstance("Lorg/xml/sax/XMLReader$Impl;"));
  });
  registry.Register(
      "android.sax.set_content_handler", [context](dx::IntrinsicContext &call) {
        const auto handler = call.arguments[0].ref;
        if (!handler.IsValid()) {
          throw dx::VmJavaThrow{"Ljava/lang/NullPointerException;",
                                "SAX content handler is null"};
        }
        context->sax_content_handlers[call.receiver.Value()] = handler;
        return dx::VmValue::Void();
    });
  registry.Register("android.sax.parse_unsupported",
                    [](dx::IntrinsicContext &) -> dx::VmValue {
                      throw dx::VmJavaThrow{
                          "Ljava/lang/UnsupportedOperationException;",
                          "SAX parsing is not implemented"};
                    });
  registry.Register("android.receiver.init",
                    [](dx::IntrinsicContext &) { return dx::VmValue::Void(); });
  registry.Register("android.receiver.on_receive_noop",
                    [](dx::IntrinsicContext &) { return dx::VmValue::Void(); });
  registry.Register("android.intent_filter.init",
                    [](dx::IntrinsicContext &) { return dx::VmValue::Void(); });
  registry.Register("android.intent_filter.init_empty",
                    [](dx::IntrinsicContext &) { return dx::VmValue::Void(); });
  registry.Register("android.intent_filter.add_action",
                    [](dx::IntrinsicContext &) { return dx::VmValue::Void(); });
  registry.Register("android.intent.init",
                    [](dx::IntrinsicContext &) { return dx::VmValue::Void(); });
  registry.Register(
      "android.intent.init_component", [context](dx::IntrinsicContext &call) {
        const auto class_object = call.arguments[1].ref;
        const auto target = call.vm.Model().ClassOfClassObject(class_object);
        context->intent_components[call.receiver.Value()] =
            call.vm.Linker().Class(target).descriptor;
        return dx::VmValue::Void();
    });
    registry.Register("android.intent.set_class_name",
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
  registry.Register(
      "android.intent.put_extra_int", [context](dx::IntrinsicContext &call) {
        context->intent_int_extras[call.receiver.Value()]
            [call.vm.StringUtf8(call.arguments[0].ref)] =
                call.arguments[1].AsInt();
        return Self(call);
    });
  registry.Register(
      "android.intent.put_extra_string", [context](dx::IntrinsicContext &call) {
        context->intent_string_extras[call.receiver.Value()][call.vm.StringUtf8(
            call.arguments[0].ref)] = call.vm.StringUtf8(call.arguments[1].ref);
        return Self(call);
    });
  registry.Register(
      "android.intent.get_string_extra", [context](dx::IntrinsicContext &call) {
        const auto extras =
            context->intent_string_extras.find(call.receiver.Value());
        if (extras != context->intent_string_extras.end()) {
          const auto found =
              extras->second.find(call.vm.StringUtf8(call.arguments[0].ref));
            if (found != extras->second.end()) {
                return MakeString(call, found->second);
            }
        }
        return dx::VmValue::Ref(dx::VmObjectRef{});
    });
  registry.Register(
      "android.intent.get_int_extra", [context](dx::IntrinsicContext &call) {
        const auto extras =
            context->intent_int_extras.find(call.receiver.Value());
        if (extras != context->intent_int_extras.end()) {
          const auto found =
              extras->second.find(call.vm.StringUtf8(call.arguments[0].ref));
            if (found != extras->second.end()) {
                return dx::VmValue::Int(found->second);
            }
        }
        return dx::VmValue::Int(call.arguments[1].AsInt());
    });
  registry.Register("android.intent.get_action", [](dx::IntrinsicContext &) {
        return dx::VmValue::Ref(dx::VmObjectRef{});
    });
  registry.Register("android.intent.get_extras", [](dx::IntrinsicContext &) {
        return dx::VmValue::Ref(dx::VmObjectRef{});
    });
    registry.Register("android.intent.set_flags",
                    [](dx::IntrinsicContext &call) { return Self(call); });
    registry.Register("android.intent.set_data_and_type",
                    [](dx::IntrinsicContext &call) { return Self(call); });
    registry.Register("android.pending_intent.get_broadcast",
                      [](dx::IntrinsicContext&) {
        return dx::VmValue::Ref(dx::VmObjectRef{});
    });
    registry.Register("android.uri.parse", [](dx::IntrinsicContext& call) {
    return dx::VmValue::Ref(call.vm.NewIntrinsicInstance("Landroid/net/Uri;"));
    });
  registry.Register("android.toast.make_text", [](dx::IntrinsicContext &call) {
        return dx::VmValue::Ref(
            call.vm.NewIntrinsicInstance("Landroid/widget/Toast;"));
    });
    registry.Register("android.toast.show", [](dx::IntrinsicContext& call) {
        GuestLog(call, core::LogLevel::info, "Toast.show()");
        return dx::VmValue::Void();
    });
  registry.Register(
      "android.sms.get_default", [context](dx::IntrinsicContext &call) {
        return dx::VmValue::Ref(
            Singleton(call, context, "sms", "Landroid/telephony/SmsManager;"));
    });
    for (const auto* blocked :
         {"android.sms.send_text", "android.sms.create_from_pdu",
        "android.sms.get_message_body", "android.sms.get_originating_address",
          "android.net.unsupported"}) {
        registry.Register(blocked, [](dx::IntrinsicContext&) -> dx::VmValue {
            throw dx::VmJavaThrow{
                "Ljava/lang/UnsupportedOperationException;",
                "SMS/network actions are outside the compatibility scope"};
        });
    }

    // Motion events read their slots directly.
  registry.Register(
      "android.motion_event.get_action", [](dx::IntrinsicContext &call) {
        return dx::VmValue::Int(static_cast<std::int32_t>(
            call.vm.Model().InstanceSlots(call.receiver)[0].bits));
    });
    const auto slot_float = [](dx::IntrinsicContext& call,
                               const std::size_t slot) {
        dx::VmValue value;
        value.kind = dx::VmValue::Kind::cat1;
        value.cat1 = call.vm.Model().InstanceSlots(call.receiver)[slot].bits;
        return value;
    };
  registry.Register(
      "android.motion_event.get_x",
      [slot_float](dx::IntrinsicContext &call) { return slot_float(call, 1); });
  registry.Register(
      "android.motion_event.get_y",
      [slot_float](dx::IntrinsicContext &call) { return slot_float(call, 2); });
  registry.Register(
      "android.motion_event.get_x_indexed",
      [slot_float](dx::IntrinsicContext &call) { return slot_float(call, 1); });
  registry.Register(
      "android.motion_event.get_y_indexed",
      [slot_float](dx::IntrinsicContext &call) { return slot_float(call, 2); });
    registry.Register("android.motion_event.get_pointer_count",
                    [](dx::IntrinsicContext &) { return dx::VmValue::Int(1); });
  registry.Register(
      "android.motion_event.get_pointer_id", [](dx::IntrinsicContext &call) {
        return dx::VmValue::Int(static_cast<std::int32_t>(
            call.vm.Model().InstanceSlots(call.receiver)[3].bits));
    });

    // Platform System handlers (declared by the core catalog).
    registry.Register("platform.system.current_time_millis",
                      [context](dx::IntrinsicContext&) {
                      // Deterministic epoch base plus lifecycle-published
                      // uptime.
        return dx::VmValue::Long(1'400'000'000'000LL +
                                 context->uptime_millis.load());
    });
    // java.util.Date over the same deterministic platform clock.
  registry.Register(
      "platform.date.init", [context](dx::IntrinsicContext &call) {
        const auto millis = 1'400'000'000'000LL + context->uptime_millis.load();
        const auto millis_bits = static_cast<std::uint64_t>(millis);
        const auto slots = call.vm.Model().InstanceSlots(call.receiver);
        slots[0] = {static_cast<std::uint32_t>(millis_bits & 0xffffffffULL),
                    dx::SlotTag::wide_lo};
        slots[1] = {static_cast<std::uint32_t>(millis_bits >> 32U),
                    dx::SlotTag::wide_hi};
        return dx::VmValue::Void();
    });
    const auto date_millis = [](dx::IntrinsicContext& call) {
        const auto slots = call.vm.Model().InstanceSlots(call.receiver);
        return static_cast<std::int64_t>(
        (static_cast<std::uint64_t>(slots[1].bits) << 32U) | slots[0].bits);
    };
    registry.Register("platform.date.get_time",
                      [date_millis](dx::IntrinsicContext& call) {
        return dx::VmValue::Long(date_millis(call));
    });
  registry.Register(
      "platform.date.get_year", [date_millis](dx::IntrinsicContext &call) {
        using days = std::chrono::days;
        const auto time_point =
            std::chrono::sys_days(std::chrono::January / 1 / 1970) +
            std::chrono::milliseconds(date_millis(call));
        const std::chrono::year_month_day date(
            std::chrono::floor<days>(time_point));
        // Date.getYear is 1900-based.
        return dx::VmValue::Int(
            static_cast<std::int32_t>(static_cast<int>(date.year())) - 1900);
    });
  registry.Register(
      "platform.system.nano_time", [context](dx::IntrinsicContext &) {
        return dx::VmValue::Long(context->uptime_millis.load() * 1'000'000LL);
    });
  registry.Register(
      "platform.system.load_library", [](dx::IntrinsicContext &call) {
        // Libraries are preloaded and initialized by the session
        // (04 §2 step 4); the name is recorded for diagnostics.
        GuestLog(call, core::LogLevel::info,
                 "System.loadLibrary(" +
                     call.vm.StringUtf8(call.arguments[0].ref) + ")");
        return dx::VmValue::Void();
    });
  registry.Register("platform.system.exit", [context](dx::IntrinsicContext &) {
        context->exit_requested = true;
        return dx::VmValue::Void();
    });
}

}  // namespace ogplay::runtime::android_intrinsics
