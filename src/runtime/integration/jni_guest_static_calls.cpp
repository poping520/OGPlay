#include "ogplay/runtime/integration/jni_guest_static_calls.h"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "ogplay/memory/address_space.h"
#include "ogplay/runtime/integration/jni_guest_bindings.h"
#include "ogplay/runtime/jni/jni_class_registry.h"
#include "ogplay/runtime/jni/jni_environment.h"
#include "ogplay/runtime/jni/jni_invocation.h"
#include "ogplay/runtime/jni/jni_object.h"

namespace ogplay::runtime {

class JniGuestObjectRegistry::Impl final {
public:
    explicit Impl(const JniClassRegistry& classes) : classes_(&classes) {}

    [[nodiscard]] JniObjectIdentity Allocate(
        const JniObjectIdentity java_class) {
        const auto object = AllocateJniHostObjectIdentity();
        Register(object, java_class);
        return object;
    }

    void Register(const JniObjectIdentity object,
                  const JniObjectIdentity java_class) {
        Validate(object, java_class);
        std::scoped_lock lock(mutex_);
        if (!objects_.emplace(object.value, java_class).second) {
            throw JniGuestBindingError(
                "JNI guest object is already registered");
        }
    }

    void Forget(const JniObjectIdentity object) {
        if (object.domain != JniObjectDomain::host || object.value == 0U) {
            throw JniGuestBindingError(
                "JNI guest object identity is invalid");
        }
        std::scoped_lock lock(mutex_);
        if (objects_.erase(object.value) == 0U) {
            throw JniGuestBindingError(
                "JNI guest object is not registered");
        }
    }

    [[nodiscard]] JniObjectIdentity ClassOf(
        const JniObjectIdentity object) const {
        std::scoped_lock lock(mutex_);
        const auto found = objects_.find(object.value);
        if (object.domain != JniObjectDomain::host || found == objects_.end()) {
            throw JniGuestBindingError(
                "JNI guest receiver is not a registered instance");
        }
        return found->second;
    }

private:
    void Validate(const JniObjectIdentity object,
                  const JniObjectIdentity java_class) const {
        if (object.domain != JniObjectDomain::host || object.value == 0U ||
            java_class.value == 0U) {
            throw JniGuestBindingError(
                "JNI guest object registration is invalid");
        }
        try {
            if (!classes_->IsAssignableFrom(java_class, java_class)) {
                throw JniGuestBindingError(
                    "JNI guest object class is not registered");
            }
        } catch (const JniClassRegistryError&) {
            throw JniGuestBindingError(
                "JNI guest object class is not registered");
        }
    }

