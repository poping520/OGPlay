#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "ogplay/memory/address_space.h"
#include "ogplay/runtime/jni_guest/jni_guest_bindings.h"
#include "ogplay/runtime/jni_guest/jni_guest_static_calls.h"
#include "ogplay/runtime/jni/jni_array.h"
#include "ogplay/runtime/jni/jni_class_registry.h"
#include "ogplay/runtime/jni/jni_environment.h"
#include "ogplay/runtime/jni/jni_object.h"
#include "ogplay/runtime/jni/jni_object_array.h"
#include "jni_guest_memory.h"
#include "jni_guest_lookup.h"

namespace ogplay::runtime {
namespace {

struct PrimitiveBinding final {
    std::string_view name;
    JniPrimitiveKind kind;
    std::size_t element_size;
};

constexpr std::array<PrimitiveBinding, 8> kPrimitiveBindings{{
    {"Boolean", JniPrimitiveKind::boolean, sizeof(JniBoolean)},
    {"Byte", JniPrimitiveKind::byte, sizeof(JniByte)},
    {"Char", JniPrimitiveKind::character, sizeof(JniChar)},
    {"Short", JniPrimitiveKind::short_integer, sizeof(JniShort)},
    {"Int", JniPrimitiveKind::integer, sizeof(JniInt)},
    {"Long", JniPrimitiveKind::long_integer, sizeof(JniLong)},
    {"Float", JniPrimitiveKind::float_value, sizeof(JniFloat)},
    {"Double", JniPrimitiveKind::double_value, sizeof(JniDouble)},
}};

[[nodiscard]] JniSlot Slot(const std::string_view name) {
    const auto slot = FindJniSlot(name);
    if (!slot.has_value()) {
        throw std::logic_error("required JNI array slot is absent");
    }
    return *slot;
}

[[nodiscard]] JniGuestCallResult Word(const std::uint32_t value) {
    return {JniGuestReturnWidth::word, {value, 0U}};
}

[[nodiscard]] JniGuestCallResult Int(const JniInt value) {
    return Word(std::bit_cast<std::uint32_t>(value));
}

[[nodiscard]] JniObjectIdentity Resolve(
    JniEnvironment& environment, const JniGuestCallFrame& frame,
    const std::uint32_t reference, const char* operation) {
    const auto identity = environment.ResolveObjectForHle(
        frame.thread_id, JniReference{reference});
    if (!identity.has_value()) {
        throw JniGuestBindingError(
            std::string(operation) + " requires a valid reference");
    }
    return *identity;
}

[[nodiscard]] JniObjectIdentity ResolvePrimitive(
    JniEnvironment& environment, JniPrimitiveArrayStore& arrays,
    const JniGuestCallFrame& frame, const JniPrimitiveKind kind,
    const char* operation) {
    const auto array = Resolve(
        environment, frame, frame.registers[1], operation);
    if (arrays.Kind(array) != kind) {
        throw JniGuestBindingError(
            std::string(operation) + " received the wrong array type");
    }
    return array;
}

[[nodiscard]] std::size_t CheckedByteSize(
    const JniSize length, const std::size_t element_size,
    const char* operation) {
    if (length < 0) {
        throw JniGuestBindingError(
            std::string(operation) + " length cannot be negative");
    }
    const auto count = static_cast<std::size_t>(length);
    if (element_size != 0U &&
        count > std::numeric_limits<std::size_t>::max() / element_size) {
        throw JniGuestBindingError(
            std::string(operation) + " byte size overflows");
    }
    return count * element_size;
}

bool ValidateRegion(JniEnvironment& env, JniClassRegistry& classes, const JniGuestCallFrame& frame,
                    JniSize array_length, JniSize start, JniSize length, JniPrimitiveKind kind, const char* direction) {
    if (start >= 0 && length >= 0 && start <= array_length && length <= array_length - start) return true;
    // Dalvik Jni.cpp:2388-2395: exception precedes every buffer access/write.
    const std::string descriptor = std::string("[") + "ZBCSIJFD"[static_cast<std::size_t>(kind)];
    jni_guest_detail::ThrowJavaError(env, classes, frame.thread_id, "java/lang/ArrayIndexOutOfBoundsException",
                                     descriptor + " offset=" + std::to_string(start) +
                                         " length=" + std::to_string(length) + " " + direction +
                                         ".length=" + std::to_string(array_length));
    return false;
}

template <typename T>
using UnsignedBits = std::conditional_t<
    sizeof(T) == 1U, std::uint8_t,
    std::conditional_t<sizeof(T) == 2U, std::uint16_t,
                       std::conditional_t<sizeof(T) == 4U, std::uint32_t,
                                          std::uint64_t>>>;

template <typename T>
[[nodiscard]] std::vector<std::byte> EncodeValues(
    const std::vector<T>& values) {
    std::vector<std::byte> bytes(values.size() * sizeof(T));
    for (std::size_t index = 0; index < values.size(); ++index) {
        const auto bits = std::bit_cast<UnsignedBits<T>>(values[index]);
        for (std::size_t byte = 0; byte < sizeof(T); ++byte) {
            bytes[index * sizeof(T) + byte] = static_cast<std::byte>(
                (bits >> static_cast<unsigned>(byte * 8U)) & 0xffU);
        }
    }
    return bytes;
}

[[nodiscard]] std::vector<std::byte> Encode(
    const JniPrimitiveArrayData& data) {
    return std::visit(
        [](const auto& values) { return EncodeValues(values); }, data);
}

template <typename T>
[[nodiscard]] std::vector<T> DecodeValues(
    const std::span<const std::byte> bytes) {
    std::vector<T> values(bytes.size() / sizeof(T));
    for (std::size_t index = 0; index < values.size(); ++index) {
        UnsignedBits<T> bits{};
        for (std::size_t byte = 0; byte < sizeof(T); ++byte) {
            bits |= std::to_integer<UnsignedBits<T>>(
                        bytes[index * sizeof(T) + byte])
                    << static_cast<unsigned>(byte * 8U);
        }
        values[index] = std::bit_cast<T>(bits);
    }
    return values;
}

[[nodiscard]] JniPrimitiveArrayData Decode(
    const JniPrimitiveKind kind, const std::span<const std::byte> bytes) {
    switch (kind) {
    case JniPrimitiveKind::boolean:
        return DecodeValues<JniBoolean>(bytes);
    case JniPrimitiveKind::byte: return DecodeValues<JniByte>(bytes);
    case JniPrimitiveKind::character: return DecodeValues<JniChar>(bytes);
    case JniPrimitiveKind::short_integer:
        return DecodeValues<JniShort>(bytes);
    case JniPrimitiveKind::integer: return DecodeValues<JniInt>(bytes);
    case JniPrimitiveKind::long_integer:
        return DecodeValues<JniLong>(bytes);
    case JniPrimitiveKind::float_value:
        return DecodeValues<JniFloat>(bytes);
    case JniPrimitiveKind::double_value:
        return DecodeValues<JniDouble>(bytes);
    }
    throw std::logic_error("unknown JNI primitive array kind");
}

class PrimitiveArrayLeases final {
public:
    PrimitiveArrayLeases(JniEnvironment& environment, JniPrimitiveArrayStore& arrays,
                         memory::AddressSpace& address_space, JniGuestCallDispatcher& dispatcher, core::Logger* logger)
        : environment_(&environment), arrays_(&arrays), address_space_(&address_space), dispatcher_(&dispatcher),
          logger_(logger) {}

