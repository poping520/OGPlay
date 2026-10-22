#include "ogplay/runtime/integration/java_payload_provider.h"

#include <algorithm>
#include <list>
#include <limits>
#include <map>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace ogplay::runtime {
namespace {
constexpr std::uint64_t kAlignment = 8;
constexpr std::size_t kLarge = SIZE_MAX;
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
        std::byte* base{};
        std::uint64_t bytes{}, slot_bytes{};
        std::size_t class_index{kLarge}, live{};
        std::vector<std::size_t> free_slots;
        Arena *available_prev{}, *available_next{}, *empty_prev{}, *empty_next{};
        bool available{}, empty{};
        std::list<Arena>::iterator position;
    };

    struct SizeClass final {
        std::uint64_t slot_bytes{}, slabs{}, live_slots{}, allocation_count{};
        Arena* available{};
    };
    struct Allocation final {
        std::shared_ptr<payload_detail::Block> block;
        Arena* arena{};
        std::size_t slot{}, pins{};
        std::uint64_t emergency_bytes{};
    };

    Impl(memory::AddressSpace& space, core::CapabilityLedger& ledger, JavaPayloadLimits limits)
        : space(space), ledger(ledger), limits(limits) {
        if (limits.slab_bytes < space.BackingPageSize() || limits.slab_bytes % space.BackingPageSize() ||
            (limits.slab_bytes & (limits.slab_bytes - 1)) != 0)
            throw std::invalid_argument("invalid Java payload limits");
        committed_limit = limits.committed_limit_bytes.value_or(kPayloadBounds.Size());
        const auto maximum = limits.slab_bytes / 2;
        for (std::uint64_t size = 8; size <= std::min<std::uint64_t>(128, maximum); size += 8)
            classes.push_back(SizeClass{size});
        for (std::uint64_t lower = 128; lower < maximum; lower *= 2)
            for (std::uint64_t step = 1; step <= 8; ++step)
                classes.push_back(SizeClass{lower + step * (lower / 8)});
        class_lookup.resize(static_cast<std::size_t>(maximum / 8 + 1));
        std::size_t index{};
        for (std::size_t unit = 0; unit < class_lookup.size(); ++unit) {
            while (classes[index].slot_bytes < unit * 8)
                ++index;
            class_lookup[unit] = index;
        }
        owner = space.RegisterProtectedOwner([&ledger](std::string_view operation) {
            ledger.RecordUnimplemented("memory.java_payload.protected." + std::string(operation), 0);
        });
    }
    ~Impl() {
        for (auto& [id, allocation] : blocks) {
            static_cast<void>(id);
            allocation.block->alive.store(false, std::memory_order_relaxed);
        }
        for (const auto& arena : arenas)
            space.UnmapProtected(owner, {arena.address, arena.bytes});
        if (emergency_base) space.UnmapProtected(owner, {emergency_address, stats.emergency_reserved_bytes});
    }

    void AddAvailable(Arena& arena) noexcept {
        auto& head = classes[arena.class_index].available;
        arena.available_prev = nullptr;
        arena.available_next = head;
        if (head) head->available_prev = &arena;
        head = &arena;
        arena.available = true;
    }

    void RemoveAvailable(Arena& arena) noexcept {
        if (!arena.available) return;
        if (arena.available_prev)
            arena.available_prev->available_next = arena.available_next;
        else
            classes[arena.class_index].available = arena.available_next;
        if (arena.available_next) arena.available_next->available_prev = arena.available_prev;
        arena.available_prev = arena.available_next = nullptr;
        arena.available = false;
    }

    void AddEmpty(Arena& arena) noexcept {
        arena.empty_next = empty_head;
        if (empty_head) empty_head->empty_prev = &arena;
        empty_head = &arena;
        arena.empty = true;
        ++empty_count;
    }

    void RemoveEmpty(Arena& arena) noexcept {
        if (!arena.empty) return;
        if (arena.empty_prev)
            arena.empty_prev->empty_next = arena.empty_next;
        else
            empty_head = arena.empty_next;
        if (arena.empty_next) arena.empty_next->empty_prev = arena.empty_prev;
        arena.empty_prev = arena.empty_next = nullptr;
        arena.empty = false;
        --empty_count;
    }

    static std::vector<std::size_t> FreeSlots(std::uint64_t bytes, std::uint64_t slot_bytes) {
        const auto count = static_cast<std::size_t>(bytes / slot_bytes);
        std::vector<std::size_t> result(count);
        for (std::size_t i = 0; i < count; ++i)
            result[i] = count - i - 1;
        return result;
    }

    Arena& NewArena(std::size_t class_index, std::uint64_t physical) {
        const auto bytes = class_index == kLarge ? Align(physical, space.BackingPageSize()) : limits.slab_bytes;
        const auto normal_committed = stats.committed_bytes - stats.emergency_reserved_bytes;
        if (bytes > committed_limit || normal_committed > committed_limit - bytes) throw std::bad_alloc();
        auto position = arenas.emplace(arenas.end());
        auto& arena = *position;
        arena.position = position;
        arena.bytes = bytes;
        arena.class_index = class_index;
        arena.slot_bytes = class_index == kLarge ? bytes : classes[class_index].slot_bytes;
        bool mapped{};
        try {
            arena.free_slots = FreeSlots(bytes, arena.slot_bytes);
            try {
                arena.address = space.MapProtectedAnywhere(owner, kPayloadBounds, bytes);
            } catch (const std::runtime_error&) {
                throw std::bad_alloc();
            }
            mapped = true;
            arena.base = space.ResolveProtected(owner, {arena.address, bytes});
        } catch (...) {
            if (mapped) space.UnmapProtected(owner, {arena.address, bytes});
            arenas.erase(position);
            throw;
        }
        return arena;
    }

    memory::AddressSpace& space;
    core::CapabilityLedger& ledger;
    JavaPayloadLimits limits;
    memory::ProtectedMappingOwner owner;
    std::list<Arena> arenas;
    std::vector<SizeClass> classes;
    std::vector<std::size_t> class_lookup;
    Arena* empty_head{};
    std::size_t empty_count{};
    std::unordered_map<std::uint64_t, Allocation> blocks;
    std::map<std::uint64_t, std::uint64_t> borrows;
    std::uint64_t next_generation{}, next_borrow{};
    JavaPayloadStatistics stats;
    std::uint64_t committed_limit{kPayloadBounds.Size()};
    memory::GuestAddress emergency_address;
    std::byte* emergency_base{};

    struct FreeRange {
        std::uint64_t offset{}, bytes{};
    };

    std::vector<FreeRange> emergency_free;
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
        const auto physical = Align(std::max<std::uint64_t>(bytes, 1), kAlignment);
        const auto class_index = physical <= impl_->limits.slab_bytes / 2
                                     ? impl_->class_lookup[static_cast<std::size_t>(physical / 8)]
                                     : kLarge;
        auto block = std::make_shared<payload_detail::Block>();
        block->owner_lifetime = impl_;
        block->owner = impl_.get();
        block->bytes = bytes;
        block->generation = ++impl_->next_generation;
        auto [entry, inserted] = impl_->blocks.emplace(block->generation, Impl::Allocation{block});
        static_cast<void>(inserted);
        Impl::Arena* selected{};
        bool fresh{};
        try {
            if (class_index != kLarge) selected = impl_->classes[class_index].available;
            if (!selected && class_index != kLarge && impl_->empty_head) {
                // Reclassify an empty cached slab without another map/commit. Prepare
                // every allocating operation before touching its old class or lists.
                auto slots = Impl::FreeSlots(impl_->limits.slab_bytes, impl_->classes[class_index].slot_bytes);
                selected = impl_->empty_head;
                impl_->RemoveAvailable(*selected);
                --impl_->classes[selected->class_index].slabs;
                selected->class_index = class_index;
                selected->slot_bytes = impl_->classes[class_index].slot_bytes;
                selected->free_slots = std::move(slots);
                ++impl_->classes[class_index].slabs;
                impl_->AddAvailable(*selected);
            }
            if (!selected) {
                selected = &impl_->NewArena(class_index, physical);
                fresh = true;
            }
            const auto slot = selected->free_slots.back();
            const auto offset = slot * selected->slot_bytes;
            block->address = selected->address.Add(offset);
            block->base = selected->base + offset;
            entry->second.arena = selected;
            entry->second.slot = slot;
            // All operations below are non-allocating, so failure leaves the block,
            // mapping, free lists, cache and counters unchanged.
            if (fresh) {
                impl_->stats.committed_bytes += selected->bytes;
                if (class_index != kLarge) {
                    impl_->stats.slab_free_bytes += selected->bytes;
                    ++impl_->classes[class_index].slabs;
                    impl_->AddAvailable(*selected);
                }
            }
            impl_->RemoveEmpty(*selected);
            selected->free_slots.pop_back();
            ++selected->live;
            if (class_index != kLarge) {
                if (selected->free_slots.empty()) impl_->RemoveAvailable(*selected);
                auto& size_class = impl_->classes[class_index];
                ++size_class.live_slots;
                ++size_class.allocation_count;
                impl_->stats.slab_free_bytes -= selected->slot_bytes;
            }
            std::memset(block->base, 0, static_cast<std::size_t>(selected->slot_bytes));
        } catch (...) {
            if (fresh) {
                impl_->space.UnmapProtected(impl_->owner, {selected->address, selected->bytes});
                impl_->arenas.erase(selected->position);
            }
            impl_->blocks.erase(entry);
            throw;
        }
        impl_->stats.allocated_bytes += bytes;
        ++impl_->stats.allocation_count;
        impl_->stats.peak_live_bytes = std::max(impl_->stats.peak_live_bytes, impl_->stats.allocated_bytes);
        return MakeHandle(std::move(block));
    } catch (const std::bad_alloc&) {
        ++impl_->stats.allocation_failures;
        impl_->ledger.RecordUnimplemented("runtime.java_payload.budget_exhausted", 0);
        throw;
    }
}

