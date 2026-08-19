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
#include "ogplay/runtime/jni/jni_object_array.h"

namespace ogplay::runtime {
namespace {

constexpr memory::GuestAddress kArrayLeaseBegin{0x71400000U};
constexpr std::uint64_t kArrayLeaseSize = 4U * 1024U * 1024U;

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

void ValidateRegion(const JniSize array_length, const JniSize start,
                    const JniSize length, const char* operation) {
    if (start < 0 || length < 0 || start > array_length ||
        length > array_length - start) {
        throw JniGuestBindingError(
            std::string(operation) + " range is outside the array");
    }
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
    PrimitiveArrayLeases(JniEnvironment& environment,
                         JniPrimitiveArrayStore& arrays,
                         memory::AddressSpace& address_space)
        : environment_(&environment), arrays_(&arrays),
          address_space_(&address_space) {
        address_space_->Map(
            {kArrayLeaseBegin, kArrayLeaseSize},
            memory::PageProtection::read | memory::PageProtection::write);
    }

    ~PrimitiveArrayLeases() {
        std::scoped_lock lock(mutex_);
        try {
            address_space_->Unmap({kArrayLeaseBegin, kArrayLeaseSize});
        } catch (...) {
        }
    }

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
        auto access = arrays_->Acquire(array, access_kind);
        const auto bytes = Encode(access.data);
        std::scoped_lock lock(mutex_);
        try {
            const auto allocation_size = std::max<std::size_t>(1U, bytes.size());
            const auto pointer = Allocate(allocation_size, element_size);
            if (!bytes.empty()) {
                address_space_->Write(pointer, bytes, frame.thread_id);
            }
            if (!is_copy.IsNull()) {
                address_space_->Write8(is_copy, 1U, frame.thread_id);
            }
            leases_.push_back({pointer, allocation_size, bytes.size(), array,
                               kind, access_kind, element_size, access.token,
                               std::move(access.data)});
            return pointer.Value();
        } catch (...) {
            arrays_->Release(array, access.token, access_kind, access.data,
                             JniArrayReleaseMode::abort);
            throw;
        }
    }

    void ReleaseResolved(const JniGuestCallFrame& frame,
                         const JniObjectIdentity array,
                         const JniPrimitiveKind kind, const char* operation,
                         const JniArrayAccessKind access_kind) {
        const auto pointer = memory::GuestAddress{frame.registers[2]};
        const auto raw_mode = std::bit_cast<JniInt>(frame.registers[3]);
        JniArrayReleaseMode mode{};
        switch (raw_mode) {
        case 0: mode = JniArrayReleaseMode::copy_back_and_release; break;
        case 1: mode = JniArrayReleaseMode::commit; break;
        case 2: mode = JniArrayReleaseMode::abort; break;
        default:
            throw JniGuestBindingError(
                std::string(operation) + " mode must be 0, JNI_COMMIT or JNI_ABORT");
        }
        std::scoped_lock lock(mutex_);
        const auto found = std::ranges::find_if(
            leases_, [pointer, array, kind, access_kind](const Lease& lease) {
                return lease.pointer == pointer && lease.array == array &&
                       lease.kind == kind &&
                       lease.access_kind == access_kind;
            });
        if (found == leases_.end()) {
            throw JniGuestBindingError(
                std::string(operation) + " pointer does not match an active lease");
        }
        auto data = found->original;
        if (mode != JniArrayReleaseMode::abort) {
            std::vector<std::byte> bytes(found->byte_size);
            if (!bytes.empty()) {
                address_space_->Validate(
                    {found->pointer, found->byte_size},
                    memory::AccessType::read, frame.thread_id);
                address_space_->Read(
                    found->pointer, bytes, frame.thread_id);
            }
            data = Decode(kind, bytes);
        }
        arrays_->Release(array, found->token, access_kind, data, mode);
        if (mode != JniArrayReleaseMode::commit) leases_.erase(found);
    }

    struct Lease final {
        memory::GuestAddress pointer;
        std::size_t allocation_size{};
        std::size_t byte_size{};
        JniObjectIdentity array;
        JniPrimitiveKind kind{};
        JniArrayAccessKind access_kind{JniArrayAccessKind::elements};
        std::size_t element_size{};
        std::uint64_t token{};
        JniPrimitiveArrayData original;
    };

    [[nodiscard]] memory::GuestAddress Allocate(
        const std::size_t size, const std::size_t alignment) const {
        auto ordered = leases_;
        std::ranges::sort(ordered, {}, &Lease::pointer);
        std::uint64_t offset{};
        for (const auto& lease : ordered) {
            offset = (offset + alignment - 1U) & ~(alignment - 1U);
            const auto lease_offset =
                lease.pointer.Value() - kArrayLeaseBegin.Value();
            if (size <= lease_offset - offset) break;
            offset = lease_offset + lease.allocation_size;
        }
        offset = (offset + alignment - 1U) & ~(alignment - 1U);
        if (offset > kArrayLeaseSize || size > kArrayLeaseSize - offset) {
            throw JniGuestBindingError(
                "JNI guest primitive array lease arena is full");
        }
        return kArrayLeaseBegin.Add(offset);
    }

