#include "log_module.h"

#include <algorithm>
#include <bit>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <limits>
#include <mutex>
#include <optional>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "ogplay/core/logger.h"
#include "ogplay/memory/address_space.h"
#include "runtime/boundary/core/a32_call_frame.h"

namespace ogplay::runtime {
namespace {

constexpr std::size_t kMaximumLogBytes = 4096U;
constexpr std::size_t kMaximumBinaryLogBytes = 64U * 1024U;
constexpr std::uint32_t kFirstFormatHandle = 0x6e004000U;
constexpr std::uint32_t kHandleStride = 4U;
constexpr std::uint32_t kEventTagPoolBegin = 0x70f00000U;
constexpr std::uint32_t kEventTagPoolEnd = 0x71000000U;

constexpr std::int32_t kLogVerbose = 2;
constexpr std::int32_t kLogDebug = 3;
constexpr std::int32_t kLogInfo = 4;
constexpr std::int32_t kLogWarn = 5;
constexpr std::int32_t kLogError = 6;
constexpr std::int32_t kLogFatal = 7;
constexpr std::int32_t kLogSilent = 8;

std::uint32_t Signed(const std::int32_t value) noexcept {
    return std::bit_cast<std::uint32_t>(value);
}

core::LogLevel ProjectLevel(const std::int32_t priority) noexcept {
    if (priority >= kLogFatal) return core::LogLevel::fatal;
    if (priority >= kLogError) return core::LogLevel::error;
    if (priority >= kLogWarn) return core::LogLevel::warn;
    if (priority >= kLogInfo) return core::LogLevel::info;
    if (priority >= kLogDebug) return core::LogLevel::debug;
    return core::LogLevel::trace;
}

char PriorityLetter(const std::int32_t priority) noexcept {
    constexpr std::string_view letters{"??VDIWEFS"};
    if (priority < 0 || static_cast<std::size_t>(priority) >= letters.size()) {
        return '?';
    }
    return letters[static_cast<std::size_t>(priority)];
}

std::int32_t PriorityFromLetter(const char value) noexcept {
    switch (static_cast<char>(std::tolower(static_cast<unsigned char>(value)))) {
        case 'v': return kLogVerbose;
        case 'd': return kLogDebug;
        case 'i': return kLogInfo;
        case 'w': return kLogWarn;
        case 'e': return kLogError;
        case 'f': return kLogFatal;
        case 's': return kLogSilent;
        default: return -1;
    }
}

std::string ReadCString(memory::AddressSpace& memory,
                        const GuestCString value,
                        const std::uint64_t thread_id,
                        const std::size_t maximum = kMaximumLogBytes) {
    if (value.IsNull()) return {};
    const auto length = memory.CStringLength(value.Address(), maximum, thread_id);
    std::vector<std::byte> bytes(length);
    memory.Read(value.Address(), bytes, thread_id);
    std::string result(length, '\0');
    for (std::size_t index = 0; index < length; ++index) {
        result[index] = static_cast<char>(std::to_integer<unsigned char>(bytes[index]));
    }
    return result;
}

std::vector<std::byte> ReadBytes(memory::AddressSpace& memory,
                                 const GuestPtr<const void> value,
                                 const std::size_t size,
                                 const std::uint64_t thread_id) {
    if (size > kMaximumBinaryLogBytes) {
        throw std::length_error("guest liblog binary payload exceeds limit");
    }
    if (size != 0U && value.IsNull()) {
        throw std::invalid_argument("guest liblog binary payload is null");
    }
    std::vector<std::byte> result(size);
    if (!result.empty()) memory.Read(value.Address(), result, thread_id);
    return result;
}

std::string HexPayload(const std::span<const std::byte> bytes) {
    std::ostringstream stream;
    stream << std::hex << std::setfill('0');
    const auto count = std::min<std::size_t>(bytes.size(), 64U);
    for (std::size_t index = 0; index < count; ++index) {
        if (index != 0U) stream << ' ';
        stream << std::setw(2) << std::to_integer<unsigned int>(bytes[index]);
    }
    if (bytes.size() > count) stream << " ...";
    return stream.str();
}

std::uint32_t ReadLittle32(const std::span<const std::byte> bytes,
                           const std::size_t offset) {
    if (offset + 4U > bytes.size()) {
        throw std::invalid_argument("truncated guest binary log item");
    }
    std::uint32_t result{};
    for (std::size_t index = 0; index < 4U; ++index) {
        result |= static_cast<std::uint32_t>(
            std::to_integer<std::uint8_t>(bytes[offset + index])) << (index * 8U);
    }
    return result;
}

std::string ParseBinaryEvent(const std::span<const std::byte> bytes,
                             std::size_t& cursor, const std::size_t depth = 0U) {
    if (depth > 8U || cursor >= bytes.size()) {
        throw std::invalid_argument("invalid guest binary log nesting");
    }
    const auto type = std::to_integer<std::uint8_t>(bytes[cursor++]);
    switch (type) {
        case 0: {
            const auto value = std::bit_cast<std::int32_t>(ReadLittle32(bytes, cursor));
            cursor += 4U;
            return std::to_string(value);
        }
        case 1: {
            const auto low = ReadLittle32(bytes, cursor);
            const auto high = ReadLittle32(bytes, cursor + 4U);
            cursor += 8U;
            return std::to_string(std::bit_cast<std::int64_t>(
                static_cast<std::uint64_t>(low) |
                (static_cast<std::uint64_t>(high) << 32U)));
        }
        case 2: {
            const auto length = ReadLittle32(bytes, cursor);
            cursor += 4U;
            if (length > bytes.size() - cursor) {
                throw std::invalid_argument("truncated guest binary log string");
            }
            std::string result(length, '\0');
            for (std::size_t index = 0; index < length; ++index) {
                result[index] = static_cast<char>(
                    std::to_integer<unsigned char>(bytes[cursor + index]));
            }
            cursor += length;
            return result;
        }
        case 3: {
            if (cursor >= bytes.size()) {
                throw std::invalid_argument("truncated guest binary log list");
            }
            const auto count = std::to_integer<std::uint8_t>(bytes[cursor++]);
            std::string result{"["};
            for (std::size_t index = 0; index < count; ++index) {
                if (index != 0U) result += ',';
                result += ParseBinaryEvent(bytes, cursor, depth + 1U);
            }
            result += ']';
            return result;
        }
        case 4: {
            const auto value = std::bit_cast<float>(ReadLittle32(bytes, cursor));
            cursor += 4U;
            std::ostringstream stream;
            stream << value;
            return stream.str();
        }
        default:
            throw std::invalid_argument("unknown guest binary log item type");
    }
}

class WordReader final {
public:
    WordReader(memory::AddressSpace& memory, const A32CallFrame& call,
               const std::size_t first)
        : memory_(memory), call_(call), cursor_(first), thread_id_(call.ThreadId()) {}
    WordReader(memory::AddressSpace& memory, const A32CallFrame& call,
               const memory::GuestAddress list)
        : memory_(memory), call_(call), list_(list), thread_id_(call.ThreadId()) {}

