#include "ogplay/memory/address_space.h"

#include <algorithm>
#include <cstring>
#include <mutex>
#include <utility>
#include <vector>

#include "ogplay/hal/virtual_memory.h"

namespace ogplay::memory {
namespace {

[[nodiscard]] bool HasProtection(const PageProtection value,
                                 const PageProtection flag) noexcept {
    return (static_cast<std::uint8_t>(value) & static_cast<std::uint8_t>(flag)) != 0;
}

void ValidateProtection(const PageProtection protection) {
    if (protection == PageProtection::none) {
        throw std::invalid_argument("mapped pages require at least one permission");
    }
    if (HasProtection(protection, PageProtection::write) &&
        !HasProtection(protection, PageProtection::read)) {
        throw std::invalid_argument("writable guest pages must also be readable");
    }
}

[[nodiscard]] hal::MemoryProtection ToHostProtection(const PageProtection protection) {
    auto result = hal::MemoryProtection::none;
    if (HasProtection(protection, PageProtection::read)) {
        result = result | hal::MemoryProtection::read;
    }
    if (HasProtection(protection, PageProtection::write)) {
        result = result | hal::MemoryProtection::write;
    }
    if (HasProtection(protection, PageProtection::execute)) {
        result = result | hal::MemoryProtection::execute;
    }
    return result;
}

[[nodiscard]] bool Allows(const PageProtection protection, const AccessType access) {
    switch (access) {
    case AccessType::read: return HasProtection(protection, PageProtection::read);
    case AccessType::write: return HasProtection(protection, PageProtection::write);
    case AccessType::execute: return HasProtection(protection, PageProtection::execute);
    }
    return false;
}

}  // namespace

MemoryFault::MemoryFault(const GuestAddress address, const AccessType access,
                         const FaultReason reason, const std::uint64_t thread_id)
    : std::runtime_error(reason == FaultReason::unmapped ? "guest memory is unmapped"
                                                         : "guest memory permission denied"),
      address_(address), access_(access), reason_(reason), thread_id_(thread_id) {}

class AddressSpace::Impl final {
public:
    Impl() : reservation_(hal::ReserveVirtualMemory(kGuestAddressSpaceSize)) {
        page_size_ = reservation_->PageSize();
        pages_.resize(static_cast<std::size_t>(kGuestAddressSpaceSize / page_size_),
                      PageProtection::none);
    }

    [[nodiscard]] std::uint64_t ReservedSize() const noexcept { return reservation_->Size(); }
    [[nodiscard]] std::uint64_t PageSize() const noexcept { return page_size_; }

    void Map(const GuestRange& range, const PageProtection protection) {
        ValidateProtection(protection);
        ValidatePageRange(range);
        if (range.Overlaps(LowAddressGuard())) {
            throw std::invalid_argument("low guest address guard cannot be mapped");
        }
        std::scoped_lock lock(mutex_);
        const auto [first, last] = PageIndexes(range);
        if (std::any_of(pages_.begin() + first, pages_.begin() + last,
                        [](const auto page) { return page != PageProtection::none; })) {
            throw std::logic_error("guest memory range overlaps an existing mapping");
        }
        reservation_->Commit(range.Start().Value(), range.Size(),
                             ToHostProtection(protection));
        std::fill(pages_.begin() + first, pages_.begin() + last, protection);
    }

    void Protect(const GuestRange& range, const PageProtection protection) {
        ValidateProtection(protection);
        ValidatePageRange(range);
        std::scoped_lock lock(mutex_);
        const auto [first, last] = PageIndexes(range);
        RequireMapped(range, first, last, AccessType::read, 0);
        reservation_->Protect(range.Start().Value(), range.Size(),
                              ToHostProtection(protection));
        std::fill(pages_.begin() + first, pages_.begin() + last, protection);
    }

    void Unmap(const GuestRange& range) {
        ValidatePageRange(range);
        std::scoped_lock lock(mutex_);
        const auto [first, last] = PageIndexes(range);
        RequireMapped(range, first, last, AccessType::read, 0);
        reservation_->Decommit(range.Start().Value(), range.Size());
        std::fill(pages_.begin() + first, pages_.begin() + last, PageProtection::none);
    }

