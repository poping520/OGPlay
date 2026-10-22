#include "ogplay/runtime/jni/host_copy_payload_provider.h"
#include <algorithm>
#include <map>
#include <mutex>
#include <vector>

namespace ogplay::runtime {
class HostCopyJavaPayloadProvider::Impl final {
public:
    struct Entry {
        std::shared_ptr<payload_detail::Block> block;
        std::vector<std::uint64_t> storage;
        std::size_t pins{};
    };

    std::map<std::uint64_t, Entry> entries;
    std::map<std::uint64_t, std::uint64_t> borrows;
    std::uint64_t generation{}, token{};
    JavaPayloadStatistics stats;
    mutable std::mutex mutex;
};

HostCopyJavaPayloadProvider::HostCopyJavaPayloadProvider() : impl_(std::make_shared<Impl>()) {}

HostCopyJavaPayloadProvider::~HostCopyJavaPayloadProvider() = default;

const void* HostCopyJavaPayloadProvider::OwnerIdentity() const noexcept {
    return impl_.get();
}

JavaPayloadHandle HostCopyJavaPayloadProvider::Allocate(std::size_t bytes) {
    std::scoped_lock lock(impl_->mutex);
    if (bytes > SIZE_MAX - 7) throw std::bad_alloc();
    auto block = std::make_shared<payload_detail::Block>();
    block->owner = impl_.get();
    block->owner_lifetime = impl_;
    block->bytes = bytes;
    block->guest_address = false;
    block->generation = ++impl_->generation;
    Impl::Entry entry{block, std::vector<std::uint64_t>(std::max<std::size_t>(1, (bytes + 7) / 8)), 0};
    block->base = reinterpret_cast<std::byte*>(entry.storage.data());
    impl_->entries.emplace(block->generation, std::move(entry));
    impl_->stats.allocated_bytes += bytes;
    ++impl_->stats.allocation_count;
    return MakeHandle(std::move(block));
}

void HostCopyJavaPayloadProvider::Release(const JavaPayloadHandle& handle) {
    std::scoped_lock lock(impl_->mutex);
    const auto& block = Block(handle);
    const auto found = impl_->entries.find(block->generation);
    if (found == impl_->entries.end() || found->second.pins || block->accessors.load() != 0)
        throw std::invalid_argument("host-copy payload is accessed, pinned or retired");
    block->alive.store(false);
    impl_->stats.allocated_bytes -= block->bytes;
    --impl_->stats.allocation_count;
    impl_->entries.erase(found);
}

JavaPayloadBorrow HostCopyJavaPayloadProvider::Pin(const JavaPayloadHandle& handle) {
    std::scoped_lock lock(impl_->mutex);
    const auto& block = Block(handle);
    const auto token = ++impl_->token;
    impl_->borrows.emplace(token, block->generation);
    if (impl_->entries.at(block->generation).pins++ == 0) impl_->stats.pinned_bytes += block->bytes;
    ++impl_->stats.pin_count;
    return MakeBorrow(handle, token);
}

void HostCopyJavaPayloadProvider::Unpin(const JavaPayloadBorrow& borrow) {
    std::scoped_lock lock(impl_->mutex);
    const auto& block = Block(BorrowPayload(borrow));
    const auto found = impl_->borrows.find(BorrowId(borrow));
    if (found == impl_->borrows.end() || found->second != block->generation)
        throw std::invalid_argument("invalid host-copy borrow");
    if (--impl_->entries.at(block->generation).pins == 0) impl_->stats.pinned_bytes -= block->bytes;
    --impl_->stats.pin_count;
    impl_->borrows.erase(found);
}

JavaPayloadStatistics HostCopyJavaPayloadProvider::Statistics() const {
    std::scoped_lock lock(impl_->mutex);
    return impl_->stats;
}
}  // namespace ogplay::runtime
