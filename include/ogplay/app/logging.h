#pragma once
#include "ogplay/app/export.h"
#include <filesystem>
#include <optional>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <variant>
#include <vector>
namespace ogplay::app {
enum class LogLevel { trace, debug, info, warn, error, fatal };
struct LogConfig final {
    LogLevel console_level{LogLevel::info};
    std::optional<std::filesystem::path> file;
    bool stderr_enabled{true};
    bool status_to_stderr{};
};
struct LogField final {
    std::string key;
    std::variant<std::int64_t, std::uint64_t, double, bool, std::string> value;
};
struct LogContext final { std::uint64_t frame{}, guest_ticks{}, host_thread{}, guest_thread{}; };
class OGPLAYCORE_API Logger final {
public:
    explicit Logger(const LogConfig& config = {});
    ~Logger();
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;
    void SetFile(const std::filesystem::path& path);
    void Write(LogLevel level, std::string_view category, std::string_view message,
               const LogContext& context = {}, std::vector<LogField> fields = {});
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