    void Validate(const GuestRange& range, const AccessType access,
                  const std::uint64_t thread_id) const {
        std::scoped_lock lock(mutex_);
        ValidateLocked(range, access, thread_id);
    }

    void Read(const GuestAddress address, const std::span<std::byte> destination,
              const std::uint64_t thread_id) const {
        if (destination.empty()) return;
        const GuestRange range(address, destination.size());
        std::scoped_lock lock(mutex_);
        ValidateLocked(range, AccessType::read, thread_id);
        std::memcpy(destination.data(), reservation_->Base() + address.Value(),
                    destination.size());
    }

    void Write(const GuestAddress address, const std::span<const std::byte> source,
               const std::uint64_t thread_id) {
        if (source.empty()) return;
        const GuestRange range(address, source.size());
        std::scoped_lock lock(mutex_);
        ValidateLocked(range, AccessType::write, thread_id);
        std::memcpy(reservation_->Base() + address.Value(), source.data(), source.size());
    }

    [[nodiscard]] MemorySnapshot CaptureSnapshot() const {
        std::scoped_lock lock(mutex_);
        MemorySnapshot snapshot;
        snapshot.page_size = page_size_;

        std::size_t first = 0;
        while (first < pages_.size()) {
            const auto protection = pages_[first];
            if (protection == PageProtection::none) {
                ++first;
                continue;
            }
            if (!HasProtection(protection, PageProtection::read)) {
                throw std::logic_error("execute-only guest memory cannot be captured");
            }
            auto last = first + 1;
            while (last < pages_.size() && pages_[last] == protection) ++last;

            const auto offset = static_cast<std::uint64_t>(first) * page_size_;
            const auto size = static_cast<std::uint64_t>(last - first) * page_size_;
            MemorySnapshotMapping mapping{
                GuestRange(GuestAddress(static_cast<std::uint32_t>(offset)), size),
                protection,
                std::vector<std::byte>(static_cast<std::size_t>(size)),
            };
            std::memcpy(mapping.data.data(), reservation_->Base() + offset,
                        mapping.data.size());
            snapshot.mappings.push_back(std::move(mapping));
            first = last;
        }
        return snapshot;
    }

    void RestoreSnapshot(const MemorySnapshot& snapshot) {
        ValidateSnapshot(snapshot);
        auto replacement = hal::ReserveVirtualMemory(kGuestAddressSpaceSize);
        if (replacement->PageSize() != page_size_) {
            throw std::runtime_error("replacement virtual memory page size changed");
        }
        std::vector<PageProtection> replacement_pages(pages_.size(),
                                                      PageProtection::none);

        const auto writable = PageProtection::read | PageProtection::write;
        for (const auto& mapping : snapshot.mappings) {
            const auto offset = mapping.range.Start().Value();
            replacement->Commit(offset, mapping.range.Size(),
                                ToHostProtection(writable));
            std::memcpy(replacement->Base() + offset, mapping.data.data(),
                        mapping.data.size());
            if (mapping.protection != writable) {
                replacement->Protect(offset, mapping.range.Size(),
                                     ToHostProtection(mapping.protection));
            }
            const auto first = static_cast<std::size_t>(offset / page_size_);
            const auto last = static_cast<std::size_t>(mapping.range.EndExclusive() /
                                                       page_size_);
            std::fill(replacement_pages.begin() + first,
                      replacement_pages.begin() + last, mapping.protection);
        }

        std::scoped_lock lock(mutex_);
        reservation_.swap(replacement);
        pages_.swap(replacement_pages);
    }

private:
    using PageIterator = std::vector<PageProtection>::difference_type;

    void ValidatePageRange(const GuestRange& range) const {
        if (!range.IsAligned(page_size_)) {
            throw std::invalid_argument("guest mapping range must be page aligned");
        }
    }

    void ValidateSnapshot(const MemorySnapshot& snapshot) const {
        if (snapshot.version != kMemorySnapshotVersion) {
            throw std::invalid_argument("unsupported memory snapshot version");
        }
        if (snapshot.page_size != page_size_) {
            throw std::invalid_argument("memory snapshot page size does not match host");
        }
        std::uint64_t previous_end = LowAddressGuard().EndExclusive();
        for (const auto& mapping : snapshot.mappings) {
            ValidateProtection(mapping.protection);
            ValidatePageRange(mapping.range);
            if (!HasProtection(mapping.protection, PageProtection::read)) {
                throw std::invalid_argument("memory snapshot contains execute-only mapping");
            }
            if (mapping.range.Start().Value() < previous_end) {
                throw std::invalid_argument(
                    "memory snapshot mappings overlap, are unordered, or enter low guard");
            }
            if (mapping.range.Size() != mapping.data.size()) {
                throw std::invalid_argument("memory snapshot mapping size is inconsistent");
            }
            previous_end = mapping.range.EndExclusive();
        }
    }

