#include "ogplay/core/logger.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <utility>

#include "ogplay/core/capability_ledger.h"

namespace ogplay::core {
namespace {

std::string JsonString(const std::string_view input) {
    std::ostringstream output;
    output << '"';
    for (const char raw_character : input) {
        const auto character = static_cast<unsigned char>(raw_character);
        switch (character) {
        case '\\': output << "\\\\"; break;
        case '"': output << "\\\""; break;
        case '\n': output << "\\n"; break;
        case '\r': output << "\\r"; break;
        case '\t': output << "\\t"; break;
        default:
            if (character < 0x20U) {
                output << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                       << static_cast<unsigned int>(character) << std::dec;
            } else {
                output << static_cast<char>(character);
            }
        }
    }
    output << '"';
    return output.str();
}

std::string Hex(const std::uint64_t value) {
    std::ostringstream output;
    output << "0x" << std::hex << value;
    return output.str();
}

std::string RenderFieldText(const FieldValue& value,
                            const GuestSymbolProvider* symbols) {
    return std::visit([symbols](const auto& item) -> std::string {
        using Value = std::decay_t<decltype(item)>;
        if constexpr (std::is_same_v<Value, std::string>) {
            return item;
        } else if constexpr (std::is_same_v<Value, bool>) {
            return item ? "true" : "false";
        } else if constexpr (std::is_same_v<Value, GuestAddress>) {
            std::string rendered = Hex(item.value);
            if (symbols != nullptr) {
                if (const auto symbol = symbols->Resolve(item.value)) {
                    rendered += " (" + symbol->module + " + " + Hex(symbol->offset);
                    if (!symbol->symbol.empty()) rendered += " " + symbol->symbol;
                    if (!symbol->source_hint.empty()) rendered += " " + symbol->source_hint;
                    rendered += ')';
                }
            }
            return rendered;
        } else {
            std::ostringstream output;
            output << item;
            return output.str();
        }
    }, value);
}

std::string RenderFieldJson(const FieldValue& value,
                            const GuestSymbolProvider* symbols) {
    return std::visit([symbols](const auto& item) -> std::string {
        using Value = std::decay_t<decltype(item)>;
        if constexpr (std::is_same_v<Value, std::string>) {
            return JsonString(item);
        } else if constexpr (std::is_same_v<Value, bool>) {
            return item ? "true" : "false";
        } else if constexpr (std::is_same_v<Value, GuestAddress>) {
            std::ostringstream output;
            output << "{\"address\":" << item.value;
            if (symbols != nullptr) {
                if (const auto symbol = symbols->Resolve(item.value)) {
                    output << ",\"module\":" << JsonString(symbol->module)
                           << ",\"symbol\":" << JsonString(symbol->symbol)
                           << ",\"offset\":" << symbol->offset
                           << ",\"source_hint\":" << JsonString(symbol->source_hint);
                }
            }
            output << '}';
            return output.str();
        } else {
            std::ostringstream output;
            output << item;
            return output.str();
        }
    }, value);
}

void WriteUtf8(const std::filesystem::path& path, const std::string_view content) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) throw std::runtime_error("cannot create diagnostic file: " + path.string());
    output.write(content.data(), static_cast<std::streamsize>(content.size()));
    if (!output) throw std::runtime_error("cannot write diagnostic file: " + path.string());
}

}  // namespace

ConsoleSink::ConsoleSink(std::FILE* stream, const LogFormat format)
    : stream_(stream), format_(format) {
    if (stream_ == nullptr) throw std::invalid_argument("console sink stream must not be null");
}

void ConsoleSink::Consume(const LogRecord&,
                          const std::string_view rendered_text,
                          const std::string_view rendered_json) {
    const auto output = format_ == LogFormat::jsonl ? rendered_json : rendered_text;
    std::scoped_lock lock(mutex_);
    static_cast<void>(std::fwrite(output.data(), sizeof(char), output.size(), stream_));
    static_cast<void>(std::fwrite("\n", sizeof(char), 1, stream_));
    std::fflush(stream_);
}

class FileSink::Impl final {
public:
    Impl(const std::filesystem::path& path, const LogFormat format)
        : output(path, std::ios::binary | std::ios::app), format(format) {
        if (!output) throw std::runtime_error("cannot open log sink: " + path.string());
    }

    std::ofstream output;
    LogFormat format;
    std::mutex mutex;
};

