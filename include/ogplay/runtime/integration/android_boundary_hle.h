#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include "ogplay/cpu/cpu.h"
#include "ogplay/gles/angle_backend.h"
#include "ogplay/runtime/bionic/bionic_profile.h"

namespace ogplay::runtime {

enum class AndroidBoundaryInputType : std::uint8_t {
    key,
    pointer_motion,
    pointer_button,
};

struct AndroidBoundaryInput final {
    AndroidBoundaryInputType type{};
    std::int32_t code{};
    float x{};
    float y{};
    bool pressed{};
};

struct AndroidBoundaryFrame final {
    std::uint32_t width{};
    std::uint32_t height{};
    std::uint64_t sequence{};
    std::vector<std::uint8_t> rgba8;
};

class AndroidBoundaryHle final {
public:
    AndroidBoundaryHle(memory::AddressSpace& address_space,
                       gles::AngleBackend backend,
                       std::uint32_t width, std::uint32_t height);
    ~AndroidBoundaryHle();
    AndroidBoundaryHle(const AndroidBoundaryHle&) = delete;
    AndroidBoundaryHle& operator=(const AndroidBoundaryHle&) = delete;

    void MapThunks();
    [[nodiscard]] const BionicHleSymbolProvider& Symbols() const noexcept;
    [[nodiscard]] bool Handle(cpu::Cpu& cpu, const cpu::RunResult& stopped);
    void NotifyFileWrite();
    void PushInput(const AndroidBoundaryInput& input);
    [[nodiscard]] std::optional<AndroidBoundaryFrame> TakeLatestFrame();

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace ogplay::runtime