    [[nodiscard]] std::pair<PageIterator, PageIterator> PageIndexes(
        const GuestRange& range) const {
        const auto first = static_cast<PageIterator>(range.Start().Value() / page_size_);
        const auto last = static_cast<PageIterator>(range.EndExclusive() / page_size_);
        return {first, last};
    }

    void RequireMapped(const GuestRange& range, const PageIterator first,
                       const PageIterator last, const AccessType access,
                       const std::uint64_t thread_id) const {
        const auto missing = std::find(pages_.begin() + first, pages_.begin() + last,
                                       PageProtection::none);
        if (missing == pages_.begin() + last) return;
        const auto page_index = static_cast<std::uint64_t>(missing - pages_.begin());
        const auto fault = std::max<std::uint64_t>(range.Start().Value(),
                                                   page_index * page_size_);
        throw MemoryFault(GuestAddress(static_cast<std::uint32_t>(fault)), access,
                          FaultReason::unmapped, thread_id);
    }

    void ValidateLocked(const GuestRange& range, const AccessType access,
                        const std::uint64_t thread_id) const {
        const auto first = range.Start().Value() / page_size_;
        const auto last = (range.EndExclusive() - 1U) / page_size_;
        for (auto page = first; page <= last; ++page) {
            const auto protection = pages_[static_cast<std::size_t>(page)];
            const auto fault_address = std::max<std::uint64_t>(
                range.Start().Value(), static_cast<std::uint64_t>(page) * page_size_);
            if (protection == PageProtection::none) {
                throw MemoryFault(GuestAddress(static_cast<std::uint32_t>(fault_address)),
                                  access, FaultReason::unmapped, thread_id);
            }
            if (!Allows(protection, access)) {
                throw MemoryFault(GuestAddress(static_cast<std::uint32_t>(fault_address)),
                                  access, FaultReason::permission_denied, thread_id);
            }
        }
    }

    std::unique_ptr<hal::VirtualMemoryReservation> reservation_;
    std::uint64_t page_size_{};
    std::vector<PageProtection> pages_;
    mutable std::mutex mutex_;
};

AddressSpace::AddressSpace() : impl_(std::make_unique<Impl>()) {}
AddressSpace::~AddressSpace() = default;
AddressSpace::AddressSpace(AddressSpace&&) noexcept = default;
AddressSpace& AddressSpace::operator=(AddressSpace&&) noexcept = default;

std::uint64_t AddressSpace::ReservedSize() const noexcept { return impl_->ReservedSize(); }
std::uint64_t AddressSpace::PageSize() const noexcept { return impl_->PageSize(); }
void AddressSpace::Map(const GuestRange& range, const PageProtection protection) {
    impl_->Map(range, protection);
}
void AddressSpace::Protect(const GuestRange& range, const PageProtection protection) {
    impl_->Protect(range, protection);
}
void AddressSpace::Unmap(const GuestRange& range) { impl_->Unmap(range); }
void AddressSpace::Validate(const GuestRange& range, const AccessType access,
                            const std::uint64_t thread_id) const {
    impl_->Validate(range, access, thread_id);
}
void AddressSpace::Read(const GuestAddress address,
                        const std::span<std::byte> destination,
                        const std::uint64_t thread_id) const {
    impl_->Read(address, destination, thread_id);
}
void AddressSpace::Write(const GuestAddress address,
                         const std::span<const std::byte> source,
                         const std::uint64_t thread_id) {
    impl_->Write(address, source, thread_id);
}
MemorySnapshot AddressSpace::CaptureSnapshot() const { return impl_->CaptureSnapshot(); }
void AddressSpace::RestoreSnapshot(const MemorySnapshot& snapshot) {
    impl_->RestoreSnapshot(snapshot);
}

}  // namespace ogplay::memory