std::uint64_t GuestJavaPayloadProvider::DerivedCommittedLimit(std::uint64_t maximum, std::uint64_t growth,
                                                              std::uint64_t slab_bytes,
                                                              std::uint64_t backing_page_bytes,
                                                              std::size_t cached_slabs) {
    if (!growth || growth > maximum || maximum > memory::kGuestAddressSpaceSize || slab_bytes < backing_page_bytes ||
        !backing_page_bytes || (slab_bytes & (slab_bytes - 1)) || (backing_page_bytes & (backing_page_bytes - 1)))
        throw std::invalid_argument("invalid VM payload heap limits");
    // A nonempty slab can retain only one zero-length object (VM header charge
    // 32 B). This conservative bound covers adversarial fragmentation too, not
    // merely 12.5% size rounding or a densely packed allocation trace.
    const auto factor = std::max<std::uint64_t>(3, (slab_bytes + 31) / 32);
    const auto capacity = kPayloadBounds.Size();
    if (maximum > capacity / factor || cached_slabs > capacity / slab_bytes) return capacity;
    const auto occupied = maximum * factor;
    const auto cache = static_cast<std::uint64_t>(cached_slabs) * slab_bytes;
    return occupied > capacity - cache ? capacity : occupied + cache;
}

void GuestJavaPayloadProvider::ConfigureHeap(std::uint64_t maximum, std::uint64_t growth) {
    std::scoped_lock lock(impl_->mutex);
    const auto derived = DerivedCommittedLimit(maximum, growth, impl_->limits.slab_bytes,
                                               impl_->space.BackingPageSize(), impl_->limits.empty_slab_cache_limit);
    const auto reserve = Align(impl_->limits.emergency_reserve_bytes, impl_->space.BackingPageSize());
    if (!reserve || reserve >= kPayloadBounds.Size()) throw std::invalid_argument("invalid payload emergency reserve");
    if (!impl_->emergency_base) {
        std::vector<Impl::FreeRange> free;
        free.reserve(static_cast<std::size_t>(reserve / 8 + 1));
        free.push_back({0, reserve});
        const auto address = impl_->space.MapProtectedAnywhere(impl_->owner, kPayloadBounds, reserve);
        std::byte* base;
        try {
            base = impl_->space.ResolveProtected(impl_->owner, {address, reserve});
        } catch (...) {
            impl_->space.UnmapProtected(impl_->owner, {address, reserve});
            throw;
        }
        impl_->emergency_free = std::move(free);
        impl_->emergency_address = address;
        impl_->emergency_base = base;
        impl_->stats.emergency_reserved_bytes = reserve;
        impl_->stats.committed_bytes += reserve;
    }
    impl_->committed_limit =
        impl_->limits.committed_limit_bytes.value_or(std::min(derived, kPayloadBounds.Size() - reserve));
}

