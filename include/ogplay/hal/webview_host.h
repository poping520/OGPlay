#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace ogplay::core { class Logger; }
namespace ogplay::hal {

struct WebViewHostOptions final {
    std::filesystem::path document;
    std::optional<std::uint64_t> smoke_responses;
    std::filesystem::path smoke_image;
};
struct WebViewHostCallbacks final {
    std::function<std::string(std::string_view)> request;
    std::function<void()> poll;
};
// Main-thread-only host. Native handles never cross this interface.
class WebViewHost {
public:
    virtual ~WebViewHost() = default;
    virtual int Run() = 0;
    virtual void Evaluate(std::string_view script) = 0;
    virtual void RecordSmokeResponse() = 0;
};
[[nodiscard]] std::unique_ptr<WebViewHost> CreateWebViewHost(
    WebViewHostOptions options, WebViewHostCallbacks callbacks, core::Logger& logger);
void OpenHostDirectory(const std::filesystem::path& path);

}  // namespace ogplay::hal
