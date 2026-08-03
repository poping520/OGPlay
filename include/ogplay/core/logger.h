#pragma once

#include <chrono>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace ogplay::core {

enum class LogLevel : std::uint8_t { trace, debug, info, warn, error, fatal };

struct GuestAddress {
    std::uint64_t value{};
};

using FieldValue = std::variant<std::int64_t, std::uint64_t, double, bool, std::string, GuestAddress>;

struct LogField {
    std::string key;
    FieldValue value;
};

struct LogRecord {
    std::chrono::system_clock::time_point wall;
    std::uint64_t frame{};
    std::uint64_t guest_ticks{};
    std::uint64_t host_thread{};
    std::uint64_t guest_thread{};
    LogLevel level{LogLevel::info};
    std::string category;
    std::string message;
    std::vector<LogField> fields;
};

struct LogContext {
    std::uint64_t frame{};
    std::uint64_t guest_ticks{};
    std::uint64_t host_thread{};
    std::uint64_t guest_thread{};
};

class Logger final {
public:
    explicit Logger(std::size_t ring_capacity = 4096);

    void Write(LogLevel level,
               std::string_view category,
               std::string_view message,
               const LogContext& context = {},
               std::vector<LogField> fields = {});

    [[nodiscard]] std::vector<LogRecord> Snapshot(
        std::optional<LogLevel> minimum = std::nullopt,
        std::string_view category_prefix = {},
        std::size_t limit = 0) const;
    [[nodiscard]] std::size_t Capacity() const noexcept;
    [[nodiscard]] std::uint64_t DroppedCount() const;

private:
    std::size_t capacity_;
    mutable std::mutex mutex_;
    std::deque<LogRecord> records_;
    std::uint64_t dropped_{};
};

[[nodiscard]] std::string_view ToString(LogLevel level) noexcept;

}  // namespace ogplay::core