JavaPayloadHandle GuestJavaPayloadProvider::Allocate(std::size_t bytes, bool emergency) {
    try {
        return Allocate(bytes);
    } catch (const std::bad_alloc&) {
        if (!emergency) throw;
    }
    std::scoped_lock lock(impl_->mutex);
    try {
        const auto physical = Align(std::max<std::uint64_t>(1, bytes), 8);
        auto found = std::find_if(impl_->emergency_free.begin(), impl_->emergency_free.end(),
                                  [physical](const auto& range) { return range.bytes >= physical; });
        if (!impl_->emergency_base || found == impl_->emergency_free.end()) throw JavaPayloadEmergencyExhausted();
        auto block = std::make_shared<payload_detail::Block>();
        block->owner_lifetime = impl_;
        block->owner = impl_.get();
        block->bytes = bytes;
        block->generation = ++impl_->next_generation;
        block->address = impl_->emergency_address.Add(found->offset);
        block->base = impl_->emergency_base + found->offset;
        impl_->blocks.emplace(block->generation,
                              Impl::Allocation{block, nullptr, static_cast<std::size_t>(found->offset), 0, physical});
        found->offset += physical;
        found->bytes -= physical;
        if (!found->bytes) impl_->emergency_free.erase(found);
        std::memset(block->base, 0, static_cast<std::size_t>(physical));
        impl_->stats.allocated_bytes += bytes;
        ++impl_->stats.allocation_count;
        impl_->stats.peak_live_bytes = std::max(impl_->stats.peak_live_bytes, impl_->stats.allocated_bytes);
        ++impl_->stats.emergency_uses;
        impl_->stats.emergency_live_bytes += physical;
        impl_->stats.emergency_peak_bytes =
            std::max(impl_->stats.emergency_peak_bytes, impl_->stats.emergency_live_bytes);
        return MakeHandle(std::move(block));
    } catch (const std::bad_alloc&) {
        throw JavaPayloadEmergencyExhausted();
    }
}

