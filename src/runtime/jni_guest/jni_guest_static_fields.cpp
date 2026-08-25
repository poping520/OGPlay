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

[[nodiscard]] std::uint32_t Read32(
    memory::AddressSpace& address_space, const memory::GuestAddress address,
    const std::uint64_t thread_id) {
    std::array<std::byte, 4> bytes{};
    address_space.Read(address, bytes, thread_id);
    std::uint32_t value{};
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        value |= std::to_integer<std::uint32_t>(bytes[index])
                 << static_cast<unsigned>(index * 8U);
    }
    return value;
}

[[nodiscard]] std::uint64_t Read64(
    memory::AddressSpace& address_space, const memory::GuestAddress address,
    const std::uint64_t thread_id) {
    return static_cast<std::uint64_t>(Read32(address_space, address, thread_id)) |
           (static_cast<std::uint64_t>(
                Read32(address_space, address.Add(4U), thread_id))
            << 32U);
}

[[nodiscard]] std::string ReadCString(
    memory::AddressSpace& address_space, const memory::GuestAddress address,
    const std::uint64_t thread_id, const std::string_view field) {
    constexpr std::size_t kMaximumBytes = 1024;
    if (address.IsNull()) {
        throw JniGuestBindingError(
            "JNI guest " + std::string(field) + " pointer is null");
    }
    std::size_t length{};
    try {
        length = address_space.CStringLength(address, kMaximumBytes, thread_id);
    } catch (const std::length_error&) {
        throw JniGuestBindingError(
            "JNI guest " + std::string(field) + " is not null-terminated");
    }
    std::string result(length, '\0');
    address_space.Read(address, std::as_writable_bytes(std::span(result)),
                       thread_id);
    return result;
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
    const auto word = [](const std::uint32_t result) {
        return JniGuestCallResult{JniGuestReturnWidth::word, {result, 0U}};
    };
    const auto pair = [](const std::uint64_t result) {
        return JniGuestCallResult{
            JniGuestReturnWidth::double_word,
            {static_cast<std::uint32_t>(result),
             static_cast<std::uint32_t>(result >> 32U)}};
    };
    switch (kind) {
    case JniTypeKind::object:
    case JniTypeKind::array: return word(std::get<JniReference>(value).Value());
    case JniTypeKind::boolean: return word(std::get<JniBoolean>(value));
    case JniTypeKind::byte:
        return word(std::bit_cast<std::uint32_t>(
            static_cast<JniInt>(std::get<JniByte>(value))));
    case JniTypeKind::character: return word(std::get<JniChar>(value));
    case JniTypeKind::short_integer:
        return word(std::bit_cast<std::uint32_t>(
            static_cast<JniInt>(std::get<JniShort>(value))));
    case JniTypeKind::integer:
        return word(std::bit_cast<std::uint32_t>(std::get<JniInt>(value)));
    case JniTypeKind::long_integer:
        return pair(std::bit_cast<std::uint64_t>(std::get<JniLong>(value)));
    case JniTypeKind::float_value:
        return word(std::bit_cast<std::uint32_t>(std::get<JniFloat>(value)));
    case JniTypeKind::double_value:
        return pair(std::bit_cast<std::uint64_t>(std::get<JniDouble>(value)));
    case JniTypeKind::void_value: break;
    }
    throw JniGuestBindingError("JNI guest field type is invalid");
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
        return std::bit_cast<JniLong>(Read64(
            address_space, frame.stack_pointer, frame.thread_id));
    case JniTypeKind::float_value:
        return std::bit_cast<JniFloat>(frame.registers[3]);
    case JniTypeKind::double_value:
        return std::bit_cast<JniDouble>(Read64(
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

}  // namespace

void BindJniGuestStaticFieldSlots(JniGuestCallDispatcher& dispatcher,
                                  JniEnvironment& environment,
                                  JniClassRegistry& classes,
                                  JniFieldStore& fields,
                                  memory::AddressSpace& address_space) {
    dispatcher.BindEnvironment(
        EnvironmentSlot("GetStaticFieldID"),
        [&environment, &classes, &fields,
         &address_space](const JniGuestCallFrame& frame) {
            const auto java_class = ResolveClass(
                environment, frame, "GetStaticFieldID");
            if (!fields.EnsureClassInitialized(java_class, frame.thread_id)) {
                return JniGuestCallResult{JniGuestReturnWidth::word, {0U, 0U}};
            }
            const auto name = ReadCString(
                address_space, memory::GuestAddress{frame.registers[2]},
                frame.thread_id, "field name");
            const auto descriptor = ReadCString(
                address_space, memory::GuestAddress{frame.registers[3]},
                frame.thread_id, "field descriptor");
            const auto field = classes.GetFieldId(
                java_class, name, descriptor, true);
            if (!field.has_value()) {
                throw JniGuestBindingError(
                    "JNI guest static field is not declared: " + name +
                    descriptor);
            }
            return JniGuestCallResult{
                JniGuestReturnWidth::word, {field->Value(), 0U}};
        });
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
    dispatcher.BindEnvironment(
        EnvironmentSlot("GetFieldID"),
        [&environment, &classes, &fields,
         &address_space](const JniGuestCallFrame& frame) {
            const auto java_class = ResolveClass(
                environment, frame, "GetFieldID");
            if (!fields.EnsureClassInitialized(java_class, frame.thread_id)) {
                return JniGuestCallResult{JniGuestReturnWidth::word, {0U, 0U}};
            }
            const auto name = ReadCString(
                address_space, memory::GuestAddress{frame.registers[2]},
                frame.thread_id, "field name");
            const auto descriptor = ReadCString(
                address_space, memory::GuestAddress{frame.registers[3]},
                frame.thread_id, "field descriptor");
            const auto field = classes.GetFieldId(
                java_class, name, descriptor, false);
            if (!field.has_value()) {
                throw JniGuestBindingError(
                    "JNI guest instance field is not declared: " + name +
                    ":" + descriptor);
            }
            return JniGuestCallResult{
                JniGuestReturnWidth::word, {field->Value(), 0U}};
        });
    for (const auto type : kFieldTypes) {
        BindInstanceGetter(
            dispatcher, environment, classes, fields, objects, type);
        BindInstanceSetter(dispatcher, environment, classes, fields, objects,
                           address_space, type);
    }
}

}  // namespace ogplay::runtime
