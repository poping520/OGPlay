#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include "ogplay/gles/angle_frame.h"
#include "ogplay/memory/address_space.h"
#include "ogplay/runtime/boundary/boundary_symbol.h"

namespace ogplay::runtime {
struct GraphicsBoundaryContext;
struct BoundaryCallServices;
class A32CallFrame;
inline constexpr auto kBufferMapArenaBegin = kBoundaryBufferMaps.Start().Value();
inline constexpr auto kBufferMapArenaBytes = static_cast<std::uint32_t>(kBoundaryBufferMaps.Size());

// Serialized by the current graphics execution lock. All guest APIs use this
// registry; host-private readback mappings remain outside the guest ABI.
class BufferMappingService final {
public:
    explicit BufferMappingService(memory::AddressSpace& memory) : memory_(memory) {}
    void MapGuestArena();
    void RetireShareGroup(std::uint32_t group) noexcept;
    void RetireBuffers(std::uint32_t group, std::span<const std::uint32_t> names) noexcept;
    void SetDataObserver(void* owner, void (*changed)(void*, std::uint32_t,
        std::uint32_t, std::uint32_t, std::span<const std::byte>, bool)) noexcept;
    memory::GuestAddress MapOes(gles::AngleFrame&, std::uint32_t group,
        std::uint32_t target, std::uint32_t access, std::uint64_t thread);
    memory::GuestAddress MapRange(gles::AngleFrame&, std::uint32_t group,
        std::uint32_t target, std::int32_t offset, std::int32_t length,
        std::uint32_t access, std::uint64_t thread);
    bool Unmap(gles::AngleFrame&, std::uint32_t group, std::uint32_t target,
        std::uint64_t thread);
    void Flush(gles::AngleFrame&, std::uint32_t group, std::uint32_t target,
        std::int32_t offset, std::int32_t length, std::uint64_t thread);
    memory::GuestAddress Pointer(gles::AngleFrame&, std::uint32_t group,
        std::uint32_t target, std::uint32_t pname);
    std::optional<std::int64_t> Parameter(gles::AngleFrame&, std::uint32_t group,
        std::uint32_t target, std::uint32_t pname);
private:
    struct Mapping final {
        memory::GuestAddress guest;
        std::uint32_t offset{}, length{}, access{};
        std::byte* host{};
        bool host_readable{};
    };
    std::uint64_t Key(gles::AngleFrame&, std::uint32_t group, std::uint32_t target);
    Mapping* Find(gles::AngleFrame&, std::uint32_t group, std::uint32_t target);
    memory::GuestAddress Allocate(std::uint32_t length) const;
    void Changed(gles::AngleFrame&, std::uint32_t group, std::uint32_t target,
        const Mapping&, std::uint32_t offset, std::uint32_t length, std::uint64_t thread);
    memory::AddressSpace& memory_;
    std::map<std::uint64_t, Mapping> mappings_;
    bool arena_mapped_{};
    void* observer_{};
    void (*changed_)(void*, std::uint32_t, std::uint32_t, std::uint32_t,
                    std::span<const std::byte>, bool){};
};
std::uint32_t InvokeOesMap(const A32CallFrame&, GraphicsBoundaryContext&);
std::uint32_t InvokeBufferUnmap(const A32CallFrame&, GraphicsBoundaryContext&);
std::uint32_t InvokeMapPointer(const A32CallFrame&, GraphicsBoundaryContext&, BoundaryCallServices&);
std::uint32_t InvokeBufferParameter(const A32CallFrame&, GraphicsBoundaryContext&, BoundaryCallServices&);
} // namespace ogplay::runtime
