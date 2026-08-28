#include "ogplay/runtime/jni_guest/jni_guest_static_fields.h"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include "ogplay/memory/address_space.h"
#include "ogplay/runtime/jni_guest/jni_guest_bindings.h"
#include "ogplay/runtime/jni_guest/jni_guest_static_calls.h"
#include "ogplay/runtime/jni/jni_class_registry.h"
#include "ogplay/runtime/jni/jni_environment.h"
#include "ogplay/runtime/jni/jni_field_store.h"
#include "ogplay/runtime/jni/jni_invocation.h"
#include "jni_guest_memory.h"
#include "jni_guest_value_codec.h"

namespace ogplay::runtime {
namespace {

struct FieldType final {
    std::string_view suffix;
    JniTypeKind kind;
};

constexpr std::array kFieldTypes{
    FieldType{"Object", JniTypeKind::object},
    FieldType{"Boolean", JniTypeKind::boolean},
    FieldType{"Byte", JniTypeKind::byte},
    FieldType{"Char", JniTypeKind::character},
    FieldType{"Short", JniTypeKind::short_integer},
    FieldType{"Int", JniTypeKind::integer},
    FieldType{"Long", JniTypeKind::long_integer},
    FieldType{"Float", JniTypeKind::float_value},
    FieldType{"Double", JniTypeKind::double_value},
};

[[nodiscard]] JniSlot EnvironmentSlot(const std::string_view name) {
    const auto slot = FindJniSlot(name);
    if (!slot.has_value()) {
        throw std::logic_error("required JNI guest slot is absent");
    }
    return *slot;
}

[[nodiscard]] bool TypeMatches(const JniTypeKind actual,
                               const JniTypeKind requested) {
    return requested == JniTypeKind::object
               ? actual == JniTypeKind::object || actual == JniTypeKind::array
               : actual == requested;
}

[[nodiscard]] JniObjectIdentity ResolveClass(
    JniEnvironment& environment, const JniGuestCallFrame& frame,
    const std::string_view slot) {
    const auto java_class = environment.ResolveObjectForHle(
        frame.thread_id, JniReference{frame.registers[1]});
    if (!java_class.has_value()) {
        throw JniGuestBindingError(
            std::string(slot) + " requires a valid class reference");
    }
    return *java_class;
}

[[nodiscard]] JniResolvedField ResolveTypedField(
    JniClassRegistry& classes, const JniGuestCallFrame& frame,
    const FieldType type, const std::string_view slot) {
    const auto field = classes.ResolveField(JniFieldId{frame.registers[2]});
    if (!TypeMatches(field.type.kind, type.kind)) {
        throw JniGuestBindingError(
            std::string(slot) + " type does not match field descriptor");
    }
    return field;
}

[[nodiscard]] JniGuestCallResult Encode(const JniValue& value,
                                         const JniTypeKind kind) {
    return jni_guest_detail::EncodeValueResult(
        value, kind, jni_guest_detail::VoidResultPolicy::reject,
        "JNI guest field type is invalid");
}

[[nodiscard]] JniValue Decode(const JniGuestCallFrame& frame,
                              memory::AddressSpace& address_space,
                              const JniTypeKind kind) {
    switch (kind) {
    case JniTypeKind::object:
    case JniTypeKind::array: return JniReference{frame.registers[3]};
    case JniTypeKind::boolean:
        return static_cast<JniBoolean>(frame.registers[3] != 0U);
    case JniTypeKind::byte: return static_cast<JniByte>(frame.registers[3]);
    case JniTypeKind::character:
        return static_cast<JniChar>(frame.registers[3]);
    case JniTypeKind::short_integer:
        return static_cast<JniShort>(frame.registers[3]);
    case JniTypeKind::integer:
        return std::bit_cast<JniInt>(frame.registers[3]);
    case JniTypeKind::long_integer:
        return std::bit_cast<JniLong>(ReadGuest64(
            address_space, frame.stack_pointer, frame.thread_id));
    case JniTypeKind::float_value:
        return std::bit_cast<JniFloat>(frame.registers[3]);
    case JniTypeKind::double_value:
        return std::bit_cast<JniDouble>(ReadGuest64(
            address_space, frame.stack_pointer, frame.thread_id));
    case JniTypeKind::void_value: break;
    }
    throw JniGuestBindingError("JNI guest field type is invalid");
}

void BindGetter(JniGuestCallDispatcher& dispatcher,
                JniEnvironment& environment, JniClassRegistry& classes,
                JniFieldStore& fields, const FieldType type) {
    const auto name = std::string("GetStatic") + std::string(type.suffix) +
                      "Field";
    dispatcher.BindEnvironment(
        EnvironmentSlot(name),
        [&environment, &classes, &fields, type,
         name](const JniGuestCallFrame& frame) {
            const auto java_class = ResolveClass(environment, frame, name);
            const auto field = ResolveTypedField(classes, frame, type, name);
            return Encode(fields.GetStatic(java_class, field.id,
                                           frame.thread_id),
                          field.type.kind);
        });
}

void BindSetter(JniGuestCallDispatcher& dispatcher,
                JniEnvironment& environment, JniClassRegistry& classes,
                JniFieldStore& fields, memory::AddressSpace& address_space,
                const FieldType type) {
    const auto name = std::string("SetStatic") + std::string(type.suffix) +
                      "Field";
    dispatcher.BindEnvironment(
        EnvironmentSlot(name),
        [&environment, &classes, &fields, &address_space, type,
         name](const JniGuestCallFrame& frame) {
            const auto java_class = ResolveClass(environment, frame, name);
            const auto field = ResolveTypedField(classes, frame, type, name);
            fields.SetStatic(java_class, field.id,
                             Decode(frame, address_space, field.type.kind),
                             frame.thread_id);
            return JniGuestCallResult{};
        });
}

[[nodiscard]] std::pair<JniObjectIdentity, JniObjectIdentity>
ResolveReceiver(JniEnvironment& environment, JniGuestObjectRegistry& objects,
                const JniGuestCallFrame& frame,
                const std::string_view slot) {
    const auto object = environment.ResolveObjectForHle(
        frame.thread_id, JniReference{frame.registers[1]});
    if (!object.has_value()) {
        throw JniGuestBindingError(
            std::string(slot) + " requires a valid object reference");
    }
    return {*object, objects.ClassOf(*object)};
}

void BindInstanceGetter(
    JniGuestCallDispatcher& dispatcher, JniEnvironment& environment,
    JniClassRegistry& classes, JniFieldStore& fields,
    JniGuestObjectRegistry& objects, const FieldType type) {
    const auto name =
        std::string("Get") + std::string(type.suffix) + "Field";
    dispatcher.BindEnvironment(
        EnvironmentSlot(name),
        [&environment, &classes, &fields, &objects, type,
         name](const JniGuestCallFrame& frame) {
            const auto [object, receiver_class] = ResolveReceiver(
                environment, objects, frame, name);
            const auto field = ResolveTypedField(classes, frame, type, name);
            return Encode(fields.GetInstance(
                              object, receiver_class, field.id,
                              frame.thread_id),
                          field.type.kind);
        });
}

void BindInstanceSetter(
    JniGuestCallDispatcher& dispatcher, JniEnvironment& environment,
    JniClassRegistry& classes, JniFieldStore& fields,
    JniGuestObjectRegistry& objects, memory::AddressSpace& address_space,
    const FieldType type) {
    const auto name =
        std::string("Set") + std::string(type.suffix) + "Field";
    dispatcher.BindEnvironment(
        EnvironmentSlot(name),
        [&environment, &classes, &fields, &objects, &address_space, type,
         name](const JniGuestCallFrame& frame) {
            const auto [object, receiver_class] = ResolveReceiver(
                environment, objects, frame, name);
            const auto field = ResolveTypedField(classes, frame, type, name);
            fields.SetInstance(
                object, receiver_class, field.id,
                Decode(frame, address_space, field.type.kind),
                frame.thread_id);
            return JniGuestCallResult{};
        });
}

void BindFieldIdLookup(JniGuestCallDispatcher& dispatcher,
                       JniEnvironment& environment,
                       JniClassRegistry& classes, JniFieldStore& fields,
                       memory::AddressSpace& address_space,
                       const std::string_view slot, const bool is_static,
                       const std::string_view missing_prefix,
                       const std::string_view name_descriptor_separator) {
    dispatcher.BindEnvironment(
        EnvironmentSlot(slot),
        [&environment, &classes, &fields, &address_space,
         slot = std::string(slot), is_static,
         missing_prefix = std::string(missing_prefix),
         separator = std::string(name_descriptor_separator)](
            const JniGuestCallFrame& frame) {
            const auto java_class = ResolveClass(environment, frame, slot);
            if (!fields.EnsureClassInitialized(java_class, frame.thread_id)) {
                return JniGuestCallResult{JniGuestReturnWidth::word, {0U, 0U}};
            }
            const auto name = ReadGuestCString(
                address_space, memory::GuestAddress{frame.registers[2]},
                frame.thread_id, "field name");
            const auto descriptor = ReadGuestCString(
                address_space, memory::GuestAddress{frame.registers[3]},
                frame.thread_id, "field descriptor");
            const auto field = classes.GetFieldId(
                java_class, name, descriptor, is_static);
            if (!field.has_value()) {
                throw JniGuestBindingError(
                    missing_prefix + name + separator + descriptor);
            }
            return JniGuestCallResult{
                JniGuestReturnWidth::word, {field->Value(), 0U}};
        });
}

}  // namespace

void BindJniGuestStaticFieldSlots(JniGuestCallDispatcher& dispatcher,
                                  JniEnvironment& environment,
                                  JniClassRegistry& classes,
                                  JniFieldStore& fields,
                                  memory::AddressSpace& address_space) {
    BindFieldIdLookup(dispatcher, environment, classes, fields, address_space,
                      "GetStaticFieldID", true,
                      "JNI guest static field is not declared: ", "");
    for (const auto type : kFieldTypes) {
        BindGetter(dispatcher, environment, classes, fields, type);
        BindSetter(dispatcher, environment, classes, fields, address_space,
                   type);
    }
}

void BindJniGuestInstanceFieldSlots(
    JniGuestCallDispatcher& dispatcher, JniEnvironment& environment,
    JniClassRegistry& classes, JniFieldStore& fields,
    JniGuestObjectRegistry& objects, memory::AddressSpace& address_space) {
    BindFieldIdLookup(dispatcher, environment, classes, fields, address_space,
                      "GetFieldID", false,
                      "JNI guest instance field is not declared: ", ":");
    for (const auto type : kFieldTypes) {
        BindInstanceGetter(
            dispatcher, environment, classes, fields, objects, type);
        BindInstanceSetter(dispatcher, environment, classes, fields, objects,
                           address_space, type);
    }
}

}  // namespace ogplay::runtime
