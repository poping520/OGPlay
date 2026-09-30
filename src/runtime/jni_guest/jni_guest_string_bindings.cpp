#include "ogplay/runtime/jni_guest/jni_guest_string_bindings.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include <optional>

#include "ogplay/memory/address_space.h"
#include "ogplay/runtime/jni_guest/jni_guest_bindings.h"
#include "ogplay/runtime/jni_guest/jni_guest_dispatch.h"
#include "ogplay/runtime/jni/jni.h"
#include "ogplay/runtime/jni/jni_environment.h"
#include "jni_guest_memory.h"
#include "ogplay/runtime/jni/jni_object.h"

namespace ogplay::runtime {
namespace {

// Shares the native mmap address-space ledger and skips all occupied pages.
const memory::GuestRange kStringCopyBounds{memory::GuestAddress{0x60000000U}, 0xa0000000ULL};

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

[[nodiscard]] JniObjectIdentity ResolveString(
    JniEnvironment& environment, const JniGuestCallFrame& frame,
    const std::string_view operation) {
    const auto identity = environment.ResolveObjectForHle(
        frame.thread_id, JniReference{frame.registers[1]});
    if (!identity.has_value()) {
        throw JniGuestBindingError(
            std::string(operation) +
            " requires a non-null valid JNI string reference");
    }
    return *identity;
}

struct GuestStringLease final {
    memory::GuestAddress pointer;
    std::size_t size{};
    JniObjectIdentity string;
    std::uint64_t token{};
};

class GuestStringLeaseArena final {
public:
    using Iterator = std::vector<GuestStringLease>::iterator;

    class Budget final {
    public:
        explicit Budget(const std::size_t limit) : limit_(limit) {}
        void Reserve(const std::size_t bytes) {
            std::scoped_lock lock(mutex_);
            if (bytes > limit_ - used_)
                throw JniGuestBindingError("JNI string copy resource budget exhausted: requested_bytes=" +
                    std::to_string(bytes) + " used_bytes=" + std::to_string(used_) + " budget=" + std::to_string(limit_));
            used_ += bytes;
        }
        void Release(const std::size_t bytes) noexcept {
            std::scoped_lock lock(mutex_); used_ -= bytes;
        }
    private:
        std::mutex mutex_;
        std::size_t limit_{}, used_{};
    };

    GuestStringLeaseArena(memory::AddressSpace& space, std::shared_ptr<Budget> budget)
        : space_(&space), budget_(std::move(budget)) {}

    ~GuestStringLeaseArena() {
        for (const auto& lease : leases_) Discard(lease.pointer, lease.size);
    }

    [[nodiscard]] memory::GuestAddress Allocate(
        const std::size_t requested) const {
        const auto mapped = MappedSize(requested);
        budget_->Reserve(mapped);
        try {
            return space_->MapAnywhere(kStringCopyBounds, mapped,
                memory::PageProtection::read | memory::PageProtection::write);
        } catch (const std::bad_alloc&) {
            budget_->Release(mapped);
            throw JniGuestBindingError("JNI string copy allocation failed");
        } catch (...) {
            budget_->Release(mapped); throw;
        }
    }

    void Discard(const memory::GuestAddress pointer, const std::size_t size) noexcept {
        const auto mapped = MappedSize(size);
        try { space_->Unmap({pointer, mapped}); } catch (...) {}
        budget_->Release(mapped);
    }

    void Publish(const memory::GuestAddress pointer, const std::size_t size,
                 const JniObjectIdentity string, const std::uint64_t token) {
        leases_.push_back({pointer, size, string, token});
    }

    [[nodiscard]] Iterator Require(
        const memory::GuestAddress pointer, const JniObjectIdentity string,
        const std::string_view mismatch_error) {
        const auto found = std::ranges::find_if(
            leases_, [pointer](const GuestStringLease& lease) {
                return lease.pointer == pointer;
            });
        if (found == leases_.end() || found->string != string) {
            throw JniGuestBindingError(std::string(mismatch_error));
        }
        return found;
    }

    [[nodiscard]] Iterator RequirePointer(
        const memory::GuestAddress pointer,
        const std::string_view mismatch_error) {
        const auto found = std::ranges::find_if(
            leases_, [pointer](const GuestStringLease& lease) {
                return lease.pointer == pointer;
            });
        if (found == leases_.end()) {
            throw JniGuestBindingError(std::string(mismatch_error));
        }
        return found;
    }

