#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ogplay::core {

struct GpuDrawTargetStats final {
    std::uint32_t fbo{};
    std::uint64_t draws{};
    std::string attachment;
};

struct GpuStats final {
    std::uint64_t draws{};
    std::uint64_t clears{};
    std::uint64_t shader_compiles{};
    std::uint64_t program_links{};
    std::uint64_t gl_errors{};
    std::vector<GpuDrawTargetStats> draw_targets;
};

struct GpuRenderTarget final {
    std::uint32_t fbo{};
    std::uint32_t width{};
    std::uint32_t height{};
    std::string format;
    std::vector<std::string> attachments;
    bool created_by_guest{};
};

struct GpuCapabilities final {
    std::vector<std::string> reported_extensions;
    std::map<std::string, std::int64_t, std::less<>> reported_limits;
    std::string host_backend;
};

struct GpuTraceEntry final {
    std::string call;
    std::map<std::string, std::string, std::less<>> arguments;
    std::optional<std::uint32_t> error;
};

class GpuStateProvider {
public:
    virtual ~GpuStateProvider() = default;
    [[nodiscard]] virtual GpuStats Stats() const = 0;
    [[nodiscard]] virtual std::vector<GpuRenderTarget> RenderTargets() const = 0;
    [[nodiscard]] virtual GpuCapabilities Capabilities() const = 0;
    [[nodiscard]] virtual std::vector<GpuTraceEntry> Trace(
        std::string_view filter, std::size_t limit) const = 0;
};

}  // namespace ogplay::core
