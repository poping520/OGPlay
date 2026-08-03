#pragma once

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

namespace ogplay::core {

class CapabilityLedger;

enum class LogLevel : std::uint8_t { trace, debug, info, warn, error, fatal };
enum class LogFormat : std::uint8_t { text, jsonl };
enum class RateLimitMode : std::uint8_t { automatic, none, first_and_count, every_n_frames, token_bucket };

struct GuestAddress {
    std::uint64_t value{};
};

struct SymbolizedAddress {
    std::string module;
    std::string symbol;
    std::uint64_t offset{};
    std::string source_hint;
};

class GuestSymbolProvider {
public:
    virtual ~GuestSymbolProvider() = default;
    [[nodiscard]] virtual std::optional<SymbolizedAddress> Resolve(
        std::uint64_t address) const = 0;
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

struct RateLimitPolicy {
    RateLimitMode mode{RateLimitMode::automatic};
    std::uint64_t frame_interval{1};
    double tokens_per_second{1.0};
    double burst{1.0};
};

class LogSink {
public:
    virtual ~LogSink() = default;
    virtual void Consume(const LogRecord& record,
                         std::string_view rendered_text,
                         std::string_view rendered_json) = 0;
};

class ConsoleSink final : public LogSink {
public:
    explicit ConsoleSink(std::FILE* stream = stderr, LogFormat format = LogFormat::text);
    void Consume(const LogRecord& record,
                 std::string_view rendered_text,
                 std::string_view rendered_json) override;

private:
    std::FILE* stream_;
    LogFormat format_;
    std::mutex mutex_;
};

class FileSink final : public LogSink {
public:
    explicit FileSink(std::filesystem::path path, LogFormat format = LogFormat::text);
    ~FileSink() override;
    FileSink(const FileSink&) = delete;
    FileSink& operator=(const FileSink&) = delete;

    void Consume(const LogRecord& record,
                 std::string_view rendered_text,
                 std::string_view rendered_json) override;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

class Logger final {
public:
    explicit Logger(std::size_t ring_capacity = 4096);

    void AddSink(std::shared_ptr<LogSink> sink,
                 LogLevel minimum = LogLevel::info,
                 std::string category_prefix = {});
    void SetSymbolProvider(std::shared_ptr<const GuestSymbolProvider> provider);

    void Write(LogLevel level,
               std::string_view category,
               std::string_view message,
               const LogContext& context = {},
               std::vector<LogField> fields = {},
               const RateLimitPolicy& rate_limit = {});

    void BeginFrame(const LogContext& context);
    void EndFrame(const LogContext& context);

    [[nodiscard]] std::vector<LogRecord> Snapshot(
        std::optional<LogLevel> minimum = std::nullopt,
        std::string_view category_prefix = {},
        std::size_t limit = 0) const;
    [[nodiscard]] std::string RenderText(const LogRecord& record) const;
    [[nodiscard]] std::string RenderJson(const LogRecord& record) const;
    [[nodiscard]] std::optional<SymbolizedAddress> ResolveGuestAddress(
        std::uint64_t address) const;
    [[nodiscard]] std::uint64_t SuppressedCount(std::string_view category,
                                                std::string_view message) const;
    [[nodiscard]] std::size_t Capacity() const noexcept;
    [[nodiscard]] std::uint64_t DroppedCount() const;

    void DumpRingJsonl(const std::filesystem::path& path) const;
    void DumpDiagnosticBundle(const std::filesystem::path& directory,
                              const CapabilityLedger& ledger,
                              std::string_view summary,
                              std::string_view session_json) const;

private:
    struct SinkBinding {
        std::shared_ptr<LogSink> sink;
        LogLevel minimum{LogLevel::info};
        std::string category_prefix;
    };
    struct RateState {
        bool emitted{};
        double tokens{};
        std::chrono::steady_clock::time_point last_refill{};
        std::uint64_t suppressed{};
    };

    [[nodiscard]] bool AcceptRateLimit(const std::string& key,
                                       LogLevel level,
                                       const LogContext& context,
                                       const RateLimitPolicy& policy);

    std::size_t capacity_;
    mutable std::mutex mutex_;
    std::deque<LogRecord> records_;
    std::vector<SinkBinding> sinks_;
    std::unordered_map<std::string, RateState> rate_states_;
    std::shared_ptr<const GuestSymbolProvider> symbol_provider_;
    std::uint64_t dropped_{};
};

[[nodiscard]] std::string_view ToString(LogLevel level) noexcept;

}  // namespace ogplay::core
