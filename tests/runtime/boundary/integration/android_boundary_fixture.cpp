#include "android_boundary_fixture.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <exception>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include "ogplay/hal/clock.h"
#include <future>
#include "ogplay/runtime/vfs/vfs.h"
#include <unordered_map>
#include <utility>
#include <vector>

#include "ogplay/cpu/interpreter.h"
#include "ogplay/cpu/dynarmic.h"
#include "ogplay/core/logger.h"
#include "ogplay/gles/egl_lifecycle.h"
#include "ogplay/gles/gles_dispatch.h"
#include "ogplay/gles/gles_transfer_state.h"
#include "ogplay/memory/bus.h"
#include "ogplay/loader/link_namespace.h"
#include "ogplay/runtime/boundary/android_boundary_hle.h"
#include "runtime/boundary/core/a32_call_frame.h"
#include "runtime/boundary/modules/gles1/gles1_dispatch.h"
#include "runtime/boundary/modules/gles1/gles1_draw.h"
#include "runtime/boundary/modules/gles1/gles1_query.h"
#include "runtime/boundary/modules/gles1/gles1_support.h"
#include "runtime/boundary/core/boundary_symbols.h"
#include "runtime/boundary/services/graphics_dispatch.h"
#include "runtime/boundary/modules/opensles/opensles_abi.h"

