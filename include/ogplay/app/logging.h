#pragma once
#include "ogplay/app/export.h"
#include <filesystem>
#include <optional>
namespace ogplay::app {
enum class LogLevel { trace, debug, info, warn, error, fatal };
struct LogConfig final {
    LogLevel console_level{LogLevel::info};
    std::optional<std::filesystem::path> file;
    bool stderr_enabled{true};
};
}
