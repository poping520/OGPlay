#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

#include "ogplay/loader/module_loader.h"
#include "ogplay/runtime/integration/android_boundary_hle.h"

namespace ogplay::runtime {

struct NativeActivityRunRequest final {
    std::uint32_t api{19};
    std::string root_module;
    std::span<const loader::Elf32ModuleInput> modules;
    gles::AngleBackend backend{};
    std::uint32_t width{640};
    std::uint32_t height{360};
    std::uint64_t maximum_ticks_per_call{UINT64_C(200000000)};
    std::function<void(std::string_view)> progress;
};

class NativeActivityRunError final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class NativeActivitySession final : public core::GpuStateProvider {
public:
    [[nodiscard]] static std::unique_ptr<NativeActivitySession> Start(
        const NativeActivityRunRequest& request);
    ~NativeActivitySession();
    NativeActivitySession(const NativeActivitySession&) = delete;
    NativeActivitySession& operator=(const NativeActivitySession&) = delete;

    void PushInput(const AndroidBoundaryInput& input);
    [[nodiscard]] std::optional<AndroidBoundaryFrame> TakeLatestFrame();
    void Stop();
    [[nodiscard]] bool Running() const noexcept;
    [[nodiscard]] core::GpuStats Stats() const override;
    [[nodiscard]] std::vector<core::GpuRenderTarget> RenderTargets() const override;
    [[nodiscard]] core::GpuCapabilities Capabilities() const override;
    [[nodiscard]] std::vector<core::GpuTraceEntry> Trace(
        std::string_view filter, std::size_t limit) const override;

private:
    class Impl;
    explicit NativeActivitySession(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> impl_;
};

}  // namespace ogplay::runtime
