#pragma once

#include <array>
#include <cstdint>
#include <initializer_list>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "ogplay/core/logger.h"
#include "ogplay/core/capability_ledger.h"
#include "ogplay/cpu/interpreter.h"
#include "ogplay/memory/bus.h"
#include "ogplay/runtime/boundary/android_boundary_hle.h"

namespace ogplay::test::android_boundary {

class BoundaryFixture;

#if defined(_WIN32)
constexpr ogplay::gles::AngleRenderer kNativeRenderer =
    ogplay::gles::AngleRenderer::d3d11;
#elif defined(__APPLE__)
constexpr ogplay::gles::AngleRenderer kNativeRenderer =
    ogplay::gles::AngleRenderer::metal;
#else
constexpr ogplay::gles::AngleRenderer kNativeRenderer =
    ogplay::gles::AngleRenderer::vulkan;
#endif

void WriteGuestString(BoundaryFixture& fixture,
                      const ogplay::memory::GuestAddress address,
                      const std::string_view value,
                      const bool terminated = true);

std::uint32_t FastBoundaryCall(
    BoundaryFixture& fixture, const std::string_view library,
    const std::string_view symbol,
    const std::array<std::uint32_t, 4>& arguments = {},
    const std::uint64_t thread_id = 1U);

std::uint32_t BoundaryCallAddress(
    BoundaryFixture& fixture, const std::uint32_t thumb_address,
    const std::span<const std::uint32_t> arguments);

void PrepareNativeEgl(BoundaryFixture& fixture);

std::uint32_t AuditGl(BoundaryFixture& f, const std::string_view name,
                      const std::initializer_list<std::uint32_t> args = {});

std::uint32_t AuditSurface(BoundaryFixture& f);

std::uint32_t AuditContext(BoundaryFixture& f, const std::uint32_t version = 3U,
                           const std::uint32_t share = 0U);

void AuditBind(BoundaryFixture& f, const std::uint32_t c, const std::uint32_t d,
               const std::uint32_t r = 0U);

std::uint32_t AuditName(BoundaryFixture& f, const std::string_view gen);

std::uint32_t AuditInteger(BoundaryFixture& f, const std::uint32_t pname);

std::uint32_t AuditPixel(BoundaryFixture& f);

class BoundaryFixture final {
public:
    explicit BoundaryFixture(
        const std::uint32_t supersample_factor = 1,
        std::vector<ogplay::runtime::OpenSlesGuestCallback>* callbacks = nullptr,
        const ogplay::runtime::BionicDynamicLinkHooks dynamic_link = {},
        const bool allow_single_stage_texcoord_fallback = true,
        ogplay::runtime::AndroidLooperHooks loopers = {},
        const bool async_readback = false,
        const ogplay::gles::CompressedTextureFamily texture_family = ogplay::gles::CompressedTextureFamily::all);

    ogplay::runtime::SupervisorCallProgress CallProgress(
        const std::string_view library, const std::string_view symbol,
        const std::array<std::uint32_t, 4> arguments = {});

    std::uint32_t Call(const std::string_view library,
                       const std::string_view symbol,
                       const std::array<std::uint32_t, 4> arguments = {});

    ogplay::memory::AddressSpace memory;
    ogplay::memory::CheckedMemoryBus bus;
    ogplay::cpu::InterpreterCpu cpu;
    ogplay::core::Logger logger;
    ogplay::core::CapabilityLedger ledger;
    std::unordered_map<std::string, std::vector<std::byte>> guest_files;
    ogplay::runtime::AndroidBoundaryHle boundary;
    const ogplay::memory::GuestAddress stack{0x6e100000U};
    const ogplay::memory::GuestAddress output{0x6e101000U};
};

} // namespace ogplay::test::android_boundary
