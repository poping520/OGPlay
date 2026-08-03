#include "ogplay/core/logger.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace ogplay::core {

Logger::Logger(const std::size_t ring_capacity) : capacity_(ring_capacity) {
    if (capacity_ == 0) {
        throw std::invalid_argument("logger ring capacity must be greater than zero");
    }
}

void Logger::Write(const LogLevel level,
                   const std::string_view category,
                   const std::string_view message,
                   const LogContext& context,
                   std::vector<LogField> fields) {
    if (category.empty()) {
        throw std::invalid_argument("structured log category must not be empty");
    }
    if (message.empty()) {
        throw std::invalid_argument("structured log message must not be empty");
    }

    LogRecord record{
        .wall = std::chrono::system_clock::now(),
        .frame = context.frame,
        .guest_ticks = context.guest_ticks,
        .host_thread = context.host_thread,
        .guest_thread = context.guest_thread,
        .level = level,
        .category = std::string(category),
        .message = std::string(message),
        .fields = std::move(fields),
    };

    std::scoped_lock lock(mutex_);
    if (records_.size() == capacity_) {
        records_.pop_front();
        ++dropped_;
    }
    records_.push_back(std::move(record));
}

std::vector<LogRecord> Logger::Snapshot(const std::optional<LogLevel> minimum,
                                        const std::string_view category_prefix,
                                        const std::size_t limit) const {
    std::scoped_lock lock(mutex_);
    std::vector<LogRecord> result;
    for (const auto& record : records_) {
        if (minimum && record.level < *minimum) {
            continue;
        }
        if (!category_prefix.empty() && !record.category.starts_with(category_prefix)) {
            continue;
        }
        result.push_back(record);
    }
    if (limit != 0 && result.size() > limit) {
        result.erase(result.begin(), result.end() - static_cast<std::ptrdiff_t>(limit));
    }
    return result;
}

std::size_t Logger::Capacity() const noexcept {
    return capacity_;
}

std::uint64_t Logger::DroppedCount() const {
    std::scoped_lock lock(mutex_);
    return dropped_;
}

std::string_view ToString(const LogLevel level) noexcept {
    switch (level) {
    case LogLevel::trace: return "trace";
    case LogLevel::debug: return "debug";
    case LogLevel::info: return "info";
    case LogLevel::warn: return "warn";
    case LogLevel::error: return "error";
    case LogLevel::fatal: return "fatal";
    }
    return "unknown";
}

}  // namespace ogplay::core

