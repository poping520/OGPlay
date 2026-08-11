#include "ogplay/runtime/jni_guest/jni_guest_string_bindings.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <span>
#include <vector>

#include "ogplay/memory/address_space.h"
#include "ogplay/runtime/jni_guest/jni_guest_bindings.h"
#include "ogplay/runtime/jni_guest/jni_guest_dispatch.h"
#include "ogplay/runtime/jni/jni.h"
#include "ogplay/runtime/jni/jni_environment.h"
#include "ogplay/runtime/jni/jni_object.h"

namespace ogplay::runtime {
namespace {

constexpr memory::GuestAddress kStringLeaseBegin{0x71300000U};
constexpr std::uint64_t kStringLeaseSize = 64U * 1024U;
constexpr memory::GuestAddress kUtf16LeaseBegin{0x71310000U};
constexpr std::uint64_t kUtf16LeaseSize = 64U * 1024U;
constexpr std::size_t kMaximumUtf16CodeUnits = 1024U * 1024U;

[[nodiscard]] JniSlot Slot(const std::string_view name) {
    const auto slot = FindJniSlot(name);
    if (!slot.has_value()) {
        throw std::logic_error("required JNI string slot is absent");
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

[[nodiscard]] JniObjectIdentity ResolveString(
    JniEnvironment& environment, const JniGuestCallFrame& frame) {
    const auto identity = environment.ResolveObjectForHle(
        frame.thread_id, JniReference{frame.registers[1]});
    if (!identity.has_value()) {
        throw JniGuestBindingError(
            "JNI guest string reference is null or invalid");
    }
    return *identity;
}

class ModifiedUtf8Leases final {
public:
    ModifiedUtf8Leases(JniEnvironment& environment, JniStringStore& strings,
                       memory::AddressSpace& address_space)
        : environment_(&environment), strings_(&strings),
          address_space_(&address_space) {
        address_space_->Map(
            {kStringLeaseBegin, kStringLeaseSize},
            memory::PageProtection::read | memory::PageProtection::write);
    }

    ~ModifiedUtf8Leases() {
        std::scoped_lock lock(mutex_);
        try {
            address_space_->Unmap({kStringLeaseBegin, kStringLeaseSize});
        } catch (...) {
        }
    }

    [[nodiscard]] std::uint32_t Acquire(const JniGuestCallFrame& frame) {
        const auto string = ResolveString(*environment_, frame);
        auto access = strings_->Acquire(
            string, JniStringAccessKind::modified_utf8);
        access.modified_utf8.push_back(0U);
        std::scoped_lock lock(mutex_);
        try {
            const auto pointer = Allocate(access.modified_utf8.size());
            address_space_->Write(
                pointer,
                std::as_bytes(std::span{access.modified_utf8}),
                frame.thread_id);
            if (frame.registers[2] != 0U) {
                const std::byte copied{1};
                address_space_->Write(
                    memory::GuestAddress{frame.registers[2]},
                    std::span{&copied, 1}, frame.thread_id);
            }
            leases_.push_back(
                {pointer, access.modified_utf8.size(), string, access.token});
            return pointer.Value();
        } catch (...) {
            strings_->Release(string, access.token,
                              JniStringAccessKind::modified_utf8);
            throw;
        }
    }

    void Release(const JniGuestCallFrame& frame) {
        const auto string = ResolveString(*environment_, frame);
        const auto pointer = memory::GuestAddress{frame.registers[2]};
        std::scoped_lock lock(mutex_);
        const auto found = std::ranges::find_if(
            leases_, [pointer](const Lease& lease) {
                return lease.pointer == pointer;
            });
        if (found == leases_.end() || found->string != string) {
            throw JniGuestBindingError(
                "ReleaseStringUTFChars pointer does not match an active lease");
        }
        strings_->Release(found->string, found->token,
                          JniStringAccessKind::modified_utf8);
        leases_.erase(found);
    }

private:
    struct Lease final {
        memory::GuestAddress pointer;
        std::size_t size{};
        JniObjectIdentity string;
        std::uint64_t token{};
    };

    [[nodiscard]] memory::GuestAddress Allocate(const std::size_t size) const {
        if (size == 0 || size > kStringLeaseSize) {
            throw JniGuestBindingError(
                "JNI guest modified UTF-8 lease exceeds its arena");
        }
        auto ordered = leases_;
        std::ranges::sort(ordered, {}, &Lease::pointer);
        std::uint64_t offset{};
        for (const auto& lease : ordered) {
            const auto lease_offset =
                lease.pointer.Value() - kStringLeaseBegin.Value();
            if (size <= lease_offset - offset) break;
            offset = lease_offset + lease.size;
        }
        if (offset > kStringLeaseSize || size > kStringLeaseSize - offset) {
            throw JniGuestBindingError(
                "JNI guest modified UTF-8 lease arena is full");
        }
        return kStringLeaseBegin.Add(offset);
    }

    JniEnvironment* environment_{};
    JniStringStore* strings_{};
    memory::AddressSpace* address_space_{};
    mutable std::mutex mutex_;
    std::vector<Lease> leases_;
};

[[nodiscard]] std::vector<std::byte> EncodeUtf16(
    const std::span<const JniChar> chars, const bool terminate) {
    std::vector<std::byte> bytes(
        (chars.size() + (terminate ? 1U : 0U)) * sizeof(JniChar));
    for (std::size_t index = 0; index < chars.size(); ++index) {
        bytes[index * 2U] = static_cast<std::byte>(chars[index] & 0xffU);
        bytes[index * 2U + 1U] =
            static_cast<std::byte>(chars[index] >> 8U);
    }
    return bytes;
}

class Utf16Leases final {
public:
    Utf16Leases(JniEnvironment& environment, JniStringStore& strings,
                memory::AddressSpace& address_space)
        : environment_(&environment), strings_(&strings),
          address_space_(&address_space) {
        address_space_->Map(
            {kUtf16LeaseBegin, kUtf16LeaseSize},
            memory::PageProtection::read | memory::PageProtection::write);
    }

    ~Utf16Leases() {
        std::scoped_lock lock(mutex_);
        try {
            address_space_->Unmap({kUtf16LeaseBegin, kUtf16LeaseSize});
        } catch (...) {
        }
    }

    [[nodiscard]] std::uint32_t Acquire(const JniGuestCallFrame& frame) {
        const auto string = ResolveString(*environment_, frame);
        const auto is_copy = memory::GuestAddress{frame.registers[2]};
        if (!is_copy.IsNull()) {
            address_space_->Validate(
                {is_copy, 1U}, memory::AccessType::write, frame.thread_id);
        }
        auto access = strings_->Acquire(string, JniStringAccessKind::chars);
        const auto bytes = EncodeUtf16(access.chars, true);
        std::scoped_lock lock(mutex_);
        try {
            const auto pointer = Allocate(bytes.size());
            address_space_->Write(pointer, bytes, frame.thread_id);
            if (!is_copy.IsNull()) {
                address_space_->Write8(is_copy, 1U, frame.thread_id);
            }
            leases_.push_back(
                {pointer, bytes.size(), string, access.token});
            return pointer.Value();
        } catch (...) {
            strings_->Release(string, access.token,
                              JniStringAccessKind::chars);
            throw;
        }
    }

    void Release(const JniGuestCallFrame& frame) {
        const auto string = ResolveString(*environment_, frame);
        const auto pointer = memory::GuestAddress{frame.registers[2]};
        std::scoped_lock lock(mutex_);
        const auto found = std::ranges::find_if(
            leases_, [pointer](const Lease& lease) {
                return lease.pointer == pointer;
            });
        if (found == leases_.end() || found->string != string) {
            throw JniGuestBindingError(
                "ReleaseStringChars pointer does not match an active lease");
        }
        strings_->Release(found->string, found->token,
                          JniStringAccessKind::chars);
        leases_.erase(found);
    }

private:
    struct Lease final {
        memory::GuestAddress pointer;
        std::size_t size{};
        JniObjectIdentity string;
        std::uint64_t token{};
    };

    [[nodiscard]] memory::GuestAddress Allocate(const std::size_t size) const {
        if (size == 0 || size > kUtf16LeaseSize) {
            throw JniGuestBindingError(
                "JNI guest UTF-16 lease exceeds its arena");
        }
        auto ordered = leases_;
        std::ranges::sort(ordered, {}, &Lease::pointer);
        std::uint64_t offset{};
        for (const auto& lease : ordered) {
            const auto lease_offset =
                lease.pointer.Value() - kUtf16LeaseBegin.Value();
            if (size <= lease_offset - offset) break;
            offset = lease_offset + lease.size;
        }
        if (offset > kUtf16LeaseSize ||
            size > kUtf16LeaseSize - offset) {
            throw JniGuestBindingError(
                "JNI guest UTF-16 lease arena is full");
        }
        return kUtf16LeaseBegin.Add(offset);
    }

    JniEnvironment* environment_{};
    JniStringStore* strings_{};
    memory::AddressSpace* address_space_{};
    mutable std::mutex mutex_;
    std::vector<Lease> leases_;
};

}  // namespace

void BindJniGuestModifiedUtf8Slots(
    JniGuestCallDispatcher& dispatcher, JniEnvironment& environment,
    JniStringStore& strings, memory::AddressSpace& address_space) {
    const auto leases = std::make_shared<ModifiedUtf8Leases>(
        environment, strings, address_space);
    dispatcher.BindEnvironment(
        Slot("GetStringUTFLength"),
        [&environment, &strings](const JniGuestCallFrame& frame) {
            return Int(strings.ModifiedUtf8Length(
                ResolveString(environment, frame)));
        });
    dispatcher.BindEnvironment(
        Slot("GetStringUTFChars"),
        [leases](const JniGuestCallFrame& frame) {
            return Word(leases->Acquire(frame));
        });
    dispatcher.BindEnvironment(
        Slot("ReleaseStringUTFChars"),
        [leases](const JniGuestCallFrame& frame) {
            leases->Release(frame);
            return JniGuestCallResult{};
        });
    dispatcher.BindEnvironment(
        Slot("GetStringUTFRegion"),
        [&environment, &strings,
         &address_space](const JniGuestCallFrame& frame) {
            const auto string = ResolveString(environment, frame);
            const auto start = std::bit_cast<JniSize>(frame.registers[2]);
            const auto length = std::bit_cast<JniSize>(frame.registers[3]);
            const auto bytes = strings.ModifiedUtf8Region(
                string, start, length);
            const auto destination = memory::GuestAddress{
                Read32(address_space, frame.stack_pointer, frame.thread_id)};
            if (destination.IsNull() && !bytes.empty()) {
                throw JniGuestBindingError(
                    "GetStringUTFRegion requires a non-null output buffer");
            }
            address_space.Write(
                destination, std::as_bytes(std::span{bytes}), frame.thread_id);
            return JniGuestCallResult{};
        });
}

void BindJniGuestUtf16Slots(
    JniGuestCallDispatcher& dispatcher, JniEnvironment& environment,
    JniStringStore& strings, memory::AddressSpace& address_space) {
    const auto leases = std::make_shared<Utf16Leases>(
        environment, strings, address_space);
    dispatcher.BindEnvironment(
        Slot("NewString"),
        [&environment, &strings,
         &address_space](const JniGuestCallFrame& frame) {
            const auto length = std::bit_cast<JniSize>(frame.registers[2]);
            if (length < 0 ||
                static_cast<std::size_t>(length) >
                    kMaximumUtf16CodeUnits) {
                throw JniGuestBindingError(
                    "NewString length is invalid");
            }
            const auto count = static_cast<std::size_t>(length);
            const auto byte_size = count * sizeof(JniChar);
            const auto source = memory::GuestAddress{frame.registers[1]};
            std::vector<std::byte> bytes(byte_size);
            if (!bytes.empty()) {
                if (source.IsNull()) {
                    throw JniGuestBindingError(
                        "NewString requires non-null UTF-16 input");
                }
                address_space.Validate(
                    {source, byte_size}, memory::AccessType::read,
                    frame.thread_id);
                address_space.Read(source, bytes, frame.thread_id);
            }
            std::vector<JniChar> chars(count);
            for (std::size_t index = 0; index < count; ++index) {
                chars[index] = static_cast<JniChar>(
                    std::to_integer<std::uint16_t>(bytes[index * 2U]) |
                    (std::to_integer<std::uint16_t>(
                         bytes[index * 2U + 1U])
                     << 8U));
            }
            const auto identity = strings.Create(chars);
            try {
                const auto reference = environment.PublishLocalObject(
                    frame.thread_id, identity);
                return Word(reference.Value());
            } catch (...) {
                strings.Delete(identity);
                throw;
            }
        });
    dispatcher.BindEnvironment(
        Slot("GetStringLength"),
        [&environment, &strings](const JniGuestCallFrame& frame) {
            return Int(strings.Length(ResolveString(environment, frame)));
        });
    dispatcher.BindEnvironment(
        Slot("GetStringChars"),
        [leases](const JniGuestCallFrame& frame) {
            return Word(leases->Acquire(frame));
        });
    dispatcher.BindEnvironment(
        Slot("ReleaseStringChars"),
        [leases](const JniGuestCallFrame& frame) {
            leases->Release(frame);
            return JniGuestCallResult{};
        });
    dispatcher.BindEnvironment(
        Slot("GetStringRegion"),
        [&environment, &strings,
         &address_space](const JniGuestCallFrame& frame) {
            const auto start = std::bit_cast<JniSize>(frame.registers[2]);
            const auto length = std::bit_cast<JniSize>(frame.registers[3]);
            if (start < 0 || length < 0) {
                throw JniGuestBindingError(
                    "GetStringRegion range is invalid");
            }
            const auto byte_size =
                static_cast<std::size_t>(length) * sizeof(JniChar);
            const auto destination = memory::GuestAddress{
                Read32(address_space, frame.stack_pointer, frame.thread_id)};
            if (byte_size != 0U) {
                if (destination.IsNull()) {
                    throw JniGuestBindingError(
                        "GetStringRegion requires a non-null output buffer");
                }
                address_space.Validate(
                    {destination, byte_size}, memory::AccessType::write,
                    frame.thread_id);
            }
            const auto chars = strings.Region(
                ResolveString(environment, frame), start, length);
            if (!chars.empty()) {
                address_space.Write(
                    destination, EncodeUtf16(chars, false), frame.thread_id);
            }
            return JniGuestCallResult{};
        });
}

}  // namespace ogplay::runtime