    JniEnvironment* environment_{};
    JniPrimitiveArrayStore* arrays_{};
    memory::AddressSpace* address_space_{};
    mutable std::mutex mutex_;
    std::vector<Lease> leases_;
};

[[nodiscard]] std::optional<JniObjectValue> ResolveObjectValue(
    JniEnvironment& environment, JniGuestObjectRegistry& objects,
    const JniGuestCallFrame& frame, const std::uint32_t reference,
    const char* operation) {
    if (reference == 0U) return std::nullopt;
    const auto object = Resolve(environment, frame, reference, operation);
    return JniObjectValue{object, objects.ClassOf(object)};
}

}  // namespace

void BindJniGuestArraySlots(
    JniGuestCallDispatcher& dispatcher, JniEnvironment& environment,
    JniClassRegistry& classes, JniPrimitiveArrayStore& arrays,
    JniGuestObjectRegistry& objects, memory::AddressSpace& address_space) {
    const auto leases = std::make_shared<PrimitiveArrayLeases>(
        environment, arrays, address_space);
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
        dispatcher.BindEnvironment(
            Slot(new_name),
            [&environment, &arrays, binding,
             new_name](const JniGuestCallFrame& frame) {
                const auto length =
                    std::bit_cast<JniSize>(frame.registers[1]);
                static_cast<void>(CheckedByteSize(
                    length, binding.element_size, new_name.c_str()));
                const auto identity = arrays.New(binding.kind, length);
                try {
                    return Word(environment.PublishLocalObject(
                        frame.thread_id, identity).Value());
                } catch (...) {
                    arrays.Delete(identity);
                    throw;
                }
            });

        const auto get_region =
            "Get" + std::string(binding.name) + "ArrayRegion";
        dispatcher.BindEnvironment(
            Slot(get_region),
            [&environment, &arrays, &address_space, binding,
             get_region](const JniGuestCallFrame& frame) {
                const auto array = ResolvePrimitive(
                    environment, arrays, frame, binding.kind,
                    get_region.c_str());
                const auto start =
                    std::bit_cast<JniSize>(frame.registers[2]);
                const auto length =
                    std::bit_cast<JniSize>(frame.registers[3]);
                ValidateRegion(
                    arrays.Length(array), start, length, get_region.c_str());
                const auto byte_size = CheckedByteSize(
                    length, binding.element_size, get_region.c_str());
                const auto destination = memory::GuestAddress{Read32(
                    address_space, frame.stack_pointer, frame.thread_id)};
                if (byte_size != 0U) {
                    if (destination.IsNull()) {
                        throw JniGuestBindingError(
                            get_region + " requires a non-null output buffer");
                    }
                    address_space.Validate(
                        {destination, byte_size}, memory::AccessType::write,
                        frame.thread_id);
                }
                const auto bytes = Encode(arrays.Region(array, start, length));
                if (!bytes.empty()) {
                    address_space.Write(destination, bytes, frame.thread_id);
                }
                return JniGuestCallResult{};
            });

        const auto set_region =
            "Set" + std::string(binding.name) + "ArrayRegion";
        dispatcher.BindEnvironment(
            Slot(set_region),
            [&environment, &arrays, &address_space, binding,
             set_region](const JniGuestCallFrame& frame) {
                const auto array = ResolvePrimitive(
                    environment, arrays, frame, binding.kind,
                    set_region.c_str());
                const auto start =
                    std::bit_cast<JniSize>(frame.registers[2]);
                const auto length =
                    std::bit_cast<JniSize>(frame.registers[3]);
                ValidateRegion(
                    arrays.Length(array), start, length, set_region.c_str());
                const auto byte_size = CheckedByteSize(
                    length, binding.element_size, set_region.c_str());
                const auto source = memory::GuestAddress{Read32(
                    address_space, frame.stack_pointer, frame.thread_id)};
                std::vector<std::byte> bytes(byte_size);
                if (!bytes.empty()) {
                    if (source.IsNull()) {
                        throw JniGuestBindingError(
                            set_region + " requires a non-null input buffer");
                    }
                    address_space.Validate(
                        {source, byte_size}, memory::AccessType::read,
                        frame.thread_id);
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
        [&environment, &classes, &objects,
         &object_arrays](const JniGuestCallFrame& frame) {
            const auto length = std::bit_cast<JniSize>(frame.registers[1]);
            const auto element_class = Resolve(
                environment, frame, frame.registers[2], "NewObjectArray");
            static_cast<void>(
                classes.IsAssignableFrom(element_class, element_class));
            const auto initial = ResolveObjectValue(
                environment, objects, frame, frame.registers[3],
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
    dispatcher.BindEnvironment(
        Slot("GetObjectArrayElement"),
        [&environment, &object_arrays](const JniGuestCallFrame& frame) {
            const auto array = Resolve(
                environment, frame, frame.registers[1],
                "GetObjectArrayElement");
            const auto value = object_arrays.Get(
                array, std::bit_cast<JniSize>(frame.registers[2]));
            return Word(value.has_value()
                            ? environment.PublishLocalObject(
                                  frame.thread_id, value->object).Value()
                            : 0U);
        });
    dispatcher.BindEnvironment(
        Slot("SetObjectArrayElement"),
        [&environment, &objects,
         &object_arrays](const JniGuestCallFrame& frame) {
            const auto array = Resolve(
                environment, frame, frame.registers[1],
                "SetObjectArrayElement");
            const auto value = ResolveObjectValue(
                environment, objects, frame, frame.registers[3],
                "SetObjectArrayElement");
            object_arrays.Set(
                array, std::bit_cast<JniSize>(frame.registers[2]), value);
            return JniGuestCallResult{};
        });
}

}  // namespace ogplay::runtime