    [[nodiscard]] std::uint32_t Acquire(
        const JniGuestCallFrame& frame, const JniPrimitiveKind kind,
        const std::size_t element_size, const char* operation,
        const JniArrayAccessKind access_kind = JniArrayAccessKind::elements) {
        const auto array = ResolvePrimitive(
            *environment_, *arrays_, frame, kind, operation);
        return AcquireResolved(frame, array, kind, element_size, access_kind);
    }

    [[nodiscard]] std::uint32_t AcquireCritical(
        const JniGuestCallFrame& frame) {
        constexpr auto operation = "GetPrimitiveArrayCritical";
        const auto array = Resolve(
            *environment_, frame, frame.registers[1], operation);
        const auto kind = arrays_->Kind(array);
        const auto binding = std::ranges::find(
            kPrimitiveBindings, kind, &PrimitiveBinding::kind);
        if (binding == kPrimitiveBindings.end()) {
            throw JniGuestBindingError(
                "GetPrimitiveArrayCritical received an unknown array type");
        }
        return AcquireResolved(frame, array, kind, binding->element_size,
                               JniArrayAccessKind::critical);
    }

    void ReleaseCritical(const JniGuestCallFrame& frame) {
        constexpr auto operation = "ReleasePrimitiveArrayCritical";
        const auto array = Resolve(
            *environment_, frame, frame.registers[1], operation);
        ReleaseResolved(frame, array, arrays_->Kind(array), operation,
                        JniArrayAccessKind::critical);
    }

