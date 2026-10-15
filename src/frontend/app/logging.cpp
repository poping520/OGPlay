#include "ogplay/app/logging.h"
#include "ogplay/core/logger.h"
namespace ogplay::app {
struct Logger::Impl { core::Logger logger; };
Logger::Logger(const LogConfig& config) : impl_(std::make_unique<Impl>()) {
    if (config.stderr_enabled) impl_->logger.AddSink(std::make_shared<core::ConsoleSink>(stderr), static_cast<core::LogLevel>(config.console_level));
    if (config.file) SetFile(*config.file);
}
Logger::~Logger() = default;
void Logger::SetFile(const std::filesystem::path& path) { impl_->logger.AddSink(std::make_shared<core::FileSink>(path)); }
void Logger::Write(LogLevel level, std::string_view category, std::string_view message, const LogContext& context, std::vector<LogField> fields) {
    std::vector<core::LogField> converted;
    converted.reserve(fields.size());
    for (auto& field : fields) std::visit([&](auto&& value) { converted.push_back({field.key, std::forward<decltype(value)>(value)}); }, std::move(field.value));
    impl_->logger.Write(static_cast<core::LogLevel>(level), category, message,
        {context.frame, context.guest_ticks, context.host_thread, context.guest_thread}, std::move(converted));
}
}