FileSink::FileSink(std::filesystem::path path, const LogFormat format)
    : impl_(std::make_unique<Impl>(path, format)) {}

FileSink::~FileSink() = default;

void FileSink::Consume(const LogRecord&,
                       const std::string_view rendered_text,
                       const std::string_view rendered_json) {
    const auto output = impl_->format == LogFormat::jsonl ? rendered_json : rendered_text;
    std::scoped_lock lock(impl_->mutex);
    impl_->output << output << '\n';
    impl_->output.flush();
}

Logger::Logger(const std::size_t ring_capacity) : capacity_(ring_capacity) {
    if (capacity_ == 0) throw std::invalid_argument("logger ring capacity must be greater than zero");
}

void Logger::AddSink(std::shared_ptr<LogSink> sink,
                     const LogLevel minimum,
                     std::string category_prefix) {
    if (!sink) throw std::invalid_argument("log sink must not be null");
    std::scoped_lock lock(mutex_);
    sinks_.push_back({std::move(sink), minimum, std::move(category_prefix)});
}

void Logger::SetSymbolProvider(std::shared_ptr<const GuestSymbolProvider> provider) {
    std::scoped_lock lock(mutex_);
    symbol_provider_ = std::move(provider);
}

bool Logger::AcceptRateLimit(const std::string& key,
                             const LogLevel level,
                             const LogContext& context,
                             const RateLimitPolicy& requested) {
    auto policy = requested;
    if (policy.mode == RateLimitMode::automatic) {
        policy.mode = level < LogLevel::warn ? RateLimitMode::first_and_count : RateLimitMode::none;
    }
    if (policy.mode == RateLimitMode::none) return true;

    auto& state = rate_states_[key];
    bool accepted = false;
    switch (policy.mode) {
    case RateLimitMode::first_and_count:
        accepted = !state.emitted;
        break;
    case RateLimitMode::every_n_frames:
        if (policy.frame_interval == 0) throw std::invalid_argument("frame interval must not be zero");
        accepted = context.frame % policy.frame_interval == 0;
        break;
    case RateLimitMode::token_bucket: {
        if (policy.tokens_per_second <= 0.0 || policy.burst < 1.0) {
            throw std::invalid_argument("token bucket rate and burst must be positive");
        }
        const auto now = std::chrono::steady_clock::now();
        if (!state.emitted) {
            state.tokens = policy.burst;
        } else {
            const auto seconds = std::chrono::duration<double>(now - state.last_refill).count();
            state.tokens = std::min(policy.burst, state.tokens + seconds * policy.tokens_per_second);
        }
        state.last_refill = now;
        accepted = state.tokens >= 1.0;
        if (accepted) state.tokens -= 1.0;
        break;
    }
    case RateLimitMode::automatic:
    case RateLimitMode::none:
        break;
    }
    state.emitted = state.emitted || accepted;
    if (!accepted) ++state.suppressed;
    return accepted;
}

void Logger::Write(const LogLevel level,
                   const std::string_view category,
                   const std::string_view message,
                   const LogContext& context,
                   std::vector<LogField> fields,
                   const RateLimitPolicy& rate_limit) {
    if (category.empty()) throw std::invalid_argument("structured log category must not be empty");
    if (message.empty()) throw std::invalid_argument("structured log message must not be empty");

    LogRecord record{
        .wall = std::chrono::system_clock::now(), .frame = context.frame,
        .guest_ticks = context.guest_ticks, .host_thread = context.host_thread,
        .guest_thread = context.guest_thread, .level = level,
        .category = std::string(category), .message = std::string(message),
        .fields = std::move(fields),
    };
    std::vector<SinkBinding> sinks;
    {
        std::scoped_lock lock(mutex_);
        const auto key = record.category + '\n' + record.message;
        if (!AcceptRateLimit(key, level, context, rate_limit)) return;
        if (records_.size() == capacity_) {
            records_.pop_front();
            ++dropped_;
        }
        records_.push_back(record);
        sinks = sinks_;
    }

    const auto text = RenderText(record);
    const auto json = RenderJson(record);
    for (const auto& binding : sinks) {
        if (record.level >= binding.minimum &&
            (binding.category_prefix.empty() || record.category.starts_with(binding.category_prefix))) {
            binding.sink->Consume(record, text, json);
        }
    }
}

void Logger::BeginFrame(const LogContext& context) {
    Write(LogLevel::trace, "session.frame", "frame begin", context, {},
          {.mode = RateLimitMode::none});
}

