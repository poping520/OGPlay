#include "ogplay/runtime/jni_guest/jni_guest_static_calls.h"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "ogplay/memory/address_space.h"
#include "ogplay/runtime/jni_guest/jni_guest_bindings.h"
#include "ogplay/runtime/jni/jni_class_registry.h"
#include "ogplay/runtime/jni/jni_environment.h"
#include "ogplay/runtime/jni/jni_invocation.h"
#include "jni_guest_memory.h"
#include "jni_guest_value_codec.h"
#include "ogplay/runtime/jni/jni_object.h"
#include "ogplay/runtime/jni/jni_object_array.h"

namespace ogplay::runtime {

class JniGuestObjectRegistry::Impl final {
public:
    explicit Impl(const JniClassRegistry& classes)
        : classes_(&classes), object_arrays_(classes) {}

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
        if (!objects_.emplace(Key(object), java_class).second) {
            throw JniGuestBindingError(
                "JNI guest object is already registered");
        }
    }

    void Forget(const JniObjectIdentity object) {
        if (object.value == 0U) {
            throw JniGuestBindingError(
                "JNI guest object identity is invalid");
        }
        std::scoped_lock lock(mutex_);
        if (objects_.erase(Key(object)) == 0U) {
            throw JniGuestBindingError(
                "JNI guest object is not registered");
        }
    }

    [[nodiscard]] JniObjectIdentity ClassOf(
        const JniObjectIdentity object) const {
        if (object.value == 0U) {
            throw JniGuestBindingError(
                "JNI guest receiver is not a registered instance");
        }
        {
            std::scoped_lock lock(mutex_);
            const auto found = objects_.find(Key(object));
            if (found != objects_.end()) return found->second;
        }

        // AOSP Dalvik Jni.cpp::FindClass publishes the ClassObject itself as
        // a jclass. It is therefore also a legal jobject receiver whose
        // runtime class is java.lang.Class; it is not an allocated entry in
        // the ordinary object registry.
        try {
            if (classes_->IsAssignableFrom(object, object)) {
                const auto class_class = classes_->FindClass("java/lang/Class");
                if (class_class.has_value()) return *class_class;
            }
        } catch (const JniClassRegistryError&) {
        }
        throw JniGuestBindingError(
            "JNI guest receiver is not a registered instance");
    }

    [[nodiscard]] JniObjectArrayStore& ObjectArrays() noexcept {
        return object_arrays_;
    }

    [[nodiscard]] const JniObjectArrayStore& ObjectArrays() const noexcept {
        return object_arrays_;
    }

private:
    using ObjectKey = std::pair<std::uint8_t, std::uint64_t>;

    [[nodiscard]] static ObjectKey Key(const JniObjectIdentity object) {
        return {static_cast<std::uint8_t>(object.domain), object.value};
    }