    std::uint32_t Next() {
        if (list_.has_value()) {
            const auto result = memory_.Read32(list_->Add(list_cursor_ * 4U), thread_id_);
            ++list_cursor_;
            return result;
        }
        if (cursor_ >= call_.Arguments().size()) {
            throw std::length_error("guest liblog format consumes too many A32 arguments");
        }
        return call_.Argument(cursor_++);
    }

    std::uint64_t Next64() {
        if (list_.has_value()) {
            if ((((list_->Value() / 4U) + list_cursor_) & 1U) != 0U) {
                ++list_cursor_;
            }
        } else if ((cursor_ & 1U) != 0U) {
            ++cursor_;
        }
        const auto low = Next();
        const auto high = Next();
        return static_cast<std::uint64_t>(low) |
               (static_cast<std::uint64_t>(high) << 32U);
    }

private:
    memory::AddressSpace& memory_;
    const A32CallFrame& call_;
    std::optional<memory::GuestAddress> list_;
    std::size_t cursor_{};
    std::size_t list_cursor_{};
    std::uint64_t thread_id_{};
};

std::string FormatGuest(memory::AddressSpace& memory, const A32CallFrame& call,
                        const std::string_view format, WordReader& words) {
    std::string output;
    output.reserve(std::min<std::size_t>(format.size() + 64U, kMaximumLogBytes));
    for (std::size_t cursor = 0; cursor < format.size() && output.size() < kMaximumLogBytes;
         ++cursor) {
        if (format[cursor] != '%') {
            output.push_back(format[cursor]);
            continue;
        }
        if (++cursor >= format.size()) break;
        if (format[cursor] == '%') {
            output.push_back('%');
            continue;
        }
        while (cursor < format.size() && std::string_view{"-+ #0"}.find(format[cursor]) !=
                                             std::string_view::npos) {
            ++cursor;
        }
        if (cursor < format.size() && format[cursor] == '*') {
            static_cast<void>(words.Next());
            ++cursor;
        } else {
            while (cursor < format.size() && std::isdigit(
                       static_cast<unsigned char>(format[cursor]))) ++cursor;
        }
        if (cursor < format.size() && format[cursor] == '.') {
            ++cursor;
            if (cursor < format.size() && format[cursor] == '*') {
                static_cast<void>(words.Next());
                ++cursor;
            } else {
                while (cursor < format.size() && std::isdigit(
                           static_cast<unsigned char>(format[cursor]))) ++cursor;
            }
        }
        bool wide = false;
        if (cursor < format.size() && (format[cursor] == 'l' || format[cursor] == 'j')) {
            wide = format[cursor] == 'j' ||
                   (cursor + 1U < format.size() && format[cursor + 1U] == 'l');
            cursor += wide && format[cursor] == 'l' ? 2U : 1U;
        } else if (cursor < format.size() &&
                   std::string_view{"hztL"}.find(format[cursor]) != std::string_view::npos) {
            wide = format[cursor] == 'L';
            ++cursor;
            if (cursor < format.size() && format[cursor - 1U] == 'h' &&
                format[cursor] == 'h') ++cursor;
        }
        if (cursor >= format.size()) break;
        const auto conversion = format[cursor];
        std::ostringstream value;
        switch (conversion) {
            case 's':
                output += ReadCString(memory,
                    GuestCString{memory::GuestAddress{words.Next()}}, call.ThreadId());
                continue;
            case 'c': value << static_cast<char>(words.Next()); break;
            case 'd': case 'i':
                if (wide) value << static_cast<std::int64_t>(words.Next64());
                else value << std::bit_cast<std::int32_t>(words.Next());
                break;
            case 'u':
                if (wide) value << words.Next64(); else value << words.Next();
                break;
            case 'x': case 'X': case 'o': {
                if (conversion == 'o') value << std::oct;
                else value << std::hex << (conversion == 'X' ? std::uppercase : std::nouppercase);
                if (wide) value << words.Next64(); else value << words.Next();
                break;
            }
            case 'p': value << "0x" << std::hex << words.Next(); break;
            case 'f': case 'F': case 'e': case 'E': case 'g': case 'G': {
                const auto number = std::bit_cast<double>(words.Next64());
                if (conversion == 'e' || conversion == 'E') value << std::scientific;
                value << number;
                break;
            }
            case 'n': {
                const auto address = memory::GuestAddress{words.Next()};
                if (!address.IsNull()) memory.Write32(address,
                    static_cast<std::uint32_t>(output.size()), call.ThreadId());
                continue;
            }
            default:
                output.push_back('%');
                output.push_back(conversion);
                continue;
        }
        output += value.str();
    }
    if (output.size() > kMaximumLogBytes) output.resize(kMaximumLogBytes);
    return output;
}

struct GuestLogEntry final {
    std::int32_t seconds{};
    std::int32_t nanoseconds{};
    std::int32_t priority{};
    std::int32_t pid{};
    std::int32_t tid{};
    std::uint32_t tag{};
    std::uint32_t message_length{};
    std::uint32_t message{};
};

GuestLogEntry ReadEntry(memory::AddressSpace& memory,
                        const memory::GuestAddress address,
                        const std::uint64_t thread_id) {
    GuestLogEntry result;
    result.seconds = std::bit_cast<std::int32_t>(memory.Read32(address, thread_id));
    result.nanoseconds = std::bit_cast<std::int32_t>(memory.Read32(address.Add(4), thread_id));
    result.priority = std::bit_cast<std::int32_t>(memory.Read32(address.Add(8), thread_id));
    result.pid = std::bit_cast<std::int32_t>(memory.Read32(address.Add(12), thread_id));
    result.tid = std::bit_cast<std::int32_t>(memory.Read32(address.Add(16), thread_id));
    result.tag = memory.Read32(address.Add(20), thread_id);
    result.message_length = memory.Read32(address.Add(24), thread_id);
    result.message = memory.Read32(address.Add(28), thread_id);
    return result;
}

void WriteEntry(memory::AddressSpace& memory, const memory::GuestAddress address,
                const GuestLogEntry& entry, const std::uint64_t thread_id) {
    memory.Validate({address, 32U}, memory::AccessType::write, thread_id);
    memory.Write32(address, std::bit_cast<std::uint32_t>(entry.seconds), thread_id);
    memory.Write32(address.Add(4), std::bit_cast<std::uint32_t>(entry.nanoseconds), thread_id);
    memory.Write32(address.Add(8), std::bit_cast<std::uint32_t>(entry.priority), thread_id);
    memory.Write32(address.Add(12), std::bit_cast<std::uint32_t>(entry.pid), thread_id);
    memory.Write32(address.Add(16), std::bit_cast<std::uint32_t>(entry.tid), thread_id);
    memory.Write32(address.Add(20), entry.tag, thread_id);
    memory.Write32(address.Add(24), entry.message_length, thread_id);
    memory.Write32(address.Add(28), entry.message, thread_id);
}

}  // namespace

class LogModule::Impl final {
public:
    explicit Impl(LogBoundaryContext& context) : context_(context) {}