    void Erase(const Iterator lease) {
        const auto mapped = MappedSize(lease->size);
        space_->Unmap({lease->pointer, mapped});
        budget_->Release(mapped);
        leases_.erase(lease);
    }

private:
    [[nodiscard]] std::size_t MappedSize(const std::size_t size) const noexcept {
        const auto page = static_cast<std::size_t>(space_->PageSize());
        return ((size + page - 1U) / page) * page;
    }
    memory::AddressSpace* space_;
    std::shared_ptr<Budget> budget_;
    std::vector<GuestStringLease> leases_;
};

class ModifiedUtf8Leases final {
public:
    ModifiedUtf8Leases(JniEnvironment& environment, JniStringStore& strings,
                       memory::AddressSpace& address_space, const JniGuestStringLimits limits,
                       std::shared_ptr<GuestStringLeaseArena::Budget> budget)
        : environment_(&environment), strings_(&strings),
          address_space_(&address_space), limits_(limits), arena_(address_space, std::move(budget)) {}

    [[nodiscard]] std::uint32_t Acquire(const JniGuestCallFrame& frame) {
        const auto string = ResolveString(
            *environment_, frame, "GetStringUTFChars");
        const auto is_copy = memory::GuestAddress{frame.registers[2]};
        if (!is_copy.IsNull()) address_space_->Validate({is_copy, 1U}, memory::AccessType::write, frame.thread_id);
        const auto length = static_cast<std::size_t>(strings_->ModifiedUtf8Length(string));
        if (length > limits_.maximum_modified_utf8_bytes)
            throw JniGuestBindingError("JNI string modified UTF-8 resource budget exceeded");
        std::scoped_lock lock(mutex_);
        const auto requested = length + 1U;
        const auto pointer = arena_.Allocate(requested);
        std::optional<JniStringAccess> access;
        try {
            access = strings_->Acquire(string, JniStringAccessKind::modified_utf8);
            access->modified_utf8.push_back(0U);
            address_space_->Write(
                pointer,
                std::as_bytes(std::span{access->modified_utf8}),
                frame.thread_id);
            if (frame.registers[2] != 0U) {
                const std::byte copied{1};
                address_space_->Write(
                    memory::GuestAddress{frame.registers[2]},
                    std::span{&copied, 1}, frame.thread_id);
            }
            arena_.Publish(pointer, requested, string, access->token);
            return pointer.Value();
        } catch (...) {
            arena_.Discard(pointer, requested);
            if (access) strings_->Release(string, access->token, JniStringAccessKind::modified_utf8);
            throw;
        }
    }

    void Release(const JniGuestCallFrame& frame) {
        const auto pointer = memory::GuestAddress{frame.registers[2]};
        if (pointer.IsNull()) return;
        std::scoped_lock lock(mutex_);
        const auto found = arena_.RequirePointer(
            pointer,
            "ReleaseStringUTFChars pointer does not match an active lease");
        strings_->Release(found->string, found->token,
                          JniStringAccessKind::modified_utf8);
        arena_.Erase(found);
    }

private:
    JniEnvironment* environment_{};
    JniStringStore* strings_{};
    memory::AddressSpace* address_space_{};
    mutable std::mutex mutex_;
    JniGuestStringLimits limits_;
    GuestStringLeaseArena arena_;
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
                memory::AddressSpace& address_space, const JniGuestStringLimits limits,
                std::shared_ptr<GuestStringLeaseArena::Budget> budget)
        : environment_(&environment), strings_(&strings),
          address_space_(&address_space), limits_(limits), arena_(address_space, std::move(budget)) {}

    [[nodiscard]] std::uint32_t Acquire(const JniGuestCallFrame& frame) {
        const auto string = ResolveString(*environment_, frame,
                                          "GetStringChars");
        const auto is_copy = memory::GuestAddress{frame.registers[2]};
        if (!is_copy.IsNull()) {
            address_space_->Validate(
                {is_copy, 1U}, memory::AccessType::write, frame.thread_id);
        }
        const auto length = static_cast<std::size_t>(strings_->Length(string));
        if (length > limits_.maximum_utf16_code_units)
            throw JniGuestBindingError("JNI string UTF-16 resource budget exceeded");
        std::scoped_lock lock(mutex_);
        const auto requested = (length + 1U) * sizeof(JniChar);
        const auto pointer = arena_.Allocate(requested);
        std::optional<JniStringAccess> access;
        try {
            access = strings_->Acquire(string, JniStringAccessKind::chars);
            const auto bytes = EncodeUtf16(access->chars, true);
            address_space_->Write(pointer, bytes, frame.thread_id);
            if (!is_copy.IsNull()) {
                address_space_->Write8(is_copy, 1U, frame.thread_id);
            }
            arena_.Publish(pointer, requested, string, access->token);
            return pointer.Value();
        } catch (...) {
            arena_.Discard(pointer, requested);
            if (access) strings_->Release(string, access->token, JniStringAccessKind::chars);
            throw;
        }
    }