    void Validate(const JniObjectIdentity object,
                  const JniObjectIdentity java_class) const {
        if (object.value == 0U || java_class.value == 0U) {
            throw JniGuestBindingError(
                "JNI guest object registration is invalid");
        }
        if (java_class.domain == JniObjectDomain::dex_vm) return;
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
    JniObjectArrayStore object_arrays_;
    mutable std::mutex mutex_;
    std::map<ObjectKey, JniObjectIdentity> objects_;
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
JniObjectArrayStore& JniGuestObjectRegistry::ObjectArrays() noexcept {
    return impl_->ObjectArrays();
}
const JniObjectArrayStore&
JniGuestObjectRegistry::ObjectArrays() const noexcept {
    return impl_->ObjectArrays();
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
    const JniMethodDescriptor& descriptor,
    std::size_t register_index = 3U) {
    std::uint32_t stack_offset{};
    const auto word = [&]() {
        if (register_index < frame.registers.size()) {
            return frame.registers[register_index++];
        }
        const auto result = ReadGuest32(address_space,
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
    const JniMethodDescriptor& descriptor,
    const std::optional<memory::GuestAddress> values = std::nullopt) {
    auto cursor = values.value_or(
        memory::GuestAddress{frame.registers[3]});
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
                ReadGuest64(address_space, cursor, frame.thread_id)));
            cursor = cursor.Add(8U);
            break;
        case JniTypeKind::float_value:
            cursor = Align8(cursor);
            arguments.emplace_back(static_cast<JniFloat>(
                std::bit_cast<JniDouble>(
                    ReadGuest64(address_space, cursor, frame.thread_id))));
            cursor = cursor.Add(8U);
            break;
        case JniTypeKind::double_value:
            cursor = Align8(cursor);
            arguments.emplace_back(std::bit_cast<JniDouble>(
                ReadGuest64(address_space, cursor, frame.thread_id)));
            cursor = cursor.Add(8U);
            break;
        case JniTypeKind::void_value:
            throw JniGuestBindingError(
                "JNI va_list parameter cannot have void type");
        default:
            arguments.emplace_back(WordValue(
                parameter, ReadGuest32(address_space, cursor, frame.thread_id)));
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
        return static_cast<JniBoolean>(ReadGuest8(address_space, address, thread_id));
    case JniTypeKind::byte:
        return std::bit_cast<JniByte>(ReadGuest8(address_space, address, thread_id));
    case JniTypeKind::character:
        return static_cast<JniChar>(ReadGuest16(address_space, address, thread_id));
    case JniTypeKind::short_integer:
        return std::bit_cast<JniShort>(ReadGuest16(address_space, address, thread_id));
    case JniTypeKind::integer:
        return std::bit_cast<JniInt>(ReadGuest32(address_space, address, thread_id));
    case JniTypeKind::long_integer:
        return std::bit_cast<JniLong>(ReadGuest64(address_space, address, thread_id));
    case JniTypeKind::float_value:
        return std::bit_cast<JniFloat>(ReadGuest32(address_space, address, thread_id));
    case JniTypeKind::double_value:
        return std::bit_cast<JniDouble>(ReadGuest64(address_space, address, thread_id));
    case JniTypeKind::object:
    case JniTypeKind::array:
        return JniReference{ReadGuest32(address_space, address, thread_id)};
    case JniTypeKind::void_value:
        throw JniGuestBindingError(
            "JNI value-array parameter cannot have void type");
    }
    throw JniGuestBindingError("JNI guest value-array type is invalid");
}

[[nodiscard]] std::vector<JniValue> ReadValueArrayArguments(
    memory::AddressSpace& address_space, const JniGuestCallFrame& frame,
    const JniMethodDescriptor& descriptor,
    const std::optional<memory::GuestAddress> explicit_values =
        std::nullopt) {
    const auto values = explicit_values.value_or(
        memory::GuestAddress{frame.registers[3]});
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
    return jni_guest_detail::EncodeValueResult(
        result, kind, jni_guest_detail::VoidResultPolicy::allow,
        "JNI guest static return type is invalid");
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
    const JniMethodDescriptor& descriptor, const JniArgumentSource source,
    const bool nonvirtual = false) {
    const auto stacked_pointer = [&]() {
        return memory::GuestAddress{
            ReadGuest32(address_space, frame.stack_pointer, frame.thread_id)};
    };
    switch (source) {
    case JniArgumentSource::variadic:
        return ReadVariadicArguments(
            address_space, frame, descriptor, nonvirtual ? 4U : 3U);
    case JniArgumentSource::va_list:
        return ReadVaListArguments(
            address_space, frame, descriptor,
            nonvirtual
                ? std::optional<memory::GuestAddress>{stacked_pointer()}
                : std::nullopt);
    case JniArgumentSource::value_array:
        return ReadValueArrayArguments(
            address_space, frame, descriptor,
            nonvirtual
                ? std::optional<memory::GuestAddress>{stacked_pointer()}
                : std::nullopt);
    }
    throw JniGuestBindingError("JNI guest argument source is invalid");
}

void BindNonvirtualCall(
    JniGuestCallDispatcher& dispatcher, JniEnvironment& environment,
    JniClassRegistry& classes, JniInvocationEngine& invocations,
    memory::AddressSpace& address_space,
    const std::shared_ptr<JniGuestObjectRegistry>& objects,
    const StaticCallType type, const std::string_view variant,
    const JniArgumentSource source) {
    const auto name = std::string("CallNonvirtual") +
                      std::string(type.suffix) + "Method" +
                      std::string(variant);
    dispatcher.BindEnvironment(
        EnvironmentSlot(name),
        [&environment, &classes, &invocations, &address_space, objects, type,
         source, name](const JniGuestCallFrame& frame) {
            const auto receiver = JniReference{frame.registers[1]};
            const auto object = environment.ResolveObjectForHle(
                frame.thread_id, receiver);
            const auto dispatch_class = environment.ResolveObjectForHle(
                frame.thread_id, JniReference{frame.registers[2]});
            if (!object.has_value()) {
                throw JniGuestBindingError(
                    name + " requires a valid object reference");
            }
            if (!dispatch_class.has_value()) {
                throw JniGuestBindingError(
                    name + " requires a valid class reference");
            }
            const auto method =
                classes.ResolveMethod(JniMethodId{frame.registers[3]});
            if (!ResultMatches(method.layout.result.kind, type.result)) {
                throw JniGuestBindingError(
                    name + " return type does not match method descriptor");
            }
            const auto arguments = ReadArguments(
                address_space, frame, method.layout, source, true);
            return EncodeResult(
                invocations.InvokeNonvirtual(
                    frame.thread_id, receiver, objects->ClassOf(*object),
                    *dispatch_class, method.id, arguments, source),
                method.layout.result.kind);
        });
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
            const auto name = ReadGuestCString(
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
            const auto name = ReadGuestCString(
                address_space, memory::GuestAddress{frame.registers[2]},
                frame.thread_id, "method name");
            const auto descriptor = ReadGuestCString(
                address_space, memory::GuestAddress{frame.registers[3]},
                frame.thread_id, "method descriptor");
            const auto method = classes.GetMethodId(
                *java_class, name, descriptor, false);
            if (!method.has_value()) {
                throw JniGuestBindingError(
                    "JNI guest instance method is not declared: class=" +
                    classes.ClassName(*java_class) + " method=" + name +
                    descriptor);
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
        BindNonvirtualCall(dispatcher, environment, classes, invocations,
                           address_space, object_registry, type, "",
                           JniArgumentSource::variadic);
        BindNonvirtualCall(dispatcher, environment, classes, invocations,
                           address_space, object_registry, type, "V",
                           JniArgumentSource::va_list);
        BindNonvirtualCall(dispatcher, environment, classes, invocations,
                           address_space, object_registry, type, "A",
                           JniArgumentSource::value_array);
    }
}

}  // namespace ogplay::runtime