    struct FormatState final {
        std::int32_t print_format{1};
        std::int32_t global_priority{kLogVerbose};
        std::unordered_map<std::string, std::int32_t> tag_priorities;
    };
    struct EventTagMapState final {
        std::unordered_map<std::int32_t, std::uint32_t> tags;
    };

    std::uint32_t Emit(const std::int32_t buffer, const std::int32_t priority,
                       std::string tag, std::string message,
                       const std::uint64_t thread_id) {
        if (tag.empty()) tag = "<null>";
        const auto bytes = std::min<std::size_t>(message.size() + tag.size() + 3U,
                                                 std::numeric_limits<std::int32_t>::max());
        if (context_.logger != nullptr) {
            const auto rendered = "[guest] " + tag + ": " + message;
            context_.logger->Write(ProjectLevel(priority), "guest.liblog",
                rendered,
                {.guest_thread = thread_id},
                {{"guest_log_tag", tag},
                 {"guest_log_priority", static_cast<std::int64_t>(priority)},
                 {"guest_log_buffer", static_cast<std::int64_t>(buffer)}});
        }
        return static_cast<std::uint32_t>(bytes);
    }

    FormatState* FindFormat(const std::uint32_t handle) {
        const auto found = formats_.find(handle);
        return found == formats_.end() ? nullptr : &found->second;
    }