    void Release(const JniGuestCallFrame& frame) {
        const auto string = ResolveString(*environment_, frame,
                                          "ReleaseStringChars");
        const auto pointer = memory::GuestAddress{frame.registers[2]};
        std::scoped_lock lock(mutex_);
        const auto found = arena_.Require(
            pointer, string,
            "ReleaseStringChars pointer does not match an active lease");
        strings_->Release(found->string, found->token,
                          JniStringAccessKind::chars);
        arena_.Erase(found);
    }

private:
    JniEnvironment* environment_{};
    JniStringStore* strings_{};
    memory::AddressSpace* address_space_{};
    mutable std::mutex mutex_;
    JniGuestStringLimits limits_;
    GuestStringLeaseArena arena_;
};

}  // namespace

void BindJniGuestStringSlots(
    JniGuestCallDispatcher& dispatcher, JniEnvironment& environment,
    JniStringStore& strings, memory::AddressSpace& address_space,
    const JniGuestStringLimits limits) {
    ValidateJniGuestStringLimits(limits);
    const auto budget = std::make_shared<GuestStringLeaseArena::Budget>(limits.maximum_copy_bytes);
    const auto leases = std::make_shared<ModifiedUtf8Leases>(
        environment, strings, address_space, limits, budget);
    dispatcher.BindEnvironment(
        Slot("GetStringUTFLength"),
        [&environment, &strings](const JniGuestCallFrame& frame) {
            if (frame.registers[1] == 0U) return Int(0);
            return Int(strings.ModifiedUtf8Length(
                ResolveString(environment, frame, "GetStringUTFLength")));
        });
    dispatcher.BindEnvironment(
        Slot("GetStringUTFChars"),
        [leases](const JniGuestCallFrame& frame) {
            if (frame.registers[1] == 0U) return Word(0U);
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
            const auto string = ResolveString(
                environment, frame, "GetStringUTFRegion");
            const auto start = std::bit_cast<JniSize>(frame.registers[2]);
            const auto length = std::bit_cast<JniSize>(frame.registers[3]);
            const auto bytes = strings.ModifiedUtf8Region(
                string, start, length);
            const auto destination = memory::GuestAddress{
                ReadGuest32(address_space, frame.stack_pointer, frame.thread_id)};
            if (destination.IsNull() && !bytes.empty()) {
                throw JniGuestBindingError(
                    "GetStringUTFRegion requires a non-null output buffer");
            }
            address_space.Write(
                destination, std::as_bytes(std::span{bytes}), frame.thread_id);
            return JniGuestCallResult{};
        });
    const auto utf16_leases = std::make_shared<Utf16Leases>(
        environment, strings, address_space, limits, budget);
    dispatcher.BindEnvironment(
        Slot("NewString"),
        [&environment, &strings,
         &address_space, limits](const JniGuestCallFrame& frame) {
            const auto length = std::bit_cast<JniSize>(frame.registers[2]);
            if (length < 0) throw JniGuestBindingError("NewString length is invalid");
            if (static_cast<std::size_t>(length) > limits.maximum_utf16_code_units)
                throw JniGuestBindingError("JNI string UTF-16 resource budget exceeded");
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
            return Int(strings.Length(
                ResolveString(environment, frame, "GetStringLength")));
        });
    dispatcher.BindEnvironment(
        Slot("GetStringChars"),
        [utf16_leases](const JniGuestCallFrame& frame) {
            return Word(utf16_leases->Acquire(frame));
        });
    dispatcher.BindEnvironment(
        Slot("ReleaseStringChars"),
        [utf16_leases](const JniGuestCallFrame& frame) {
            utf16_leases->Release(frame);
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
                ReadGuest32(address_space, frame.stack_pointer, frame.thread_id)};
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
                ResolveString(environment, frame, "GetStringRegion"),
                start, length);
            if (!chars.empty()) {
                address_space.Write(
                    destination, EncodeUtf16(chars, false), frame.thread_id);
            }
            return JniGuestCallResult{};
        });
}

}  // namespace ogplay::runtime
