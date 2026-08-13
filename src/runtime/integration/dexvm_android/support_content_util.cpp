// Remaining platform handlers: Intent/Bundle/Toast/date-time/identity
// and the non-goal surfaces (SMS, network, billing) that fail with
// accounting instead of pretending to succeed.

#include <chrono>

#include "shared.h"

namespace ogplay::runtime::android_intrinsics {

void PopulateMisc(AndroidHandlers& handlers, const Context& context) {
    handlers.handler_android_pair_init = dx::IntrinsicHandler([](dx::IntrinsicContext& call) {
        const auto slots = call.vm.Model().InstanceSlots(call.receiver);
        slots[0] = {call.arguments[0].ref.Value(), dx::SlotTag::ref};
        slots[1] = {call.arguments[1].ref.Value(), dx::SlotTag::ref};
        return dx::VmValue::Void();
    });
  handlers.handler_android_bundle_init = dx::IntrinsicHandler([context](dx::IntrinsicContext &call) {
                      context->bundles.try_emplace(call.receiver.Value());
        return dx::VmValue::Void();
    });
  handlers.handler_android_bundle_get = dx::IntrinsicHandler([context](dx::IntrinsicContext &call) {
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
  handlers.handler_android_bundle_get_int = dx::IntrinsicHandler([context](
                                                  dx::IntrinsicContext &call) {
    const auto &values = context->bundles[call.receiver.Value()];
    const auto found = values.find(call.vm.StringUtf8(call.arguments[0].ref));
    const auto *value = found == values.end()
                            ? nullptr
                            : std::get_if<std::int32_t>(&found->second);
    return dx::VmValue::Int(value == nullptr ? 0 : *value);
    });
  handlers.handler_android_bundle_get_string = dx::IntrinsicHandler([context](dx::IntrinsicContext &call) {
        const auto &values = context->bundles[call.receiver.Value()];
        const auto found =
            values.find(call.vm.StringUtf8(call.arguments[0].ref));
        const auto *value = found == values.end()
                                ? nullptr
                                : std::get_if<std::string>(&found->second);
        return value == nullptr ? dx::VmValue::Ref(dx::VmObjectRef{})
                                : MakeString(call, *value);
    });
  handlers.handler_android_bundle_put_string = dx::IntrinsicHandler([context](dx::IntrinsicContext &call) {
        context->bundles[call.receiver.Value()]
                        [call.vm.StringUtf8(call.arguments[0].ref)] =
            call.vm.StringUtf8(call.arguments[1].ref);
        return dx::VmValue::Void();
    });
  handlers.handler_android_bundle_put_int = dx::IntrinsicHandler([context](dx::IntrinsicContext &call) {
        context->bundles[call.receiver.Value()]
                        [call.vm.StringUtf8(call.arguments[0].ref)] =
            call.arguments[1].AsInt();
        return dx::VmValue::Void();
    });
  handlers.handler_android_bundle_get_long = dx::IntrinsicHandler([context](
                                                   dx::IntrinsicContext &call) {
    const auto &values = context->bundles[call.receiver.Value()];
    const auto found = values.find(call.vm.StringUtf8(call.arguments[0].ref));
    const auto *value = found == values.end()
                            ? nullptr
                            : std::get_if<std::int64_t>(&found->second);
    return dx::VmValue::Long(value == nullptr ? 0 : *value);
  });
  handlers.handler_android_bundle_put_long = dx::IntrinsicHandler([context](dx::IntrinsicContext &call) {
        context->bundles[call.receiver.Value()]
                        [call.vm.StringUtf8(call.arguments[0].ref)] =
            call.arguments[1].AsLong();
        return dx::VmValue::Void();
    });
  handlers.handler_android_bundle_get_byte_array = dx::IntrinsicHandler([context](dx::IntrinsicContext &call) {
        const auto &values = context->bundles[call.receiver.Value()];
        const auto found =
            values.find(call.vm.StringUtf8(call.arguments[0].ref));
        const auto *value = found == values.end()
                                ? nullptr
                                : std::get_if<dx::VmObjectRef>(&found->second);
        return dx::VmValue::Ref(value == nullptr ? dx::VmObjectRef{} : *value);
      });
  handlers.handler_android_bundle_put_byte_array = dx::IntrinsicHandler([context](dx::IntrinsicContext &call) {
        context->bundles[call.receiver.Value()]
                        [call.vm.StringUtf8(call.arguments[0].ref)] =
            call.arguments[1].ref;
        return dx::VmValue::Void();
    });
  handlers.handler_android_bundle_contains = dx::IntrinsicHandler([context](dx::IntrinsicContext &call) {
        const auto &values = context->bundles[call.receiver.Value()];
        return dx::VmValue::Int(
            values.contains(call.vm.StringUtf8(call.arguments[0].ref)) ? 1 : 0);
      });
  handlers.handler_android_bundle_clear = dx::IntrinsicHandler([context](dx::IntrinsicContext &call) {
                      context->bundles[call.receiver.Value()].clear();
        return dx::VmValue::Void();
    });
  handlers.handler_android_sax_factory_instance = dx::IntrinsicHandler([](dx::IntrinsicContext &call) {
                      return dx::VmValue::Ref(call.vm.NewIntrinsicInstance(
                          "Ljavax/xml/parsers/SAXParserFactory;"));
                    });
  handlers.handler_android_sax_new_parser = dx::IntrinsicHandler([](dx::IntrinsicContext &call) {
    return dx::VmValue::Ref(
        call.vm.NewIntrinsicInstance("Ljavax/xml/parsers/SAXParser;"));
  });
  handlers.handler_android_sax_get_reader = dx::IntrinsicHandler([](dx::IntrinsicContext &call) {
    return dx::VmValue::Ref(
        call.vm.NewIntrinsicInstance("Lorg/xml/sax/XMLReader$Impl;"));
  });
  handlers.handler_android_sax_set_content_handler = dx::IntrinsicHandler([context](dx::IntrinsicContext &call) {
        const auto handler = call.arguments[0].ref;
        if (!handler.IsValid()) {
          throw dx::VmJavaThrow{"Ljava/lang/NullPointerException;",
                                "SAX content handler is null"};
        }
        context->sax_content_handlers[call.receiver.Value()] = handler;
        return dx::VmValue::Void();
    });
  handlers.handler_android_sax_parse_unsupported = dx::IntrinsicHandler([](dx::IntrinsicContext &) -> dx::VmValue {
                      throw dx::VmJavaThrow{
                          "Ljava/lang/UnsupportedOperationException;",
                          "SAX parsing is not implemented"};
                    });
  handlers.handler_android_receiver_init = dx::IntrinsicHandler([](dx::IntrinsicContext &) { return dx::VmValue::Void(); });
  handlers.handler_android_receiver_on_receive_noop = dx::IntrinsicHandler([](dx::IntrinsicContext &) { return dx::VmValue::Void(); });
  handlers.handler_android_intent_filter_init = dx::IntrinsicHandler([](dx::IntrinsicContext &) { return dx::VmValue::Void(); });
  handlers.handler_android_intent_filter_init_empty = dx::IntrinsicHandler([](dx::IntrinsicContext &) { return dx::VmValue::Void(); });
  handlers.handler_android_intent_filter_add_action = dx::IntrinsicHandler([](dx::IntrinsicContext &) { return dx::VmValue::Void(); });
  handlers.handler_android_intent_init = dx::IntrinsicHandler([](dx::IntrinsicContext &) { return dx::VmValue::Void(); });
  handlers.handler_android_intent_init_component = dx::IntrinsicHandler([context](dx::IntrinsicContext &call) {
        const auto class_object = call.arguments[1].ref;
        const auto target = call.vm.Model().ClassOfClassObject(class_object);
        context->intent_components[call.receiver.Value()] =
            call.vm.Linker().Class(target).descriptor;
        return dx::VmValue::Void();
    });
    handlers.handler_android_intent_set_class_name = dx::IntrinsicHandler([context](dx::IntrinsicContext& call) {
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
  handlers.handler_android_intent_put_extra_int = dx::IntrinsicHandler([context](dx::IntrinsicContext &call) {
        context->intent_int_extras[call.receiver.Value()]
            [call.vm.StringUtf8(call.arguments[0].ref)] =
                call.arguments[1].AsInt();
        return Self(call);
    });
  handlers.handler_android_intent_put_extra_string = dx::IntrinsicHandler([context](dx::IntrinsicContext &call) {
        context->intent_string_extras[call.receiver.Value()][call.vm.StringUtf8(
            call.arguments[0].ref)] = call.vm.StringUtf8(call.arguments[1].ref);
        return Self(call);
    });
  handlers.handler_android_intent_get_string_extra = dx::IntrinsicHandler([context](dx::IntrinsicContext &call) {
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
  handlers.handler_android_intent_get_int_extra = dx::IntrinsicHandler([context](dx::IntrinsicContext &call) {
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
  handlers.handler_android_intent_get_action = dx::IntrinsicHandler([](dx::IntrinsicContext &) {
        return dx::VmValue::Ref(dx::VmObjectRef{});
    });
  handlers.handler_android_intent_get_extras = dx::IntrinsicHandler([](dx::IntrinsicContext &) {
        return dx::VmValue::Ref(dx::VmObjectRef{});
    });
    handlers.handler_android_intent_set_flags = dx::IntrinsicHandler([](dx::IntrinsicContext &call) { return Self(call); });
    handlers.handler_android_intent_set_data_and_type = dx::IntrinsicHandler([](dx::IntrinsicContext &call) { return Self(call); });
    handlers.handler_android_pending_intent_get_broadcast = dx::IntrinsicHandler([](dx::IntrinsicContext&) {
        return dx::VmValue::Ref(dx::VmObjectRef{});
    });
    handlers.handler_android_uri_parse = dx::IntrinsicHandler([](dx::IntrinsicContext& call) {
    return dx::VmValue::Ref(call.vm.NewIntrinsicInstance("Landroid/net/Uri;"));
    });
  handlers.handler_android_toast_make_text = dx::IntrinsicHandler([](dx::IntrinsicContext &call) {
        return dx::VmValue::Ref(
            call.vm.NewIntrinsicInstance("Landroid/widget/Toast;"));
    });
    handlers.handler_android_toast_show = dx::IntrinsicHandler([](dx::IntrinsicContext& call) {
        GuestLog(call, core::LogLevel::info, "Toast.show()");
        return dx::VmValue::Void();
    });
  handlers.handler_android_sms_get_default = dx::IntrinsicHandler([context](dx::IntrinsicContext &call) {
        return dx::VmValue::Ref(
            Singleton(call, context, "sms", "Landroid/telephony/SmsManager;"));
    });
    const auto unsupported_network =
        [](dx::IntrinsicContext&) -> dx::VmValue {
            throw dx::VmJavaThrow{
                "Ljava/lang/UnsupportedOperationException;",
                "SMS/network actions are outside the compatibility scope"};
        };
    handlers.handler_android_sms_send_text = dx::IntrinsicHandler(unsupported_network);
    handlers.handler_android_sms_create_from_pdu = dx::IntrinsicHandler(unsupported_network);
    handlers.handler_android_sms_get_message_body = dx::IntrinsicHandler(unsupported_network);
    handlers.handler_android_sms_get_originating_address = dx::IntrinsicHandler(unsupported_network);
    handlers.handler_android_net_unsupported = dx::IntrinsicHandler(unsupported_network);

    // Motion events read their slots directly.
  handlers.handler_android_motion_event_get_action = dx::IntrinsicHandler([](dx::IntrinsicContext &call) {
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
  handlers.handler_android_motion_event_get_x = dx::IntrinsicHandler([slot_float](dx::IntrinsicContext &call) { return slot_float(call, 1); });
  handlers.handler_android_motion_event_get_y = dx::IntrinsicHandler([slot_float](dx::IntrinsicContext &call) { return slot_float(call, 2); });
  handlers.handler_android_motion_event_get_x_indexed = dx::IntrinsicHandler([slot_float](dx::IntrinsicContext &call) { return slot_float(call, 1); });
  handlers.handler_android_motion_event_get_y_indexed = dx::IntrinsicHandler([slot_float](dx::IntrinsicContext &call) { return slot_float(call, 2); });
    handlers.handler_android_motion_event_get_pointer_count = dx::IntrinsicHandler([](dx::IntrinsicContext &) { return dx::VmValue::Int(1); });
  handlers.handler_android_motion_event_get_pointer_id = dx::IntrinsicHandler([](dx::IntrinsicContext &call) {
        return dx::VmValue::Int(static_cast<std::int32_t>(
            call.vm.Model().InstanceSlots(call.receiver)[3].bits));
    });

    // Platform System handlers (declared by the core catalog).
    handlers.handler_platform_system_current_time_millis = dx::IntrinsicHandler([context](dx::IntrinsicContext&) {
                      // Deterministic epoch base plus lifecycle-published
                      // uptime.
        return dx::VmValue::Long(1'400'000'000'000LL +
                                 context->uptime_millis.load());
    });
    // java.util.Date over the same deterministic platform clock.
  handlers.handler_platform_date_init = dx::IntrinsicHandler([context](dx::IntrinsicContext &call) {
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
    handlers.handler_platform_date_get_time = dx::IntrinsicHandler([date_millis](dx::IntrinsicContext& call) {
        return dx::VmValue::Long(date_millis(call));
    });
  handlers.handler_platform_date_get_year = dx::IntrinsicHandler([date_millis](dx::IntrinsicContext &call) {
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
  handlers.handler_platform_system_nano_time = dx::IntrinsicHandler([context](dx::IntrinsicContext &) {
        return dx::VmValue::Long(context->uptime_millis.load() * 1'000'000LL);
    });
  handlers.handler_platform_system_load_library = dx::IntrinsicHandler([](dx::IntrinsicContext &call) {
        // Libraries are preloaded and initialized by the session
        // (04 §2 step 4); the name is recorded for diagnostics.
        GuestLog(call, core::LogLevel::info,
                 "System.loadLibrary(" +
                     call.vm.StringUtf8(call.arguments[0].ref) + ")");
        return dx::VmValue::Void();
    });
  handlers.handler_platform_system_exit = dx::IntrinsicHandler([context](dx::IntrinsicContext &) {
        context->exit_requested = true;
        return dx::VmValue::Void();
    });
}

}  // namespace ogplay::runtime::android_intrinsics