    std::uint32_t NewFormat() {
        std::scoped_lock lock(mutex_);
        const auto handle = next_handle_;
        next_handle_ += kHandleStride;
        formats_.emplace(handle, FormatState{});
        return handle;
    }

    bool RemoveFormat(const std::uint32_t handle) {
        std::scoped_lock lock(mutex_);
        return formats_.erase(handle) != 0U;
    }

    std::uint32_t OpenEventMap(const std::string_view path) {
        if (context_.read_guest_file == nullptr) return 0U;
        std::vector<std::byte> bytes;
        if (!context_.read_guest_file(context_.guest_file_owner, path, bytes)) return 0U;
        std::string contents(bytes.size(), '\0');
        for (std::size_t index = 0; index < bytes.size(); ++index) {
            contents[index] = static_cast<char>(
                std::to_integer<unsigned char>(bytes[index]));
        }
        std::vector<std::pair<std::int32_t, std::string>> parsed;
        std::istringstream lines(contents);
        std::string line;
        std::size_t string_bytes{};
        while (std::getline(lines, line)) {
            const auto comment = line.find('#');
            if (comment != std::string::npos) line.resize(comment);
            std::istringstream fields(line);
            std::int32_t tag{};
            std::string name;
            if (!(fields >> tag >> name)) continue;
            if (name.size() > kMaximumLogBytes) return 0U;
            string_bytes += name.size() + 1U;
            parsed.emplace_back(tag, std::move(name));
        }
        if (parsed.empty()) return 0U;
        std::scoped_lock lock(mutex_);
        if (string_bytes > kEventTagPoolEnd - kEventTagPoolBegin) return 0U;
        const auto page_size = static_cast<std::uint32_t>(
            context_.address_space.PageSize());
        const auto mapping_bytes = static_cast<std::uint32_t>(
            ((string_bytes + page_size - 1U) / page_size) * page_size);
        if (event_tag_pool_cursor_ > kEventTagPoolEnd - mapping_bytes) return 0U;
        const memory::GuestAddress mapping{event_tag_pool_cursor_};
        context_.address_space.Map(
            {mapping, mapping_bytes},
            memory::PageProtection::read | memory::PageProtection::write);
        EventTagMapState state;
        auto cursor = mapping;
        for (const auto& [tag, name] : parsed) {
            std::vector<std::byte> encoded(name.size() + 1U);
            for (std::size_t index = 0; index < name.size(); ++index) {
                encoded[index] = static_cast<std::byte>(
                    static_cast<unsigned char>(name[index]));
            }
            context_.address_space.Write(cursor, encoded);
            state.tags.emplace(tag, cursor.Value());
            cursor = cursor.Add(encoded.size());
        }
        context_.address_space.Protect(
            {mapping, mapping_bytes}, memory::PageProtection::read);
        event_tag_pool_cursor_ += mapping_bytes;
        const auto handle = next_handle_;
        next_handle_ += kHandleStride;
        event_maps_.emplace(handle, std::move(state));
        return handle;
    }