    void Release(const JniGuestCallFrame& frame,
                 const JniPrimitiveKind kind, const char* operation) {
        const auto array = ResolvePrimitive(
            *environment_, *arrays_, frame, kind, operation);
        ReleaseResolved(frame, array, kind, operation,
                        JniArrayAccessKind::elements);
    }

private:
    [[nodiscard]] std::uint32_t AcquireResolved(
        const JniGuestCallFrame& frame, const JniObjectIdentity array,
        const JniPrimitiveKind kind, const std::size_t element_size,
        const JniArrayAccessKind access_kind) {
        const auto is_copy = memory::GuestAddress{frame.registers[2]};
        if (!is_copy.IsNull()) {
            address_space_->Validate(
                {is_copy, 1U}, memory::AccessType::write, frame.thread_id);
        }
        static_cast<void>(kind);
        static_cast<void>(element_size);
        const auto access = arrays_->Acquire(array, access_kind);
        try {
            if (!is_copy.IsNull()) address_space_->Write8(is_copy, 0U, frame.thread_id);
        } catch (...) {
            arrays_->Release(array, JniArrayReleaseMode::abort);
            throw;
        }
        if (access.warn_threshold) Warn(frame, array, "pin_threshold", access.pin_count);
        return access.address.Value();
    }

    void Warn(const JniGuestCallFrame& frame, JniObjectIdentity array, std::string_view reason, std::size_t count) {
        const auto slot = JniSlotName(JniSlot{frame.thunk.slot});
        dispatcher_->RecordWarning("runtime.jni.array." + std::string(reason) + "." + std::string(slot) + "." +
                                       std::to_string(array.value),
                                   frame.link_register);
        if (logger_)
            logger_->Write(core::LogLevel::warn, "runtime.jni.array", reason, {.guest_thread = frame.thread_id},
                           {{"array", array.value},
                            {"slot", std::string(slot)},
                            {"guest_lr", core::GuestAddress{frame.link_register}},
                            {"pin_count", static_cast<std::uint64_t>(count)}},
                           {.mode = core::RateLimitMode::none});
    }

    void ReleaseResolved(const JniGuestCallFrame& frame, JniObjectIdentity array, JniPrimitiveKind kind,
                         const char* operation, JniArrayAccessKind access_kind) {
        static_cast<void>(kind);
        static_cast<void>(operation);
        static_cast<void>(access_kind);
        // Dalvik Jni.cpp:2383/2615 checks only COMMIT; elems is deliberately ignored.
        const auto mode = frame.registers[3] == 1 ? JniArrayReleaseMode::commit : JniArrayReleaseMode::abort;
        if (!arrays_->Release(array, mode)) Warn(frame, array, "unmatched_unpin", 0);
    }