void GuestJavaPayloadProvider::Release(const JavaPayloadHandle& handle) {
    std::scoped_lock lock(impl_->mutex);
    const auto& block = Block(handle);
    auto entry = impl_->blocks.find(block->generation);
    if (entry == impl_->blocks.end() || entry->second.pins || block->accessors.load(std::memory_order_relaxed) != 0)
        throw std::invalid_argument("payload is pinned, accessed or retired");
    const auto allocation = entry->second;
    if (allocation.emergency_bytes) {
        const auto offset = static_cast<std::uint64_t>(allocation.slot);
        auto next = std::lower_bound(impl_->emergency_free.begin(), impl_->emergency_free.end(), offset,
                                     [](const auto& range, auto value) { return range.offset < value; });
        next = impl_->emergency_free.insert(next, {offset, allocation.emergency_bytes});
        if (next != impl_->emergency_free.begin()) {
            auto previous = next - 1;
            if (previous->offset + previous->bytes == next->offset) {
                previous->bytes += next->bytes;
                next = impl_->emergency_free.erase(next) - 1;
            }
        }
        if (next + 1 != impl_->emergency_free.end() && next->offset + next->bytes == (next + 1)->offset) {
            next->bytes += (next + 1)->bytes;
            impl_->emergency_free.erase(next + 1);
        }
        impl_->stats.emergency_live_bytes -= allocation.emergency_bytes;
        impl_->stats.allocated_bytes -= block->bytes;
        --impl_->stats.allocation_count;
        block->alive.store(false, std::memory_order_relaxed);
        impl_->blocks.erase(entry);
        return;
    }
    auto& arena = *allocation.arena;
    const auto class_index = arena.class_index;
    const bool unmap =
        arena.live == 1 && (class_index == kLarge || impl_->empty_count >= impl_->limits.empty_slab_cache_limit);
    if (unmap) impl_->space.UnmapProtected(impl_->owner, {arena.address, arena.bytes});
    block->alive.store(false, std::memory_order_relaxed);
    impl_->stats.allocated_bytes -= block->bytes;
    --impl_->stats.allocation_count;
    --arena.live;
    if (class_index != kLarge) {
        auto& size_class = impl_->classes[class_index];
        --size_class.live_slots;
        impl_->stats.slab_free_bytes += arena.slot_bytes;
        if (unmap) {
            impl_->RemoveAvailable(arena);
            --size_class.slabs;
            impl_->stats.slab_free_bytes -= arena.bytes;
        } else {
            const bool full = arena.free_slots.empty();
            arena.free_slots.push_back(allocation.slot);  // capacity fixed when slab is prepared
            if (full) impl_->AddAvailable(arena);
            if (!arena.live) impl_->AddEmpty(arena);
        }
    }
    if (unmap) {
        impl_->stats.committed_bytes -= arena.bytes;
        impl_->arenas.erase(arena.position);
    }
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

std::vector<JavaPayloadSizeClassStatistics> GuestJavaPayloadProvider::SizeClassStatistics() const {
    std::scoped_lock lock(impl_->mutex);
    std::vector<JavaPayloadSizeClassStatistics> result;
    result.reserve(impl_->classes.size());
    for (const auto& size_class : impl_->classes)
        result.push_back({size_class.slot_bytes, size_class.slabs,
                          size_class.slabs * (impl_->limits.slab_bytes / size_class.slot_bytes), size_class.live_slots,
                          size_class.allocation_count});
    return result;
}
}  // namespace ogplay::runtime