    void CloseEventMap(const std::uint32_t handle) {
        std::scoped_lock lock(mutex_);
        event_maps_.erase(handle);
    }

    std::uint32_t LookupEventTag(const std::uint32_t handle,
                                 const std::int32_t tag) {
        std::scoped_lock lock(mutex_);
        const auto map = event_maps_.find(handle);
        if (map == event_maps_.end()) return 0U;
        const auto found = map->second.tags.find(tag);
        return found == map->second.tags.end() ? 0U : found->second;
    }

    std::int32_t AddRule(FormatState& format, const std::string_view rule) {
        if (rule.empty()) return -1;
        const auto separator = rule.rfind(':');
        const auto tag = separator == std::string_view::npos ? rule : rule.substr(0, separator);
        const auto priority = separator == std::string_view::npos
            ? kLogVerbose
            : (separator + 1U < rule.size() ? PriorityFromLetter(rule[separator + 1U]) : -1);
        if (tag.empty() || priority < 0) return -1;
        if (tag == "*") format.global_priority = priority;
        else format.tag_priorities[std::string(tag)] = priority;
        return 0;
    }

    bool ShouldPrint(const FormatState& format, const std::string_view tag,
                     const std::int32_t priority) const {
        const auto found = format.tag_priorities.find(std::string(tag));
        const auto threshold = found == format.tag_priorities.end()
            ? format.global_priority : found->second;
        return threshold != kLogSilent && priority >= threshold;
    }

    std::string FormatLine(const FormatState& format, const GuestLogEntry& entry,
                           const std::string_view tag,
                           const std::string_view message) const {
        std::ostringstream line;
        switch (format.print_format) {
            case 0: return {};
            case 3: line << PriorityLetter(entry.priority) << '/' << tag << ": " << message; break;
            case 5: line << message; break;
            case 7:
                line << entry.seconds << '.' << std::setw(9) << std::setfill('0')
                     << entry.nanoseconds << ' ' << entry.pid << ' ' << entry.tid << ' '
                     << PriorityLetter(entry.priority) << ' ' << tag << ": " << message;
                break;
            case 8:
                line << "[ " << entry.seconds << '.' << std::setw(9) << std::setfill('0')
                     << entry.nanoseconds << ' ' << entry.pid << ':' << entry.tid << ' '
                     << PriorityLetter(entry.priority) << '/' << tag << " ]\n"
                     << message << "\n";
                return line.str();
            default:
                line << PriorityLetter(entry.priority) << '/' << tag << '(' << entry.pid
                     << "): " << message;
                break;
        }
        line << '\n';
        return line.str();
    }