void Logger::EndFrame(const LogContext& context) {
    Write(LogLevel::trace, "session.frame", "frame end", context, {},
          {.mode = RateLimitMode::none});
}

std::vector<LogRecord> Logger::Snapshot(const std::optional<LogLevel> minimum,
                                        const std::string_view category_prefix,
                                        const std::size_t limit) const {
    std::scoped_lock lock(mutex_);
    std::vector<LogRecord> result;
    for (const auto& record : records_) {
        if (minimum && record.level < *minimum) continue;
        if (!category_prefix.empty() && !record.category.starts_with(category_prefix)) continue;
        result.push_back(record);
    }
    if (limit != 0 && result.size() > limit) {
        result.erase(result.begin(), result.end() - static_cast<std::ptrdiff_t>(limit));
    }
    return result;
}

std::string Logger::RenderText(const LogRecord& record) const {
    std::shared_ptr<const GuestSymbolProvider> symbols;
    {
        std::scoped_lock lock(mutex_);
        symbols = symbol_provider_;
    }
    std::ostringstream output;
    output << ToString(record.level) << " [f=" << record.frame << "] "
           << record.category << " " << record.message;
    for (const auto& field : record.fields) {
        output << " " << field.key << "=" << RenderFieldText(field.value, symbols.get());
    }
    return output.str();
}

std::string Logger::RenderJson(const LogRecord& record) const {
    std::shared_ptr<const GuestSymbolProvider> symbols;
    {
        std::scoped_lock lock(mutex_);
        symbols = symbol_provider_;
    }
    const auto wall_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        record.wall.time_since_epoch()).count();
    std::ostringstream output;
    output << "{\"ts_ms\":" << wall_ms << ",\"frame\":" << record.frame
           << ",\"guest_ticks\":" << record.guest_ticks
           << ",\"host_thread\":" << record.host_thread
           << ",\"guest_thread\":" << record.guest_thread
           << ",\"level\":" << JsonString(ToString(record.level))
           << ",\"category\":" << JsonString(record.category)
           << ",\"message\":" << JsonString(record.message) << ",\"fields\":{";
    bool first = true;
    for (const auto& field : record.fields) {
        if (!first) output << ',';
        first = false;
        output << JsonString(field.key) << ':' << RenderFieldJson(field.value, symbols.get());
    }
    output << "}}";
    return output.str();
}

std::optional<SymbolizedAddress> Logger::ResolveGuestAddress(
    const std::uint64_t address) const {
    std::shared_ptr<const GuestSymbolProvider> symbols;
    {
        std::scoped_lock lock(mutex_);
        symbols = symbol_provider_;
    }
    return symbols ? symbols->Resolve(address) : std::nullopt;
}

std::uint64_t Logger::SuppressedCount(const std::string_view category,
                                      const std::string_view message) const {
    std::scoped_lock lock(mutex_);
    const auto item = rate_states_.find(std::string(category) + '\n' + std::string(message));
    return item == rate_states_.end() ? 0 : item->second.suppressed;
}

std::size_t Logger::Capacity() const noexcept { return capacity_; }

std::uint64_t Logger::DroppedCount() const {
    std::scoped_lock lock(mutex_);
    return dropped_;
}

void Logger::DumpRingJsonl(const std::filesystem::path& path) const {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) throw std::runtime_error("cannot create ring dump: " + path.string());
    for (const auto& record : Snapshot()) output << RenderJson(record) << '\n';
}

void Logger::DumpDiagnosticBundle(const std::filesystem::path& directory,
                                  const CapabilityLedger& ledger,
                                  const std::string_view summary,
                                  const std::string_view session_json) const {
    std::filesystem::create_directories(directory);
    WriteUtf8(directory / "summary.txt", summary);
    WriteUtf8(directory / "session.json", session_json);
    DumpRingJsonl(directory / "ring.jsonl");

    std::ostringstream unimplemented;
    unimplemented << "{\"unimplemented\":[";
    bool first = true;
    for (const auto& hit : ledger.Unimplemented()) {
        if (!first) unimplemented << ',';
        first = false;
        unimplemented << "{\"symbol\":" << JsonString(hit.id)
                      << ",\"count\":" << hit.count
                      << ",\"first_lr\":" << hit.first_lr
                      << ",\"last_lr\":" << hit.last_lr << '}';
    }
    unimplemented << "]}";
    WriteUtf8(directory / "unimplemented.json", unimplemented.str());
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
