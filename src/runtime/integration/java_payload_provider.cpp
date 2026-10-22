#include "ogplay/runtime/integration/java_payload_provider.h"

#include <algorithm>
#include <limits>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace ogplay::runtime {
namespace {
constexpr std::uint64_t kAlignment = 8;
constexpr memory::GuestRange kPayloadBounds{memory::GuestAddress{memory::kGuestLowGuardSize},
                                            memory::kGuestAddressSpaceSize - memory::kGuestLowGuardSize};

std::uint64_t Align(std::uint64_t value, std::uint64_t alignment) {
    if (value > UINT64_MAX - alignment + 1) throw std::bad_alloc();
    return (value + alignment - 1) & ~(alignment - 1);
}
}  // namespace

class GuestJavaPayloadProvider::Impl final {
public:
    struct Arena final {
        memory::GuestAddress address;
        std::uint64_t bytes{};
        bool small{};
        std::vector<bool> used;
        std::size_t live{};
    };

    struct Allocation final {
        std::shared_ptr<payload_detail::Block> block;
        Arena* arena{};
        std::size_t unit{}, units{}, pins{};
    };

    Impl(memory::AddressSpace& space, core::CapabilityLedger& ledger, JavaPayloadLimits limits)
        : space(space), ledger(ledger), limits(limits) {
        if (!limits.initial_heap_target_bytes || limits.initial_heap_target_bytes > limits.heap_growth_limit_bytes ||
            limits.heap_growth_limit_bytes > limits.maximum_heap_bytes ||
            limits.maximum_heap_bytes > memory::kGuestAddressSpaceSize || limits.slab_bytes < space.BackingPageSize() ||
            limits.slab_bytes % space.BackingPageSize() || (limits.slab_bytes & (limits.slab_bytes - 1)) != 0)
            throw std::invalid_argument("invalid Java payload limits");
        stats.heap_target_bytes = limits.initial_heap_target_bytes;
        owner = space.RegisterProtectedOwner([&ledger](std::string_view operation) {
            ledger.RecordUnimplemented("memory.java_payload.protected." + std::string(operation), 0);
        });
    }

    ~Impl() {
        for (auto& [id, a] : blocks) {
            static_cast<void>(id);
            a.block->alive.store(false, std::memory_order_relaxed);
        }
        for (const auto& arena : arenas)
            space.UnmapProtected(owner, {arena->address, arena->bytes});
    }

    std::uint64_t ChargeTarget(std::uint64_t bytes) const {
        if (bytes > limits.heap_growth_limit_bytes || stats.allocated_bytes > limits.heap_growth_limit_bytes - bytes)
            throw std::bad_alloc();
        const auto required = stats.allocated_bytes + bytes;
        if (required <= stats.heap_target_bytes) return stats.heap_target_bytes;
        const auto slop = std::min(limits.max_free_bytes, limits.heap_growth_limit_bytes - required);
        return required + slop;
    }

    static std::optional<std::size_t> Find(const Arena& arena, std::size_t units) {
        std::size_t run{};
        for (std::size_t i = 0; i < arena.used.size(); ++i) {
            if (arena.used[i])
                run = 0;
            else if (++run == units)
                return i + 1 - units;
        }
        return std::nullopt;
    }

    memory::AddressSpace& space;
    core::CapabilityLedger& ledger;
    JavaPayloadLimits limits;
    memory::ProtectedMappingOwner owner;
    std::vector<std::unique_ptr<Arena>> arenas;
    std::map<std::uint64_t, Allocation> blocks;
    std::map<std::uint64_t, std::uint64_t> borrows;
    std::uint64_t next_generation{}, next_borrow{};
    JavaPayloadStatistics stats;
    mutable std::mutex mutex;
};

GuestJavaPayloadProvider::GuestJavaPayloadProvider(memory::AddressSpace& space, core::CapabilityLedger& ledger,
                                                   JavaPayloadLimits limits)
    : impl_(std::make_shared<Impl>(space, ledger, limits)) {}

GuestJavaPayloadProvider::~GuestJavaPayloadProvider() = default;

const void* GuestJavaPayloadProvider::OwnerIdentity() const noexcept {
    return impl_.get();
}

