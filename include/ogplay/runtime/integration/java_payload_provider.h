#pragma once

#include <cstdint>
#include <memory>

#include "ogplay/core/capability_ledger.h"
#include "ogplay/memory/address_space.h"
#include "ogplay/runtime/jni/java_payload.h"

namespace ogplay::runtime {

struct JavaPayloadLimits final {
    // ADR-0060 defaults; not another production heap. Stage 2 must wire the
    // existing VM's shared charge/GC context before switching any store.
    std::uint64_t initial_heap_target_bytes{64ULL * 1024 * 1024};
    std::uint64_t heap_growth_limit_bytes{512ULL * 1024 * 1024};
    std::uint64_t maximum_heap_bytes{1024ULL * 1024 * 1024};
    std::uint64_t max_free_bytes{8ULL * 1024 * 1024};
    std::uint64_t committed_limit_bytes{512ULL * 1024 * 1024};
    std::uint64_t slab_bytes{16ULL * 1024};
};

// Fixture-only in I1. AddressSpace and ledger outlive provider; owner serializes
// release/teardown with access and stops CPUs before reclaiming guest mappings.
class GuestJavaPayloadProvider final : public JavaPayloadProvider {
public:
    GuestJavaPayloadProvider(memory::AddressSpace& space, core::CapabilityLedger& ledger,
                             JavaPayloadLimits limits = {});
    ~GuestJavaPayloadProvider() override;
    GuestJavaPayloadProvider(const GuestJavaPayloadProvider&) = delete;
    GuestJavaPayloadProvider& operator=(const GuestJavaPayloadProvider&) = delete;
    [[nodiscard]] JavaPayloadHandle Allocate(std::size_t bytes) override;
    void Release(const JavaPayloadHandle& handle) override;
    [[nodiscard]] JavaPayloadBorrow Pin(const JavaPayloadHandle& handle) override;
    void Unpin(const JavaPayloadBorrow& borrow) override;
    [[nodiscard]] JavaPayloadStatistics Statistics() const override;

private:
    [[nodiscard]] const void* OwnerIdentity() const noexcept override;
    class Impl;
    std::shared_ptr<Impl> impl_;
};

}  // namespace ogplay::runtime