namespace ogplay::test::android_boundary {

void WriteGuestString(BoundaryFixture& fixture,
                      const ogplay::memory::GuestAddress address,
                      const std::string_view value,
                      const bool terminated) {
    std::vector<std::byte> bytes;
    bytes.reserve(value.size() + (terminated ? 1U : 0U));
    for (const auto character : value) {
        bytes.push_back(static_cast<std::byte>(static_cast<unsigned char>(character)));
    }
    if (terminated) bytes.push_back(std::byte{});
    fixture.memory.Write(address, bytes, 1);
}

std::uint32_t FastBoundaryCall(
    BoundaryFixture& fixture, const std::string_view library,
    const std::string_view symbol,
    const std::array<std::uint32_t, 4>& arguments,
    const std::uint64_t thread_id) {
    const auto address = fixture.boundary.Symbols().Lookup(library, symbol);
    REQUIRE(address.has_value());
    std::array<std::uint32_t, 16> registers{};
    std::copy(arguments.begin(), arguments.end(), registers.begin());
    registers[13] = fixture.stack.Value();
    ogplay::cpu::A32HostCallContext call{
        registers, thread_id,
        ogplay::memory::GuestAddress{address->Value() & ~UINT32_C(1)}};
    const auto hook = fixture.boundary.FastHostCallHook();
    REQUIRE(hook.invoke != nullptr);
    REQUIRE(hook.invoke(hook.userdata, 2U, call) ==
            ogplay::cpu::HostCallResult::handled);
    return registers[0];
}

std::uint32_t BoundaryCallAddress(
    BoundaryFixture& fixture, const std::uint32_t thumb_address,
    const std::span<const std::uint32_t> arguments) {
    REQUIRE(arguments.size() <= ogplay::runtime::kMaximumA32CallArguments);
    auto state = fixture.cpu.GetState();
    for (std::size_t index = 0;
         index < std::min(arguments.size(), std::size_t{4}); ++index) {
        state.SetRegister(static_cast<ogplay::cpu::CoreRegister>(index),
                          arguments[index]);
    }
    if (arguments.size() > 4U) {
        std::array<std::byte,
                   (ogplay::runtime::kMaximumA32CallArguments - 4U) * 4U> bytes{};
        for (std::size_t word = 4U; word < arguments.size(); ++word) {
            for (std::size_t byte = 0; byte < 4U; ++byte) {
                bytes[(word - 4U) * 4U + byte] =
                    static_cast<std::byte>(arguments[word] >> (byte * 8U));
            }
        }
        fixture.memory.Write(
            fixture.stack,
            std::span(bytes).first((arguments.size() - 4U) * 4U), 1U);
    }
    fixture.cpu.SetState(state);
    const ogplay::cpu::RunResult stopped{
        1U, ogplay::cpu::RunStopReason::supervisor_call,
        ogplay::memory::GuestAddress{thumb_address & ~UINT32_C(1)}, 0xdf02U,
        2U, std::nullopt};
    if (!fixture.boundary.Handle(fixture.cpu, stopped)) {
        throw std::runtime_error("boundary address call was not handled");
    }
    return fixture.cpu.GetState().Register(ogplay::cpu::CoreRegister::r0);
}

void PrepareNativeEgl(BoundaryFixture& fixture) {
    REQUIRE(fixture.Call("libEGL.so", "eglInitialize", {1U}) == 1U);
    REQUIRE(fixture.Call("libEGL.so", "eglCreateWindowSurface",
                         {1U, 2U, 1U, 0U}) == 3U);
    REQUIRE(fixture.Call("libEGL.so", "eglCreateContext",
                         {1U, 2U, 0U, 0U}) == 4U);
}

std::uint32_t AuditGl(BoundaryFixture& f, const std::string_view name,
                      const std::initializer_list<std::uint32_t> args) {
    const auto entry = f.boundary.Symbols().Lookup("libGLESv2.so", name);
    REQUIRE(entry.has_value());
    return BoundaryCallAddress(f, entry->Value(), {args.begin(), args.size()});
}

std::uint32_t AuditSurface(BoundaryFixture& f) {
    f.bus.Write32(f.output, 0x3057U, 1U); f.bus.Write32(f.output.Add(4), 4U, 1U);
    f.bus.Write32(f.output.Add(8), 0x3056U, 1U); f.bus.Write32(f.output.Add(12), 4U, 1U);
    f.bus.Write32(f.output.Add(16), 0x3038U, 1U);
    const auto value = f.Call("libEGL.so", "eglCreatePbufferSurface", {1, 2, f.output.Value()});
    REQUIRE(value != 0U); return value;
}

std::uint32_t AuditContext(BoundaryFixture& f, const std::uint32_t version,
                           const std::uint32_t share) {
    f.bus.Write32(f.output, 0x3098U, 1U); f.bus.Write32(f.output.Add(4), version, 1U);
    f.bus.Write32(f.output.Add(8), 0x3038U, 1U);
    const auto value = f.Call("libEGL.so", "eglCreateContext", {1, 2, share, f.output.Value()});
    REQUIRE(value != 0U); return value;
}

void AuditBind(BoundaryFixture& f, const std::uint32_t c, const std::uint32_t d,
               const std::uint32_t r) {
    REQUIRE(f.Call("libEGL.so", "eglMakeCurrent", {1, d, r == 0U ? d : r, c}) == 1U);
}

std::uint32_t AuditName(BoundaryFixture& f, const std::string_view gen) {
    AuditGl(f, gen, {1U, f.output.Add(128).Value()});
    return f.bus.Read32(f.output.Add(128), 1U);
}

std::uint32_t AuditInteger(BoundaryFixture& f, const std::uint32_t pname) {
    AuditGl(f, "glGetIntegerv", {pname, f.output.Add(128).Value()});
    return f.bus.Read32(f.output.Add(128), 1U);
}

std::uint32_t AuditPixel(BoundaryFixture& f) {
    AuditGl(f, "glReadPixels", {0, 0, 1, 1, 0x1908, 0x1401, f.output.Add(160).Value()});
    REQUIRE(AuditGl(f, "glGetError") == 0U);
    return f.bus.Read32(f.output.Add(160), 1U);
}

BoundaryFixture::BoundaryFixture(
    const std::uint32_t supersample_factor,
    std::vector<ogplay::runtime::OpenSlesGuestCallback>* callbacks,
    const ogplay::runtime::BionicDynamicLinkHooks dynamic_link,
    const bool allow_single_stage_texcoord_fallback,
    ogplay::runtime::AndroidLooperHooks loopers,
    const bool async_readback,
    const ogplay::gles::CompressedTextureFamily texture_family)
    : bus(memory), cpu(bus), boundary(memory,
          {kNativeRenderer,
           ogplay::gles::AngleDevice::hardware}, 4, 3,
          supersample_factor,
          {.allow_gles1_single_stage_texcoord_fallback =
               allow_single_stage_texcoord_fallback,
           .logger = &logger,
           .guest_file_owner = &guest_files,
           .read_guest_file = +[](void* owner, const std::string_view path,
                                  std::vector<std::byte>& output) {
               const auto& files = *static_cast<const std::unordered_map<
                   std::string, std::vector<std::byte>>*>(owner);
               const auto found = files.find(std::string(path));
               if (found == files.end()) return false;
               output = found->second;
               return true;
           },
           .dynamic_link = dynamic_link,
           .open_sles_callbacks = {
               callbacks,
               +[](void* owner,
                   const ogplay::runtime::OpenSlesGuestCallback& callback) {
                   if (owner != nullptr) {
                       static_cast<std::vector<
                           ogplay::runtime::OpenSlesGuestCallback>*>(owner)
                           ->push_back(callback);
                   }
               }}, .loopers = std::move(loopers),
           .async_present_readback = async_readback,
           .compressed_texture_family = texture_family}) {
    memory.Map({stack, memory.PageSize()},
               ogplay::memory::PageProtection::read |
                   ogplay::memory::PageProtection::write);
    memory.Map({output, memory.PageSize()},
               ogplay::memory::PageProtection::read |
                   ogplay::memory::PageProtection::write);
    boundary.MapThunks();
    ogplay::cpu::A32State state;
    state.SetThreadId(1);
    state.SetRegister(ogplay::cpu::CoreRegister::sp, stack.Value());
    cpu.SetState(state);
}

ogplay::runtime::SupervisorCallProgress BoundaryFixture::CallProgress(
    const std::string_view library, const std::string_view symbol,
    const std::array<std::uint32_t, 4> arguments) {
    const auto address = boundary.Symbols().Lookup(library, symbol);
    REQUIRE(address.has_value());
    auto state = cpu.GetState();
    for (std::size_t index = 0; index < arguments.size(); ++index) {
        state.SetRegister(static_cast<ogplay::cpu::CoreRegister>(index), arguments[index]);
    }
    cpu.SetState(state);
    const ogplay::cpu::RunResult stopped{
        1, ogplay::cpu::RunStopReason::supervisor_call,
        ogplay::memory::GuestAddress{address->Value() & ~1U}, 0xdf02U, 2, std::nullopt};
    const auto progress = boundary.HandleWithProgress(cpu, stopped);
    if (progress == ogplay::runtime::SupervisorCallProgress::not_handled) {
        throw std::runtime_error("Android boundary fixture call was not handled");
    }
    return progress;
}

std::uint32_t BoundaryFixture::Call(const std::string_view library,
                   const std::string_view symbol,
                   const std::array<std::uint32_t, 4> arguments) {
    static_cast<void>(CallProgress(library, symbol, arguments));
    return cpu.GetState().Register(ogplay::cpu::CoreRegister::r0);
}

} // namespace ogplay::test::android_boundary