JavaPayloadHandle GuestJavaPayloadProvider::Allocate(std::size_t bytes) {
    std::scoped_lock lock(impl_->mutex);
    try {
        const auto target = impl_->ChargeTarget(bytes);
        const auto physical = Align(std::max<std::uint64_t>(bytes, 1), kAlignment);
        const bool small = physical <= impl_->limits.slab_bytes / 2;
        const auto units = static_cast<std::size_t>(physical / kAlignment);
        auto block = std::make_shared<payload_detail::Block>();
        block->owner_lifetime = impl_;
        block->owner = impl_.get();
        block->bytes = bytes;
        block->generation = ++impl_->next_generation;
        auto [entry, inserted] = impl_->blocks.emplace(block->generation, Impl::Allocation{block});
        static_cast<void>(inserted);
        Impl::Arena* selected{};
        std::size_t unit{};
        bool new_arena = false;
        try {
            if (small)
                for (const auto& arena : impl_->arenas) {
                    if (!arena->small) continue;
                    if (const auto found = Impl::Find(*arena, units)) {
                        selected = arena.get();
                        unit = *found;
                        break;
                    }
                }
            if (!selected) {
                const auto mapped_bytes =
                    small ? impl_->limits.slab_bytes : Align(physical, impl_->space.BackingPageSize());
                if (mapped_bytes > impl_->limits.committed_limit_bytes ||
                    impl_->stats.committed_bytes > impl_->limits.committed_limit_bytes - mapped_bytes)
                    throw std::bad_alloc();
                auto arena = std::make_unique<Impl::Arena>();
                arena->small = small;
                arena->bytes = mapped_bytes;
                arena->used.resize(static_cast<std::size_t>(mapped_bytes / kAlignment), false);
                impl_->arenas.reserve(impl_->arenas.size() + 1);
                arena->address = impl_->space.MapProtectedAnywhere(impl_->owner, kPayloadBounds, mapped_bytes);
                selected = arena.get();
                impl_->arenas.push_back(std::move(arena));
                new_arena = true;
            }
            block->address = selected->address.Add(unit * kAlignment);
            block->base = impl_->space.ResolveProtected(impl_->owner, {block->address, physical});
        } catch (...) {
            if (new_arena) {
                impl_->space.UnmapProtected(impl_->owner, {selected->address, selected->bytes});
                impl_->arenas.pop_back();
            }
            impl_->blocks.erase(entry);
            throw;
        }
        for (std::size_t i = unit; i < unit + units; ++i)
            selected->used[i] = true;
        ++selected->live;
        entry->second.arena = selected;
        entry->second.unit = unit;
        entry->second.units = units;
        std::memset(block->base, 0, static_cast<std::size_t>(physical));
        if (new_arena) {
            impl_->stats.committed_bytes += selected->bytes;
            if (small) impl_->stats.slab_free_bytes += selected->bytes;
        }
        if (small) impl_->stats.slab_free_bytes -= physical;
        impl_->stats.allocated_bytes += bytes;
        ++impl_->stats.allocation_count;
        impl_->stats.heap_target_bytes = target;
        return MakeHandle(std::move(block));
    } catch (const std::bad_alloc&) {
        impl_->ledger.RecordUnimplemented("runtime.java_payload.budget_exhausted", 0);
        throw;
    }
}

void GuestJavaPayloadProvider::Release(const JavaPayloadHandle& handle) {
    std::scoped_lock lock(impl_->mutex);
    const auto& block = Block(handle);
    auto entry = impl_->blocks.find(block->generation);
    if (entry == impl_->blocks.end() || entry->second.pins || block->accessors.load(std::memory_order_relaxed) != 0)
        throw std::invalid_argument("payload is pinned, accessed or retired");
    const auto allocation = entry->second;
    auto* arena = allocation.arena;
    if (arena->live == 1) impl_->space.UnmapProtected(impl_->owner, {arena->address, arena->bytes});
    block->alive.store(false, std::memory_order_relaxed);
    impl_->stats.allocated_bytes -= block->bytes;
    --impl_->stats.allocation_count;
    if (arena->small) impl_->stats.slab_free_bytes += allocation.units * kAlignment;
    --arena->live;
    if (!arena->live) {
        impl_->stats.committed_bytes -= arena->bytes;
        if (arena->small) impl_->stats.slab_free_bytes -= arena->bytes;
        std::erase_if(impl_->arenas, [arena](const auto& p) { return p.get() == arena; });
    } else
        for (std::size_t i = allocation.unit; i < allocation.unit + allocation.units; ++i)
            arena->used[i] = false;
    impl_->blocks.erase(entry);
}

JavaPayloadBorrow GuestJavaPayloadProvider::Pin(const JavaPayloadHandle& handle) {
    std::scoped_lock lock(impl_->mutex);
    const auto& block = Block(handle);
    auto& allocation = impl_->blocks.at(block->generation);
    const auto id = ++impl_->next_borrow;
    impl_->borrows.emplace(id, block->generation);
    if (allocation.pins++ == 0) impl_->stats.pinned_bytes += block->bytes;
    ++impl_->stats.pin_count;
    return MakeBorrow(handle, id);
}

void GuestJavaPayloadProvider::Unpin(const JavaPayloadBorrow& borrow) {
    std::scoped_lock lock(impl_->mutex);
    const auto& block = Block(BorrowPayload(borrow));
    const auto it = impl_->borrows.find(BorrowId(borrow));
    if (it == impl_->borrows.end() || it->second != block->generation)
        throw std::invalid_argument("invalid payload borrow");
    if (--impl_->blocks.at(block->generation).pins == 0) impl_->stats.pinned_bytes -= block->bytes;
    --impl_->stats.pin_count;
    impl_->borrows.erase(it);
}

JavaPayloadStatistics GuestJavaPayloadProvider::Statistics() const {
    std::scoped_lock lock(impl_->mutex);
    return impl_->stats;
}
}  // namespace ogplay::runtime