    JniEnvironment* environment_{};
    JniPrimitiveArrayStore* arrays_{};
    memory::AddressSpace* address_space_{};
    JniGuestCallDispatcher* dispatcher_{};
    core::Logger* logger_{};
};

[[nodiscard]] std::optional<JniObjectValue> ResolveObjectValue(
    JniEnvironment& environment, JniClassRegistry& classes,
    JniStringStore& strings, JniGuestObjectRegistry& objects,
    const JniGuestCallFrame& frame, const std::uint32_t reference,
    const char* operation) {
    if (reference == 0U) return std::nullopt;
    const auto object = Resolve(environment, frame, reference, operation);
    if (strings.Contains(object)) {
        const auto java_class = classes.FindClass("java/lang/String");
        if (!java_class.has_value()) {
            throw JniGuestBindingError("java/lang/String is not registered");
        }
        return JniObjectValue{object, *java_class};
    }
    return JniObjectValue{object, objects.ClassOf(object)};
}

}  // namespace

void BindJniGuestArraySlots(JniGuestCallDispatcher& dispatcher, JniEnvironment& environment, JniClassRegistry& classes,
                            JniStringStore& strings, JniPrimitiveArrayStore& arrays, JniGuestObjectRegistry& objects,
                            memory::AddressSpace& address_space, core::Logger* logger) {
    const auto leases = std::make_shared<PrimitiveArrayLeases>(environment, arrays, address_space, dispatcher, logger);
    arrays.SetPinTeardownObserver([logger](std::uint64_t count, std::uint64_t bytes) {
        if (logger)
            logger->Write(core::LogLevel::warn, "runtime.jni.array", "retiring outstanding pins", {},
                          {{"arrays", count}, {"bytes", bytes}}, {.mode = core::RateLimitMode::none});
    });
    auto& object_arrays = objects.ObjectArrays();

    dispatcher.BindEnvironment(
        Slot("GetArrayLength"),
        [&environment, &arrays, &object_arrays](const JniGuestCallFrame& frame) {
            const auto array = Resolve(
                environment, frame, frame.registers[1], "GetArrayLength");
            return Int(object_arrays.Contains(array)
                           ? object_arrays.Length(array)
                           : arrays.Length(array));
        });

    for (const auto binding : kPrimitiveBindings) {
        const auto new_name = "New" + std::string(binding.name) + "Array";
        dispatcher.BindEnvironment(Slot(new_name), [&environment, &classes, &arrays, binding,
                                                    new_name](const JniGuestCallFrame& frame) {
            const auto length = std::bit_cast<JniSize>(frame.registers[1]);
            static_cast<void>(CheckedByteSize(length, binding.element_size, new_name.c_str()));
            JniObjectIdentity identity;
            try {
                identity = arrays.New(binding.kind, length);
            } catch (const std::bad_alloc&) {
                jni_guest_detail::ThrowJavaError(environment, classes, frame.thread_id, "java/lang/OutOfMemoryError",
                                                 "primitive array payload allocation failed");
                return Word(0);
            }
            try {
                return Word(environment.PublishLocalObject(frame.thread_id, identity).Value());
            } catch (...) {
                arrays.Delete(identity);
                throw;
            }
        });

        const auto get_region =
            "Get" + std::string(binding.name) + "ArrayRegion";
        dispatcher.BindEnvironment(Slot(get_region), [&environment, &classes, &arrays, &address_space, binding,
                                                      get_region](const JniGuestCallFrame& frame) {
            const auto array = ResolvePrimitive(environment, arrays, frame, binding.kind, get_region.c_str());
            const auto start = std::bit_cast<JniSize>(frame.registers[2]);
            const auto length = std::bit_cast<JniSize>(frame.registers[3]);
            if (!ValidateRegion(environment, classes, frame, arrays.Length(array), start, length, binding.kind, "src"))
                return JniGuestCallResult{};
            const auto byte_size = CheckedByteSize(length, binding.element_size, get_region.c_str());
            const auto destination =
                memory::GuestAddress{ReadGuest32(address_space, frame.stack_pointer, frame.thread_id)};
            if (byte_size != 0U) {
                if (destination.IsNull()) {
                    throw JniGuestBindingError(get_region + " requires a non-null output buffer");
                }
                address_space.Validate({destination, byte_size}, memory::AccessType::write, frame.thread_id);
            }
            const auto bytes = Encode(arrays.Region(array, start, length));
            if (!bytes.empty()) {
                address_space.Write(destination, bytes, frame.thread_id);
            }
            return JniGuestCallResult{};
        });

        const auto set_region =
            "Set" + std::string(binding.name) + "ArrayRegion";
        dispatcher.BindEnvironment(Slot(set_region), [&environment, &classes, &arrays, &address_space, binding,
                                                      set_region](const JniGuestCallFrame& frame) {
            const auto array = ResolvePrimitive(environment, arrays, frame, binding.kind, set_region.c_str());
            const auto start = std::bit_cast<JniSize>(frame.registers[2]);
            const auto length = std::bit_cast<JniSize>(frame.registers[3]);
            if (!ValidateRegion(environment, classes, frame, arrays.Length(array), start, length, binding.kind, "dst"))
                return JniGuestCallResult{};
            const auto byte_size = CheckedByteSize(length, binding.element_size, set_region.c_str());
            const auto source = memory::GuestAddress{ReadGuest32(address_space, frame.stack_pointer, frame.thread_id)};
            std::vector<std::byte> bytes(byte_size);
            if (!bytes.empty()) {
                if (source.IsNull()) {
                    throw JniGuestBindingError(set_region + " requires a non-null input buffer");
                }
                address_space.Validate({source, byte_size}, memory::AccessType::read, frame.thread_id);
                address_space.Read(source, bytes, frame.thread_id);
            }
            arrays.SetRegion(array, start, Decode(binding.kind, bytes));
            return JniGuestCallResult{};
        });

        const auto get_elements =
            "Get" + std::string(binding.name) + "ArrayElements";
        dispatcher.BindEnvironment(
            Slot(get_elements),
            [leases, binding,
             get_elements](const JniGuestCallFrame& frame) {
                return Word(leases->Acquire(
                    frame, binding.kind, binding.element_size,
                    get_elements.c_str()));
            });

        const auto release_elements =
            "Release" + std::string(binding.name) + "ArrayElements";
        dispatcher.BindEnvironment(
            Slot(release_elements),
            [leases, binding,
             release_elements](const JniGuestCallFrame& frame) {
                leases->Release(
                    frame, binding.kind, release_elements.c_str());
                return JniGuestCallResult{};
            });
    }

    dispatcher.BindEnvironment(
        Slot("GetPrimitiveArrayCritical"),
        [leases](const JniGuestCallFrame& frame) {
            return Word(leases->AcquireCritical(frame));
        });
    dispatcher.BindEnvironment(
        Slot("ReleasePrimitiveArrayCritical"),
        [leases](const JniGuestCallFrame& frame) {
            leases->ReleaseCritical(frame);
            return JniGuestCallResult{};
        });

    dispatcher.BindEnvironment(
        Slot("NewObjectArray"),
        [&environment, &classes, &strings, &objects,
         &object_arrays](const JniGuestCallFrame& frame) {
            const auto length = std::bit_cast<JniSize>(frame.registers[1]);
            const auto element_class = Resolve(
                environment, frame, frame.registers[2], "NewObjectArray");
            static_cast<void>(
                classes.IsAssignableFrom(element_class, element_class));
            const auto initial = ResolveObjectValue(
                environment, classes, strings, objects, frame,
                frame.registers[3],
                "NewObjectArray");
            const auto identity =
                object_arrays.New(element_class, length, initial);
            try {
                return Word(environment.PublishLocalObject(
                    frame.thread_id, identity).Value());
            } catch (...) {
                object_arrays.Delete(identity);
                throw;
            }
        });
    dispatcher.BindEnvironment(Slot("GetObjectArrayElement"), [&environment, &classes, &object_arrays,
                                                               &objects](const JniGuestCallFrame& frame) {
        const auto array = Resolve(environment, frame, frame.registers[1], "GetObjectArrayElement");
        const auto index = std::bit_cast<JniSize>(frame.registers[2]);
        const auto length = object_arrays.Length(array);
        // Dalvik Jni.cpp:2271-2294 -> Exception.cpp:1205-1209.
        if (index < 0 || index >= length) {
            jni_guest_detail::ThrowJavaError(environment, classes, frame.thread_id,
                                             "java/lang/ArrayIndexOutOfBoundsException",
                                             "length=" + std::to_string(length) + "; index=" + std::to_string(index));
            return Word(0U);
        }
        const auto value = object_arrays.Get(array, index);
        if (value.has_value()) {
            objects.EnsureRegistered(value->object, value->java_class);
        }
        return Word(value.has_value() ? environment.PublishLocalObject(frame.thread_id, value->object).Value() : 0U);
    });
    dispatcher.BindEnvironment(
        Slot("SetObjectArrayElement"),
        [&environment, &classes, &strings, &objects,
         &object_arrays](const JniGuestCallFrame& frame) {
            const auto array = Resolve(
                environment, frame, frame.registers[1],
                "SetObjectArrayElement");
            const auto value = ResolveObjectValue(
                environment, classes, strings, objects, frame,
                frame.registers[3],
                "SetObjectArrayElement");
            object_arrays.Set(
                array, std::bit_cast<JniSize>(frame.registers[2]), value);
            return JniGuestCallResult{};
        });
}

}  // namespace ogplay::runtime
