#include "buffer_mapping.h"
#include "graphics_boundary_context.h"
#include "runtime/boundary/core/boundary_binding.h"

#include <algorithm>
#include <array>
#include <vector>

namespace ogplay::runtime {
namespace {
[[noreturn]] void Error(std::uint32_t code) { throw gles::GlesApiError("guest buffer mapping", code); }
std::byte* NativePointer(gles::AngleFrame& frame, std::uint32_t target) {
    return frame.ClientVersion() >= 3 ? frame.MappedBufferPointer(target)
                                     : frame.MappedBufferPointerOes(target, 0x88BDU);
}
bool NativeUnmap(gles::AngleFrame& frame, std::uint32_t target) {
    return frame.ClientVersion() >= 3 ? frame.UnmapBuffer(target) : frame.UnmapBufferOes(target);
}
}

void BufferMappingService::MapGuestArena() {
    if (arena_mapped_) return;
    memory_.Map({memory::GuestAddress{kBufferMapArenaBegin}, kBufferMapArenaBytes},
        memory::PageProtection::read | memory::PageProtection::write);
    arena_mapped_ = true;
}
void BufferMappingService::RetireShareGroup(std::uint32_t group) noexcept {
    std::erase_if(mappings_, [group](const auto& item) { return (item.first >> 32U) == group; });
}
void BufferMappingService::RetireBuffers(std::uint32_t group, std::span<const std::uint32_t> names) noexcept {
    for (auto name : names) mappings_.erase((static_cast<std::uint64_t>(group) << 32U) | name);
}
void BufferMappingService::SetDataObserver(void* owner,
    void (*changed)(void*, std::uint32_t, std::uint32_t, std::uint32_t,
                    std::span<const std::byte>, bool)) noexcept {
    observer_ = owner; changed_ = changed;
}
std::uint64_t BufferMappingService::Key(gles::AngleFrame& frame, std::uint32_t group, std::uint32_t target) {
    return (static_cast<std::uint64_t>(group) << 32U) | frame.BoundBuffer(target);
}
BufferMappingService::Mapping* BufferMappingService::Find(gles::AngleFrame& frame,
    std::uint32_t group, std::uint32_t target) {
    const auto key = Key(frame, group, target);
    const auto found = mappings_.find(key);
    if (found == mappings_.end()) return nullptr;
    if (frame.GetBufferParameter(target, 0x88BCU) == 0) {
        mappings_.erase(found); return nullptr;
    }
    if (NativePointer(frame, target) != found->second.host) {
        mappings_.erase(found); Error(0x0502U);
    }
    return &found->second;
}
memory::GuestAddress BufferMappingService::Allocate(std::uint32_t length) const {
    const auto size = (std::max)(length, 1U);
    if (size > kBufferMapArenaBytes) Error(0x0505U);
    std::vector<std::pair<std::uint32_t, std::uint32_t>> occupied;
    for (const auto& [_, map] : mappings_)
        occupied.emplace_back(map.guest.Value() - kBufferMapArenaBegin, (map.length + 15U) & ~15U);
    std::ranges::sort(occupied);
    std::uint32_t offset{};
    for (const auto& [begin, count] : occupied) {
        if (size <= begin - offset) return memory::GuestAddress{kBufferMapArenaBegin + offset};
        offset = begin + count;
    }
    if (size > kBufferMapArenaBytes - offset) Error(0x0505U);
    return memory::GuestAddress{kBufferMapArenaBegin + offset};
}
memory::GuestAddress BufferMappingService::MapOes(gles::AngleFrame& frame,
    std::uint32_t group, std::uint32_t target, std::uint32_t access, std::uint64_t thread) {
    if (access != 0x88B9U || (target != 0x8892U && target != 0x8893U)) Error(0x0500U);
    const auto length = frame.GetBufferParameter(target, 0x8764U);
    return MapRange(frame, group, target, 0, length, 2U, thread);
}
memory::GuestAddress BufferMappingService::MapRange(gles::AngleFrame& frame,
    std::uint32_t group, std::uint32_t target, std::int32_t offset,
    std::int32_t length, std::uint32_t access, std::uint64_t thread) {
    if (!arena_mapped_) throw std::logic_error("buffer mapping arena is unavailable");
    if (offset < 0 || length < 0 || (access & ~0x3FU) != 0) Error(0x0501U);
    if (length == 0 || (access & 3U) == 0 ||
        ((access & 1U) != 0 && (access & 0x2CU) != 0) ||
        ((access & 0x10U) != 0 && (access & 2U) == 0)) Error(0x0502U);
    const auto key = Key(frame, group, target);
    if (static_cast<std::uint32_t>(key) == 0 || Find(frame, group, target) != nullptr ||
        frame.GetBufferParameter(target, 0x88BCU) != 0) Error(0x0502U);
    const auto size = frame.GetBufferParameter(target, 0x8764U);
    if (static_cast<std::int64_t>(offset) + length > size) Error(0x0501U);
    const auto guest = Allocate(static_cast<std::uint32_t>(length));
    const bool preserve = (access & 2U) != 0 && (access & 0xCU) == 0;
    // Reading a WRITE_ONLY host mapping is undefined. Ordinary write mappings
    // use a legal internal READ|WRITE lease; UNSYNCHRONIZED cannot include READ.
    std::vector<std::byte> before;
    if (preserve && (access & 0x20U) != 0)
        before = frame.ReadBufferRange(target, static_cast<std::uint32_t>(offset), static_cast<std::uint32_t>(length));
    const auto host_access = preserve && (access & 0x20U) == 0 ? access | 1U : access;
    auto* host = frame.MapBufferRangeCompat(target, offset, length, host_access);
    if (!host) Error(0x0505U);
    try {
        if (!before.empty()) memory_.Write(guest, before, thread);
        else if ((host_access & 1U) != 0)
            memory_.Write(guest, {host, static_cast<std::size_t>(length)}, thread);
        mappings_.emplace(key, Mapping{guest, static_cast<std::uint32_t>(offset),
            static_cast<std::uint32_t>(length), access, host, (host_access & 1U) != 0});
    } catch (...) { static_cast<void>(NativeUnmap(frame, target)); throw; }
    return guest;
}
void BufferMappingService::Changed(gles::AngleFrame& frame, std::uint32_t group,
    std::uint32_t target, const Mapping& map, std::uint32_t offset, std::uint32_t length,
    std::uint64_t thread) {
    if (!changed_ || length == 0) return;
    std::vector<std::byte> written;
    std::span<const std::byte> bytes{map.host + offset, length};
    if (!map.host_readable) {
        written.resize(length);
        memory_.Read(map.guest.Add(offset), written, thread);
        bytes = written;
    }
    changed_(observer_, group, target, map.offset + offset, bytes,
        map.offset + offset == 0 && length == static_cast<std::uint32_t>(frame.GetBufferParameter(target, 0x8764U)));
}
void BufferMappingService::Flush(gles::AngleFrame& frame, std::uint32_t group,
    std::uint32_t target, std::int32_t offset, std::int32_t length, std::uint64_t thread) {
    auto* map = Find(frame, group, target);
    if (!map || (map->access & 0x10U) == 0) Error(0x0502U);
    if (offset < 0 || length < 0 || static_cast<std::int64_t>(offset) + length > map->length) Error(0x0501U);
    if (length != 0) memory_.Read(map->guest.Add(static_cast<std::uint32_t>(offset)),
        {map->host + offset, static_cast<std::size_t>(length)}, thread);
    frame.FlushMappedBufferRangeCompat(target, offset, length);
    Changed(frame, group, target, *map, static_cast<std::uint32_t>(offset), static_cast<std::uint32_t>(length), thread);
}
bool BufferMappingService::Unmap(gles::AngleFrame& frame, std::uint32_t group,
    std::uint32_t target, std::uint64_t thread) {
    auto* map = Find(frame, group, target);
    if (!map) Error(0x0502U);
    const auto key = Key(frame, group, target);
    if ((map->access & 0x12U) == 2U) {
        memory_.Read(map->guest, {map->host, map->length}, thread);
        Changed(frame, group, target, *map, 0, map->length, thread);
    }
    const auto intact = NativeUnmap(frame, target);
    mappings_.erase(key);
    return intact;
}
memory::GuestAddress BufferMappingService::Pointer(gles::AngleFrame& frame,
    std::uint32_t group, std::uint32_t target, std::uint32_t pname) {
    if (pname != 0x88BDU) Error(0x0500U);
    const auto* map = Find(frame, group, target);
    if (!map && frame.GetBufferParameter(target, 0x88BCU) != 0) Error(0x0502U);
    return map ? map->guest : memory::GuestAddress{};
}
std::optional<std::int64_t> BufferMappingService::Parameter(gles::AngleFrame& frame,
    std::uint32_t group, std::uint32_t target, std::uint32_t pname) {
    if (pname != 0x88BBU && pname != 0x88BCU && pname != 0x911FU &&
        pname != 0x9120U && pname != 0x9121U) return std::nullopt;
    const auto* map = Find(frame, group, target);
    if (!map) return std::nullopt;
    if (pname == 0x88BCU) return 1;
    if (pname == 0x911FU) return map->access;
    if (pname == 0x9120U) return map->length;
    if (pname == 0x9121U) return map->offset;
    return (map->access & 3U) == 1U ? 0x88B8 : (map->access & 3U) == 2U ? 0x88B9 : 0x88BA;
}

std::uint32_t InvokeOesMap(const A32CallFrame& call, GraphicsBoundaryContext& graphics) {
    return graphics.buffer_mappings->MapOes(graphics.RequireFrame("glMapBufferOES"),
        graphics.gl_context.ShareGroup(), call.Argument(0), call.Argument(1), call.ThreadId()).Value();
}
std::uint32_t InvokeBufferUnmap(const A32CallFrame& call, GraphicsBoundaryContext& graphics) {
    return graphics.buffer_mappings->Unmap(graphics.RequireFrame("glUnmapBuffer"),
        graphics.gl_context.ShareGroup(), call.Argument(0), call.ThreadId()) ? 1U : 0U;
}
std::uint32_t InvokeMapPointer(const A32CallFrame& call, GraphicsBoundaryContext& graphics,
    BoundaryCallServices& calls) {
    const auto output = calls.address_space.PreflightWrite(
        {call.Pointer<std::uint32_t>(2).Address(), 4}, call.ThreadId());
    const auto pointer = graphics.buffer_mappings->Pointer(graphics.RequireFrame("glGetBufferPointerv"),
        graphics.gl_context.ShareGroup(), call.Argument(0), call.Argument(1)).Value();
    std::array<std::byte, 4> bytes{};
    for (std::size_t i = 0; i < bytes.size(); ++i) bytes[i] = static_cast<std::byte>(pointer >> (8U * i));
    calls.address_space.WritePrevalidated(output, bytes);
    return 0;
}
std::uint32_t InvokeBufferParameter(const A32CallFrame& call, GraphicsBoundaryContext& graphics,
    BoundaryCallServices& calls) {
    const auto output = calls.address_space.PreflightWrite(
        {call.Pointer<std::uint32_t>(2).Address(), 4}, call.ThreadId());
    auto& frame = graphics.RequireFrame("glGetBufferParameteriv");
    const auto guest = graphics.buffer_mappings->Parameter(frame, graphics.gl_context.ShareGroup(),
        call.Argument(0), call.Argument(1));
    const auto value = static_cast<std::uint32_t>(guest ? *guest : frame.GetBufferParameter(call.Argument(0), call.Argument(1)));
    std::array<std::byte, 4> bytes{};
    for (std::size_t i = 0; i < bytes.size(); ++i) bytes[i] = static_cast<std::byte>(value >> (8U * i));
    calls.address_space.WritePrevalidated(output, bytes);
    return 0;
}
} // namespace ogplay::runtime
