#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include "ogplay/core/capability_ledger.h"
#include "ogplay/memory/address_space.h"
#include "ogplay/runtime/jni/java_payload.h"

namespace ogplay::runtime {

struct JavaPayloadLimits final {
    // Logical heap accounting belongs exclusively to the VM allocation context.
    // Optional override for constrained fixtures; production derives this from VM limits.
    std::optional<std::uint64_t> committed_limit_bytes;
    std::uint64_t emergency_reserve_bytes{256ULL * 1024};
    std::uint64_t slab_bytes{16ULL * 1024};
    std::size_t empty_slab_cache_limit{64};
};

struct JavaPayloadSizeClassStatistics final {
    std::uint64_t slot_bytes{}, slabs{}, total_slots{}, live_slots{}, allocation_count{};
};

// Process-owned backing allocator. AddressSpace and ledger outlive provider; owner serializes
// release/teardown with access and stops CPUs before reclaiming guest mappings.
class GuestJavaPayloadProvider final : public JavaPayloadProvider {
public:
    GuestJavaPayloadProvider(memory::AddressSpace& space, core::CapabilityLedger& ledger,
                             JavaPayloadLimits limits = {});
    ~GuestJavaPayloadProvider() override;
    void ConfigureHeap(std::uint64_t maximum, std::uint64_t growth) override;
    [[nodiscard]] static std::uint64_t DerivedCommittedLimit(std::uint64_t maximum, std::uint64_t growth,
                                                             std::uint64_t slab_bytes, std::uint64_t backing_page_bytes,
                                                             std::size_t cached_slabs);
    [[nodiscard]] JavaPayloadHandle Allocate(std::size_t bytes, bool emergency) override;
    GuestJavaPayloadProvider(const GuestJavaPayloadProvider&) = delete;
    GuestJavaPayloadProvider& operator=(const GuestJavaPayloadProvider&) = delete;
    [[nodiscard]] JavaPayloadHandle Allocate(std::size_t bytes) override;
    void Release(const JavaPayloadHandle& handle) override;
    [[nodiscard]] JavaPayloadBorrow Pin(const JavaPayloadHandle& handle) override;
    void Unpin(const JavaPayloadBorrow& borrow) override;
    [[nodiscard]] JavaPayloadStatistics Statistics() const override;
    [[nodiscard]] std::vector<JavaPayloadSizeClassStatistics> SizeClassStatistics() const;

private:
    [[nodiscard]] const void* OwnerIdentity() const noexcept override;
    class Impl;
    std::shared_ptr<Impl> impl_;
};

}  // namespace ogplay::runtime