    const JniClassRegistry* classes_{};
    mutable std::mutex mutex_;
    std::map<std::uint64_t, JniObjectIdentity> objects_;
};

JniGuestObjectRegistry::JniGuestObjectRegistry(
    const JniClassRegistry& classes)
    : impl_(std::make_unique<Impl>(classes)) {}
JniGuestObjectRegistry::~JniGuestObjectRegistry() = default;
JniGuestObjectRegistry::JniGuestObjectRegistry(
    JniGuestObjectRegistry&&) noexcept = default;
JniGuestObjectRegistry& JniGuestObjectRegistry::operator=(
    JniGuestObjectRegistry&&) noexcept = default;
JniObjectIdentity JniGuestObjectRegistry::Allocate(
    const JniObjectIdentity java_class) {
    return impl_->Allocate(java_class);
}
void JniGuestObjectRegistry::Register(const JniObjectIdentity object,
                                      const JniObjectIdentity java_class) {
    impl_->Register(object, java_class);
}
void JniGuestObjectRegistry::Forget(const JniObjectIdentity object) {
    impl_->Forget(object);
}
JniObjectIdentity JniGuestObjectRegistry::ClassOf(
    const JniObjectIdentity object) const {
    return impl_->ClassOf(object);
}

namespace {

struct StaticCallType final {
    std::string_view suffix;
    JniTypeKind result;
};

constexpr std::array kStaticCallTypes{
    StaticCallType{"Object", JniTypeKind::object},
    StaticCallType{"Boolean", JniTypeKind::boolean},
    StaticCallType{"Byte", JniTypeKind::byte},
    StaticCallType{"Char", JniTypeKind::character},
    StaticCallType{"Short", JniTypeKind::short_integer},
    StaticCallType{"Int", JniTypeKind::integer},
    StaticCallType{"Long", JniTypeKind::long_integer},
    StaticCallType{"Float", JniTypeKind::float_value},
    StaticCallType{"Double", JniTypeKind::double_value},
    StaticCallType{"Void", JniTypeKind::void_value},
};

[[nodiscard]] JniSlot EnvironmentSlot(const std::string_view name) {
    const auto slot = FindJniSlot(name);
    if (!slot.has_value()) {
        throw std::logic_error("required JNI guest slot is absent");
    }
    return *slot;
}

[[nodiscard]] std::uint8_t Read8(memory::AddressSpace& address_space,
                                 memory::GuestAddress address,
                                 std::uint64_t thread_id);

[[nodiscard]] std::string ReadCString(
    memory::AddressSpace& address_space,
    const memory::GuestAddress address, const std::uint64_t thread_id,
    const std::string_view field) {
    constexpr std::size_t kMaximumBytes = 1024;
    if (address.IsNull()) {
        throw JniGuestBindingError(
            "JNI guest " + std::string(field) + " pointer is null");
    }
    std::string result;
    for (std::size_t index = 0; index < kMaximumBytes; ++index) {
        const auto value = Read8(address_space, address.Add(index), thread_id);
        if (value == 0U) return result;
        result.push_back(static_cast<char>(value));
    }
    throw JniGuestBindingError(
        "JNI guest " + std::string(field) + " is not null-terminated");
}

[[nodiscard]] std::uint8_t Read8(memory::AddressSpace& address_space,
                                 const memory::GuestAddress address,
                                 const std::uint64_t thread_id) {
    std::byte byte{};
    address_space.Read(address, std::span{&byte, 1}, thread_id);
    return std::to_integer<std::uint8_t>(byte);
}

[[nodiscard]] std::uint16_t Read16(memory::AddressSpace& address_space,
                                   const memory::GuestAddress address,
                                   const std::uint64_t thread_id) {
    std::array<std::byte, 2> bytes{};
    address_space.Read(address, bytes, thread_id);
    return std::to_integer<std::uint16_t>(bytes[0]) |
           static_cast<std::uint16_t>(
               std::to_integer<std::uint16_t>(bytes[1]) << 8U);
}

[[nodiscard]] std::uint32_t Read32(memory::AddressSpace& address_space,
                                   const memory::GuestAddress address,
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

[[nodiscard]] std::uint64_t Read64(memory::AddressSpace& address_space,
                                   const memory::GuestAddress address,
                                   const std::uint64_t thread_id) {
    return static_cast<std::uint64_t>(Read32(address_space, address, thread_id)) |
           (static_cast<std::uint64_t>(
                Read32(address_space, address.Add(4U), thread_id))
            << 32U);
}

[[nodiscard]] memory::GuestAddress Align8(memory::GuestAddress address) {
    return address.AlignUp(8U);
}

[[nodiscard]] JniValue WordValue(const JniTypeDescriptor& parameter,
                                 const std::uint32_t word) {
    switch (parameter.kind) {
    case JniTypeKind::boolean:
        return static_cast<JniBoolean>(word != 0U);
    case JniTypeKind::byte: return static_cast<JniByte>(word);
    case JniTypeKind::character: return static_cast<JniChar>(word);
    case JniTypeKind::short_integer: return static_cast<JniShort>(word);
    case JniTypeKind::integer: return std::bit_cast<JniInt>(word);
    case JniTypeKind::object:
    case JniTypeKind::array: return JniReference{word};
    default:
        throw JniGuestBindingError(
            "JNI guest argument does not have a word representation");
    }
}

[[nodiscard]] std::vector<JniValue> ReadVariadicArguments(
    memory::AddressSpace& address_space, const JniGuestCallFrame& frame,
    const JniMethodDescriptor& descriptor) {
    std::size_t register_index = 3U;
    std::uint32_t stack_offset{};
    const auto word = [&]() {
        if (register_index < frame.registers.size()) {
            return frame.registers[register_index++];
        }
        const auto result = Read32(address_space,
                                   frame.stack_pointer.Add(stack_offset),
                                   frame.thread_id);
        stack_offset += sizeof(std::uint32_t);
        return result;
    };
    const auto double_word = [&]() {
        if (register_index < frame.registers.size()) {
            register_index = (register_index + 1U) & ~std::size_t{1U};
        } else {
            stack_offset = (stack_offset + 7U) & ~std::uint32_t{7U};
        }
        const auto low = word();
        const auto high = word();
        return static_cast<std::uint64_t>(low) |
               (static_cast<std::uint64_t>(high) << 32U);
    };

    std::vector<JniValue> arguments;
    arguments.reserve(descriptor.parameters.size());
    for (const auto& parameter : descriptor.parameters) {
        switch (parameter.kind) {
        case JniTypeKind::long_integer:
            arguments.emplace_back(std::bit_cast<JniLong>(double_word()));
            break;
        case JniTypeKind::float_value:
            arguments.emplace_back(static_cast<JniFloat>(
                std::bit_cast<JniDouble>(double_word())));
            break;
        case JniTypeKind::double_value:
            arguments.emplace_back(std::bit_cast<JniDouble>(double_word()));
            break;
        case JniTypeKind::void_value:
            throw JniGuestBindingError(
                "JNI variadic parameter cannot have void type");
        default: arguments.emplace_back(WordValue(parameter, word())); break;
        }
    }
    return arguments;
}

[[nodiscard]] std::vector<JniValue> ReadVaListArguments(
    memory::AddressSpace& address_space, const JniGuestCallFrame& frame,
    const JniMethodDescriptor& descriptor) {
    auto cursor = memory::GuestAddress{frame.registers[3]};
    if (cursor.IsNull() && !descriptor.parameters.empty()) {
        throw JniGuestBindingError("JNI guest va_list pointer is null");
    }
    std::vector<JniValue> arguments;
    arguments.reserve(descriptor.parameters.size());
    for (const auto& parameter : descriptor.parameters) {
        switch (parameter.kind) {
        case JniTypeKind::long_integer:
            cursor = Align8(cursor);
            arguments.emplace_back(std::bit_cast<JniLong>(
                Read64(address_space, cursor, frame.thread_id)));
            cursor = cursor.Add(8U);
            break;
        case JniTypeKind::float_value:
            cursor = Align8(cursor);
            arguments.emplace_back(static_cast<JniFloat>(
                std::bit_cast<JniDouble>(
                    Read64(address_space, cursor, frame.thread_id))));
            cursor = cursor.Add(8U);
            break;
        case JniTypeKind::double_value:
            cursor = Align8(cursor);
            arguments.emplace_back(std::bit_cast<JniDouble>(
                Read64(address_space, cursor, frame.thread_id)));
            cursor = cursor.Add(8U);
            break;
        case JniTypeKind::void_value:
            throw JniGuestBindingError(
                "JNI va_list parameter cannot have void type");
        default:
            arguments.emplace_back(WordValue(
                parameter, Read32(address_space, cursor, frame.thread_id)));
            cursor = cursor.Add(4U);
            break;
        }
    }
    return arguments;
}

[[nodiscard]] JniValue ReadArrayValue(
    memory::AddressSpace& address_space, const memory::GuestAddress address,
    const std::uint64_t thread_id, const JniTypeDescriptor& parameter) {
    switch (parameter.kind) {
    case JniTypeKind::boolean:
        return static_cast<JniBoolean>(Read8(address_space, address, thread_id));
    case JniTypeKind::byte:
        return std::bit_cast<JniByte>(Read8(address_space, address, thread_id));
    case JniTypeKind::character:
        return static_cast<JniChar>(Read16(address_space, address, thread_id));
    case JniTypeKind::short_integer:
        return std::bit_cast<JniShort>(Read16(address_space, address, thread_id));
    case JniTypeKind::integer:
        return std::bit_cast<JniInt>(Read32(address_space, address, thread_id));
    case JniTypeKind::long_integer:
        return std::bit_cast<JniLong>(Read64(address_space, address, thread_id));
    case JniTypeKind::float_value:
        return std::bit_cast<JniFloat>(Read32(address_space, address, thread_id));
    case JniTypeKind::double_value:
        return std::bit_cast<JniDouble>(Read64(address_space, address, thread_id));
    case JniTypeKind::object:
    case JniTypeKind::array:
        return JniReference{Read32(address_space, address, thread_id)};
    case JniTypeKind::void_value:
        throw JniGuestBindingError(
            "JNI value-array parameter cannot have void type");
    }
    throw JniGuestBindingError("JNI guest value-array type is invalid");
}

[[nodiscard]] std::vector<JniValue> ReadValueArrayArguments(
    memory::AddressSpace& address_space, const JniGuestCallFrame& frame,
    const JniMethodDescriptor& descriptor) {
    const auto values = memory::GuestAddress{frame.registers[3]};
    if (values.IsNull() && !descriptor.parameters.empty()) {
        throw JniGuestBindingError("JNI guest jvalue array pointer is null");
    }
    std::vector<JniValue> arguments;
    arguments.reserve(descriptor.parameters.size());
    for (std::size_t index = 0; index < descriptor.parameters.size(); ++index) {
        arguments.emplace_back(ReadArrayValue(
            address_space, values.Add(index * 8U), frame.thread_id,
            descriptor.parameters[index]));
    }
    return arguments;
}

[[nodiscard]] JniGuestCallResult EncodeResult(const JniValue& result,
                                               const JniTypeKind kind) {
    const auto word = [](const std::uint32_t value) {
        return JniGuestCallResult{JniGuestReturnWidth::word, {value, 0U}};
    };
    const auto pair = [](const std::uint64_t value) {
        return JniGuestCallResult{
            JniGuestReturnWidth::double_word,
            {static_cast<std::uint32_t>(value),
             static_cast<std::uint32_t>(value >> 32U)}};
    };
    switch (kind) {
    case JniTypeKind::object:
    case JniTypeKind::array: return word(std::get<JniReference>(result).Value());
    case JniTypeKind::boolean: return word(std::get<JniBoolean>(result));
    case JniTypeKind::byte:
        return word(std::bit_cast<std::uint32_t>(
            static_cast<JniInt>(std::get<JniByte>(result))));
    case JniTypeKind::character: return word(std::get<JniChar>(result));
    case JniTypeKind::short_integer:
        return word(std::bit_cast<std::uint32_t>(
            static_cast<JniInt>(std::get<JniShort>(result))));
    case JniTypeKind::integer:
        return word(std::bit_cast<std::uint32_t>(std::get<JniInt>(result)));
    case JniTypeKind::long_integer:
        return pair(std::bit_cast<std::uint64_t>(std::get<JniLong>(result)));
    case JniTypeKind::float_value:
        return word(std::bit_cast<std::uint32_t>(std::get<JniFloat>(result)));
    case JniTypeKind::double_value:
        return pair(std::bit_cast<std::uint64_t>(std::get<JniDouble>(result)));
    case JniTypeKind::void_value:
        static_cast<void>(std::get<std::monostate>(result));
        return {};
    }
    throw JniGuestBindingError("JNI guest static return type is invalid");
}

[[nodiscard]] bool ResultMatches(const JniTypeKind actual,
                                 const JniTypeKind requested) {
    return requested == JniTypeKind::object
               ? actual == JniTypeKind::object || actual == JniTypeKind::array
               : actual == requested;
}

void BindOne(JniGuestCallDispatcher& dispatcher, JniEnvironment& environment,
             JniClassRegistry& classes, JniInvocationEngine& invocations,
             memory::AddressSpace& address_space, const StaticCallType type,
             const std::string_view variant, const JniArgumentSource source) {
    const auto name = std::string("CallStatic") + std::string(type.suffix) +
                      "Method" + std::string(variant);
    dispatcher.BindEnvironment(
        EnvironmentSlot(name),
        [&environment, &classes, &invocations, &address_space, type, source,
         name](const JniGuestCallFrame& frame) {
            const auto dispatch_class = environment.ResolveObjectForHle(
                frame.thread_id, JniReference{frame.registers[1]});
            if (!dispatch_class.has_value()) {
                throw JniGuestBindingError(
                    name + " requires a valid class reference");
            }
            const auto method =
                classes.ResolveMethod(JniMethodId{frame.registers[2]});
            if (!ResultMatches(method.layout.result.kind, type.result)) {
                throw JniGuestBindingError(
                    name + " return type does not match method descriptor");
            }
            std::vector<JniValue> arguments;
            switch (source) {
            case JniArgumentSource::variadic:
                arguments = ReadVariadicArguments(
                    address_space, frame, method.layout);
                break;
            case JniArgumentSource::va_list:
                arguments = ReadVaListArguments(
                    address_space, frame, method.layout);
                break;
            case JniArgumentSource::value_array:
                arguments = ReadValueArrayArguments(
                    address_space, frame, method.layout);
                break;
            }
            const auto result = invocations.InvokeStatic(
                frame.thread_id, *dispatch_class, method.id, arguments, source);
            return EncodeResult(result, method.layout.result.kind);
        });
}

[[nodiscard]] std::vector<JniValue> ReadArguments(
    memory::AddressSpace& address_space, const JniGuestCallFrame& frame,
    const JniMethodDescriptor& descriptor, const JniArgumentSource source) {
    switch (source) {
    case JniArgumentSource::variadic:
        return ReadVariadicArguments(address_space, frame, descriptor);
    case JniArgumentSource::va_list:
        return ReadVaListArguments(address_space, frame, descriptor);
    case JniArgumentSource::value_array:
        return ReadValueArrayArguments(address_space, frame, descriptor);
    }
    throw JniGuestBindingError("JNI guest argument source is invalid");
}

void BindInstanceCall(
    JniGuestCallDispatcher& dispatcher, JniEnvironment& environment,
    JniClassRegistry& classes, JniInvocationEngine& invocations,
    memory::AddressSpace& address_space,
    const std::shared_ptr<JniGuestObjectRegistry>& objects,
    const StaticCallType type,
    const std::string_view variant, const JniArgumentSource source) {
    const auto name = std::string("Call") + std::string(type.suffix) +
                      "Method" + std::string(variant);
    dispatcher.BindEnvironment(
        EnvironmentSlot(name),
        [&environment, &classes, &invocations, &address_space, objects, type,
         source, name](const JniGuestCallFrame& frame) {
            const auto receiver = JniReference{frame.registers[1]};
            const auto identity = environment.ResolveObjectForHle(
                frame.thread_id, receiver);
            if (!identity.has_value()) {
                throw JniGuestBindingError(
                    name + " requires a valid object reference");
            }
            const auto receiver_class = objects->ClassOf(*identity);
            const auto method =
                classes.ResolveMethod(JniMethodId{frame.registers[2]});
            if (!ResultMatches(method.layout.result.kind, type.result)) {
                throw JniGuestBindingError(
                    name + " return type does not match method descriptor");
            }
            const auto arguments = ReadArguments(
                address_space, frame, method.layout, source);
            return EncodeResult(
                invocations.InvokeVirtual(
                    frame.thread_id, receiver, receiver_class, method.id,
                    arguments, source),
                method.layout.result.kind);
        });
}

void BindNewObject(
    JniGuestCallDispatcher& dispatcher, JniEnvironment& environment,
    JniClassRegistry& classes, JniInvocationEngine& invocations,
    memory::AddressSpace& address_space,
    const std::shared_ptr<JniGuestObjectRegistry>& objects,
    const std::string_view variant, const JniArgumentSource source) {
    const auto name = std::string("NewObject") + std::string(variant);
    dispatcher.BindEnvironment(
        EnvironmentSlot(name),
        [&environment, &classes, &invocations, &address_space, objects, source,
         name](const JniGuestCallFrame& frame) {
            const auto java_class = environment.ResolveObjectForHle(
                frame.thread_id, JniReference{frame.registers[1]});
            if (!java_class.has_value()) {
                throw JniGuestBindingError(
                    name + " requires a valid class reference");
            }
            const auto method =
                classes.ResolveMethod(JniMethodId{frame.registers[2]});
            if (method.declaration.is_static ||
                method.declaration.name != "<init>" ||
                method.layout.result.kind != JniTypeKind::void_value) {
                throw JniGuestBindingError(
                    name + " requires a void instance constructor");
            }
            const auto arguments = ReadArguments(
                address_space, frame, method.layout, source);
            const auto object = objects->Allocate(*java_class);
            JniReference reference;
            try {
                reference = environment.PublishLocalObject(
                    frame.thread_id, object);
                static_cast<void>(invocations.InvokeNonvirtual(
                    frame.thread_id, reference, *java_class, *java_class,
                    method.id, arguments, source));
                return EncodeResult(JniValue{reference}, JniTypeKind::object);
            } catch (...) {
                if (!reference.IsNull()) {
                    environment.DeleteLocalRef(frame.thread_id, reference);
                }
                objects->Forget(object);
                throw;
            }
        });
}

}  // namespace

void BindJniGuestStaticCallSlots(JniGuestCallDispatcher& dispatcher,
                                 JniEnvironment& environment,
                                 JniClassRegistry& classes,
                                 JniInvocationEngine& invocations,
                                 memory::AddressSpace& address_space) {
    for (const auto type : kStaticCallTypes) {
        BindOne(dispatcher, environment, classes, invocations, address_space,
                type, "", JniArgumentSource::variadic);
        BindOne(dispatcher, environment, classes, invocations, address_space,
                type, "V", JniArgumentSource::va_list);
        BindOne(dispatcher, environment, classes, invocations, address_space,
                type, "A", JniArgumentSource::value_array);
    }
}

void BindJniGuestClassAndInstanceSlots(
    JniGuestCallDispatcher& dispatcher, JniEnvironment& environment,
    JniClassRegistry& classes, JniInvocationEngine& invocations,
    memory::AddressSpace& address_space, JniGuestObjectRegistry* objects) {
    const auto object_registry = objects == nullptr
                                     ? std::make_shared<JniGuestObjectRegistry>(
                                           classes)
                                     : std::shared_ptr<JniGuestObjectRegistry>(
                                           objects,
                                           [](JniGuestObjectRegistry*) {});
    dispatcher.BindEnvironment(
        EnvironmentSlot("FindClass"),
        [&environment, &classes,
         &address_space](const JniGuestCallFrame& frame) {
            const auto name = ReadCString(
                address_space, memory::GuestAddress{frame.registers[1]},
                frame.thread_id, "class name");
            const auto java_class = classes.FindClass(name);
            if (!java_class.has_value()) {
                throw JniGuestBindingError(
                    "JNI guest class is not declared: " + name);
            }
            return EncodeResult(
                JniValue{environment.PublishLocalObject(
                    frame.thread_id, *java_class)},
                JniTypeKind::object);
        });
    dispatcher.BindEnvironment(
        EnvironmentSlot("GetMethodID"),
        [&environment, &classes,
         &address_space](const JniGuestCallFrame& frame) {
            const auto java_class = environment.ResolveObjectForHle(
                frame.thread_id, JniReference{frame.registers[1]});
            if (!java_class.has_value()) {
                throw JniGuestBindingError(
                    "GetMethodID requires a valid class reference");
            }
            const auto name = ReadCString(
                address_space, memory::GuestAddress{frame.registers[2]},
                frame.thread_id, "method name");
            const auto descriptor = ReadCString(
                address_space, memory::GuestAddress{frame.registers[3]},
                frame.thread_id, "method descriptor");
            const auto method = classes.GetMethodId(
                *java_class, name, descriptor, false);
            if (!method.has_value()) {
                throw JniGuestBindingError(
                    "JNI guest instance method is not declared: " +
                    name + descriptor);
            }
            return EncodeResult(JniValue{JniInt{
                                    static_cast<JniInt>(method->Value())}},
                                JniTypeKind::integer);
        });
    dispatcher.BindEnvironment(
        EnvironmentSlot("GetObjectClass"),
        [&environment, object_registry](const JniGuestCallFrame& frame) {
            const auto object = environment.ResolveObjectForHle(
                frame.thread_id, JniReference{frame.registers[1]});
            if (!object.has_value()) {
                throw JniGuestBindingError(
                    "GetObjectClass requires a valid object reference");
            }
            return EncodeResult(
                JniValue{environment.PublishLocalObject(
                    frame.thread_id, object_registry->ClassOf(*object))},
                JniTypeKind::object);
        });
    dispatcher.BindEnvironment(
        EnvironmentSlot("IsInstanceOf"),
        [&environment, &classes,
         object_registry](const JniGuestCallFrame& frame) {
            const auto object = environment.ResolveObjectForHle(
                frame.thread_id, JniReference{frame.registers[1]});
            const auto java_class = environment.ResolveObjectForHle(
                frame.thread_id, JniReference{frame.registers[2]});
            if (!java_class.has_value()) {
                throw JniGuestBindingError(
                    "IsInstanceOf requires a valid class reference");
            }
            const auto matches = !object.has_value() || classes.IsAssignableFrom(
                *java_class, object_registry->ClassOf(*object));
            return EncodeResult(
                JniValue{static_cast<JniBoolean>(matches)},
                JniTypeKind::boolean);
        });
    BindNewObject(dispatcher, environment, classes, invocations,
                  address_space, object_registry, "",
                  JniArgumentSource::variadic);
    BindNewObject(dispatcher, environment, classes, invocations,
                  address_space, object_registry, "V",
                  JniArgumentSource::va_list);
    BindNewObject(dispatcher, environment, classes, invocations,
                  address_space, object_registry, "A",
                  JniArgumentSource::value_array);
    for (const auto type : kStaticCallTypes) {
        BindInstanceCall(dispatcher, environment, classes, invocations,
                         address_space, object_registry, type, "",
                         JniArgumentSource::variadic);
        BindInstanceCall(dispatcher, environment, classes, invocations,
                         address_space, object_registry, type, "V",
                         JniArgumentSource::va_list);
        BindInstanceCall(dispatcher, environment, classes, invocations,
                         address_space, object_registry, type, "A",
                         JniArgumentSource::value_array);
    }
}

}  // namespace ogplay::runtime