    LogBoundaryContext& context_;
    std::mutex mutex_;
    std::uint32_t next_handle_{kFirstFormatHandle};
    std::uint32_t event_tag_pool_cursor_{kEventTagPoolBegin};
    std::unordered_map<std::uint32_t, FormatState> formats_;
    std::unordered_map<std::uint32_t, EventTagMapState> event_maps_;
};

LogModule::LogModule(LogBoundaryContext& context)
    : impl_(std::make_unique<Impl>(context)) {}
LogModule::~LogModule() = default;
LogBoundaryContext& LogModule::CallServices() noexcept {
    return impl_->context_;
}

std::uint32_t LogModule::DevAvailable(const A32CallFrame&) { return 1U; }

std::uint32_t LogModule::Write(const A32CallFrame& call) {
    return impl_->Emit(0, call.Scalar<std::int32_t>(0),
        ReadCString(impl_->context_.address_space, call.CString(1), call.ThreadId()),
        ReadCString(impl_->context_.address_space, call.CString(2), call.ThreadId()),
        call.ThreadId());
}

std::uint32_t LogModule::BufWrite(const A32CallFrame& call) {
    return impl_->Emit(call.Scalar<std::int32_t>(0), call.Scalar<std::int32_t>(1),
        ReadCString(impl_->context_.address_space, call.CString(2), call.ThreadId()),
        ReadCString(impl_->context_.address_space, call.CString(3), call.ThreadId()),
        call.ThreadId());
}

std::uint32_t LogModule::VPrint(const A32CallFrame& call) {
    auto& memory = impl_->context_.address_space;
    const auto format = ReadCString(memory, call.CString(2), call.ThreadId());
    WordReader words(memory, call, memory::GuestAddress{call.Argument(3)});
    return impl_->Emit(0, call.Scalar<std::int32_t>(0),
        ReadCString(memory, call.CString(1), call.ThreadId()),
        FormatGuest(memory, call, format, words), call.ThreadId());
}

std::uint32_t LogModule::Print(const A32CallFrame& call) {
    auto& memory = impl_->context_.address_space;
    const auto format = ReadCString(memory, call.CString(2), call.ThreadId());
    WordReader words(memory, call, 3U);
    return impl_->Emit(0, call.Scalar<std::int32_t>(0),
        ReadCString(memory, call.CString(1), call.ThreadId()),
        FormatGuest(memory, call, format, words), call.ThreadId());
}

std::uint32_t LogModule::BufPrint(const A32CallFrame& call) {
    auto& memory = impl_->context_.address_space;
    const auto format = ReadCString(memory, call.CString(3), call.ThreadId());
    WordReader words(memory, call, 4U);
    return impl_->Emit(call.Scalar<std::int32_t>(0), call.Scalar<std::int32_t>(1),
        ReadCString(memory, call.CString(2), call.ThreadId()),
        FormatGuest(memory, call, format, words), call.ThreadId());
}

std::uint32_t LogModule::Assert(const A32CallFrame& call) {
    auto& memory = impl_->context_.address_space;
    const auto condition = ReadCString(memory, call.CString(0), call.ThreadId());
    const auto tag = ReadCString(memory, call.CString(1), call.ThreadId());
    std::string message;
    if (!call.CString(2).IsNull()) {
        const auto format = ReadCString(memory, call.CString(2), call.ThreadId());
        WordReader words(memory, call, 3U);
        message = FormatGuest(memory, call, format, words);
    } else {
        message = condition.empty() ? "Unspecified assertion failed"
                                    : "Assertion failed: " + condition;
    }
    static_cast<void>(impl_->Emit(0, kLogFatal, tag, message, call.ThreadId()));
    throw std::runtime_error("guest liblog assertion: " + message);
}

std::uint32_t LogModule::BinaryWrite(const A32CallFrame& call) {
    const auto payload = ReadBytes(impl_->context_.address_space,
        call.Pointer<const void>(1), call.Argument(2), call.ThreadId());
    return impl_->Emit(2, kLogInfo, "event/" + std::to_string(call.Scalar<std::int32_t>(0)),
        HexPayload(payload), call.ThreadId());
}

std::uint32_t LogModule::BinaryTypedWrite(const A32CallFrame& call) {
    const auto payload = ReadBytes(impl_->context_.address_space,
        call.Pointer<const void>(2), call.Argument(3), call.ThreadId());
    return impl_->Emit(2, kLogInfo, "event/" + std::to_string(call.Scalar<std::int32_t>(0)),
        "type=" + std::to_string(call.Argument(1) & 0xffU) + " " + HexPayload(payload),
        call.ThreadId());
}

std::uint32_t LogModule::FormatNew(const A32CallFrame&) { return impl_->NewFormat(); }
std::uint32_t LogModule::FormatFree(const A32CallFrame& call) {
    static_cast<void>(impl_->RemoveFormat(call.Argument(0)));
    return 0U;
}
std::uint32_t LogModule::SetPrintFormat(const A32CallFrame& call) {
    std::scoped_lock lock(impl_->mutex_);
    if (auto* format = impl_->FindFormat(call.Argument(0)); format != nullptr) {
        const auto value = call.Scalar<std::int32_t>(1);
        if (value >= 0 && value <= 8) format->print_format = value;
    }
    return 0U;
}

std::uint32_t LogModule::FormatFromString(const A32CallFrame& call) {
    const auto value = ReadCString(impl_->context_.address_space, call.CString(0), call.ThreadId());
    static const std::unordered_map<std::string, std::int32_t> formats{
        {"brief", 1}, {"process", 2}, {"tag", 3}, {"thread", 4}, {"raw", 5},
        {"time", 6}, {"threadtime", 7}, {"long", 8}};
    const auto found = formats.find(value);
    return static_cast<std::uint32_t>(found == formats.end() ? 0 : found->second);
}

std::uint32_t LogModule::AddFilterRule(const A32CallFrame& call) {
    const auto rule = ReadCString(impl_->context_.address_space, call.CString(1), call.ThreadId());
    std::scoped_lock lock(impl_->mutex_);
    auto* format = impl_->FindFormat(call.Argument(0));
    return Signed(format == nullptr ? -1 : impl_->AddRule(*format, rule));
}

std::uint32_t LogModule::AddFilterString(const A32CallFrame& call) {
    const auto rules = ReadCString(impl_->context_.address_space, call.CString(1), call.ThreadId());
    std::scoped_lock lock(impl_->mutex_);
    auto* format = impl_->FindFormat(call.Argument(0));
    if (format == nullptr) return Signed(-1);
    std::istringstream stream(rules);
    std::string rule;
    while (stream >> rule) if (impl_->AddRule(*format, rule) != 0) return Signed(-1);
    return 0U;
}

std::uint32_t LogModule::ShouldPrintLine(const A32CallFrame& call) {
    const auto tag = ReadCString(impl_->context_.address_space, call.CString(1), call.ThreadId());
    std::scoped_lock lock(impl_->mutex_);
    const auto* format = impl_->FindFormat(call.Argument(0));
    return format != nullptr && impl_->ShouldPrint(*format, tag, call.Scalar<std::int32_t>(2));
}

std::uint32_t LogModule::ProcessLogBuffer(const A32CallFrame& call) {
    auto& memory = impl_->context_.address_space;
    const auto source = call.Pointer<const void>(0).Address();
    const auto destination = call.Pointer<void>(1).Address();
    if (source.IsNull() || destination.IsNull()) return Signed(-1);
    const auto payload_length = memory.Read16(source, call.ThreadId());
    if (payload_length < 3U) return Signed(-1);
    const auto payload = source.Add(20U);
    std::vector<std::byte> encoded(payload_length);
    memory.Read(payload, encoded, call.ThreadId());
    const auto tag_end = std::find(encoded.begin() + 1, encoded.end(), std::byte{});
    if (tag_end == encoded.end()) return Signed(-1);
    const auto message_begin = static_cast<std::size_t>(tag_end - encoded.begin()) + 1U;
    auto message_end = std::find(encoded.begin() +
        static_cast<std::ptrdiff_t>(message_begin), encoded.end(), std::byte{});
    if (message_end == encoded.end()) {
        memory.Write8(payload.Add(payload_length - 1U), 0U, call.ThreadId());
        message_end = encoded.end() - 1;
    }
    const auto tag = payload.Add(1U);
    const auto message = payload.Add(message_begin);
    const auto message_length = static_cast<std::size_t>(
        message_end - (encoded.begin() + static_cast<std::ptrdiff_t>(message_begin)));
    WriteEntry(memory, destination,
        {std::bit_cast<std::int32_t>(memory.Read32(source.Add(12), call.ThreadId())),
         std::bit_cast<std::int32_t>(memory.Read32(source.Add(16), call.ThreadId())),
         std::to_integer<std::uint8_t>(encoded[0]),
         std::bit_cast<std::int32_t>(memory.Read32(source.Add(4), call.ThreadId())),
         std::bit_cast<std::int32_t>(memory.Read32(source.Add(8), call.ThreadId())),
         tag.Value(), static_cast<std::uint32_t>(message_length), message.Value()},
        call.ThreadId());
    return 0U;
}

std::uint32_t LogModule::ProcessBinaryLogBuffer(const A32CallFrame& call) {
    auto& memory = impl_->context_.address_space;
    const auto source = call.Pointer<const void>(0).Address();
    const auto destination = call.Pointer<void>(1).Address();
    const auto message_buffer = call.Pointer<void>(3).Address();
    const auto capacity = call.Scalar<std::int32_t>(4);
    if (source.IsNull() || destination.IsNull() || message_buffer.IsNull() || capacity <= 1) {
        return Signed(-1);
    }
    const auto payload_length = memory.Read16(source, call.ThreadId());
    if (payload_length < 5U) return Signed(-1);
    std::vector<std::byte> payload(payload_length);
    memory.Read(source.Add(20U), payload, call.ThreadId());
    const auto tag_number = std::bit_cast<std::int32_t>(ReadLittle32(payload, 0U));
    std::size_t cursor = 4U;
    std::string message;
    try {
        message = ParseBinaryEvent(payload, cursor);
    } catch (const std::invalid_argument&) {
        return Signed(-1);
    }
    if (cursor + 1U == payload.size() && payload[cursor] == std::byte{'\n'}) ++cursor;
    if (cursor != payload.size()) return Signed(-1);

    const auto mapped_tag = call.Argument(2) == 0U
        ? 0U : impl_->LookupEventTag(call.Argument(2), tag_number);
    const auto generated_tag = "[" + std::to_string(tag_number) + "]";
    const auto required = message.size() + 1U +
        (mapped_tag == 0U ? generated_tag.size() + 1U : 0U);
    if (required > static_cast<std::size_t>(capacity)) return Signed(-1);
    std::vector<std::byte> encoded(required);
    std::size_t output_cursor{};
    std::uint32_t tag_address = mapped_tag;
    if (mapped_tag == 0U) {
        tag_address = message_buffer.Value();
        for (const auto character : generated_tag) {
            encoded[output_cursor++] = static_cast<std::byte>(
                static_cast<unsigned char>(character));
        }
        ++output_cursor;
    }
    const auto message_address = message_buffer.Add(output_cursor).Value();
    for (const auto character : message) {
        encoded[output_cursor++] = static_cast<std::byte>(
            static_cast<unsigned char>(character));
    }
    memory.Write(message_buffer, encoded, call.ThreadId());
    WriteEntry(memory, destination,
        {std::bit_cast<std::int32_t>(memory.Read32(source.Add(12), call.ThreadId())),
         std::bit_cast<std::int32_t>(memory.Read32(source.Add(16), call.ThreadId())),
         kLogInfo,
         std::bit_cast<std::int32_t>(memory.Read32(source.Add(4), call.ThreadId())),
         std::bit_cast<std::int32_t>(memory.Read32(source.Add(8), call.ThreadId())),
         tag_address, static_cast<std::uint32_t>(message.size()), message_address},
        call.ThreadId());
    return 0U;
}

std::uint32_t LogModule::FormatLogLine(const A32CallFrame& call) {
    auto& memory = impl_->context_.address_space;
    const auto destination = call.Pointer<void>(1).Address();
    const auto capacity = call.Argument(2);
    const auto entry = ReadEntry(memory, call.Pointer<const void>(3).Address(), call.ThreadId());
    const auto tag = ReadCString(memory, GuestCString{memory::GuestAddress{entry.tag}}, call.ThreadId());
    const auto message = ReadCString(memory, GuestCString{memory::GuestAddress{entry.message}},
                                     call.ThreadId(), entry.message_length + 1U);
    std::string line;
    {
        std::scoped_lock lock(impl_->mutex_);
        const auto* format = impl_->FindFormat(call.Argument(0));
        if (format == nullptr) return 0U;
        line = impl_->FormatLine(*format, entry, tag, message);
    }
    if (!call.Pointer<void>(4).IsNull()) {
        memory.Write32(call.Pointer<void>(4).Address(), static_cast<std::uint32_t>(line.size()),
                       call.ThreadId());
    }
    if (destination.IsNull() || capacity <= line.size()) return 0U;
    std::vector<std::byte> bytes(line.size() + 1U);
    for (std::size_t index = 0; index < line.size(); ++index) {
        bytes[index] = static_cast<std::byte>(static_cast<unsigned char>(line[index]));
    }
    memory.Write(destination, bytes, call.ThreadId());
    return destination.Value();
}

std::uint32_t LogModule::PrintLogLine(const A32CallFrame& call) {
    auto& memory = impl_->context_.address_space;
    const auto entry = ReadEntry(memory, call.Pointer<const void>(2).Address(), call.ThreadId());
    const auto tag = ReadCString(memory, GuestCString{memory::GuestAddress{entry.tag}}, call.ThreadId());
    const auto message = ReadCString(memory, GuestCString{memory::GuestAddress{entry.message}},
                                     call.ThreadId(), entry.message_length + 1U);
    {
        std::scoped_lock lock(impl_->mutex_);
        const auto* format = impl_->FindFormat(call.Argument(0));
        if (format == nullptr || !impl_->ShouldPrint(*format, tag, entry.priority)) return 0U;
    }
    return impl_->Emit(0, entry.priority, tag, message, call.ThreadId());
}

std::uint32_t LogModule::OpenEventTagMap(const A32CallFrame& call) {
    const auto path = ReadCString(
        impl_->context_.address_space, call.CString(0), call.ThreadId());
    return impl_->OpenEventMap(path);
}
std::uint32_t LogModule::CloseEventTagMap(const A32CallFrame& call) {
    impl_->CloseEventMap(call.Argument(0));
    return 0U;
}
std::uint32_t LogModule::LookupEventTag(const A32CallFrame& call) {
    return impl_->LookupEventTag(call.Argument(0), call.Scalar<std::int32_t>(1));
}

}  // namespace ogplay::runtime
