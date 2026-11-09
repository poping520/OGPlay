// GUI-23: the last-run report shared by the game process (writer) and the library (reader).

#include "ogplay/app/run_report.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <stdexcept>

#include "ogplay/core/text.h"

namespace ogplay::app {
namespace {

constexpr std::uint64_t kSchema = 1;
constexpr std::size_t kTextLimit = 4096;
constexpr std::string_view kEnds[] = {"startup_failed", "fault", "crashed", "no_frame", "closed", "exited"};
constexpr std::string_view kFailedEnds[] = {"startup_failed", "fault", "crashed", "no_frame"};

// Error texts come from guest exceptions and may carry any bytes: keep valid UTF-8 as is,
// replace other non-ASCII bytes, and cut at a character boundary.
std::string BoundedText(std::string_view text) {
    std::string result;
    if (core::IsValidUtf8(text)) {
        result.assign(text);
    } else {
        result.reserve(text.size());
        for (const char c : text)
            result.push_back(static_cast<unsigned char>(c) < 0x80U ? c : '?');
    }
    if (result.size() > kTextLimit) {
        auto cut = kTextLimit;
        while (cut > 0 && (static_cast<unsigned char>(result[cut]) & 0xC0U) == 0x80U)
            --cut;
        result.resize(cut);
        result += "…";
    }
    return result;
}

void AddOptional(JsonWriter& writer, const JsonWriter::Value& object, std::string_view name,
                 const std::optional<std::string>& value) {
    if (value)
        writer.AddString(object, name, *value);
    else
        writer.AddNull(object, name);
}

}  // namespace

void WriteLastRunReport(const std::filesystem::path& file, LastRunReport report) {
    for (auto* text : {&report.error, &report.exit_error})
        if (*text) *text = BoundedText(**text);
    JsonWriter writer;
    const auto root = writer.Object();
    writer.AddUnsignedInteger(root, "schema", kSchema);
    writer.AddBool(root, "finished", report.finished);
    writer.AddString(root, "started_at", report.started_at);
    AddOptional(writer, root, "ended_at", report.ended_at);
    writer.AddString(root, "end", report.end);
    if (report.exit_code)
        writer.AddInteger(root, "exit_code", *report.exit_code);
    else
        writer.AddNull(root, "exit_code");
    writer.AddUnsignedInteger(root, "presented", report.presented);
    AddOptional(writer, root, "error", report.error);
    AddOptional(writer, root, "exit_error", report.exit_error);
    const auto text = writer.Serialize(root);
    if (!file.parent_path().empty()) std::filesystem::create_directories(file.parent_path());
    auto temporary = file;
    temporary += ".tmp";
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        output.write(text.data(), static_cast<std::streamsize>(text.size()));
        if (!output) throw std::runtime_error("cannot write the last run report");
    }
    std::filesystem::rename(temporary, file);
}

std::optional<LastRunReport> LoadLastRunReport(const std::filesystem::path& file) {
    std::ifstream input(file, std::ios::binary);
    if (!input) return std::nullopt;
    const std::string text{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    JsonParseError error;
    const auto document = JsonDocument::ParseStrict(text, error);
    if (!document) return std::nullopt;
    const auto root = document->Root();
    const auto member = [&](std::string_view name) { return root.Member(name); };
    const auto schema = member("schema"), finished = member("finished"), started = member("started_at"),
               ended = member("ended_at"), end = member("end"), code = member("exit_code"),
               presented = member("presented"), fault = member("error"), exit_error = member("exit_error");
    if (!schema || schema->UnsignedInteger() != kSchema || !finished || !finished->Bool() || !started ||
        !started->String() || !end || !end->String() || !presented || !presented->UnsignedInteger())
        return std::nullopt;
    LastRunReport report;
    report.finished = *finished->Bool();
    report.started_at = std::string(*started->String());
    report.end = std::string(*end->String());
    report.presented = *presented->UnsignedInteger();
    if (report.finished != (std::find(std::begin(kEnds), std::end(kEnds), report.end) != std::end(kEnds)))
        return std::nullopt;
    if (!report.finished && !report.end.empty()) return std::nullopt;
    const auto optional_text = [](const std::optional<JsonValue>& value, std::optional<std::string>& into) {
        if (!value) return false;
        if (value->IsNull()) return true;
        if (!value->String()) return false;
        into = std::string(*value->String());
        return true;
    };
    if (!optional_text(ended, report.ended_at) || !optional_text(fault, report.error) ||
        !optional_text(exit_error, report.exit_error) || !code)
        return std::nullopt;
    if (!code->IsNull()) {
        if (!code->Integer()) return std::nullopt;
        report.exit_code = *code->Integer();
    }
    return report;
}

std::string_view LastRunOutcome(const LastRunReport& report, const bool running) noexcept {
    if (!report.finished) return running ? "running" : "interrupted";
    return report.end;
}

bool LastRunFailed(const LastRunReport& report, const bool running) noexcept {
    if (!report.finished) return !running;
    return report.exit_error.has_value() ||
           std::find(std::begin(kFailedEnds), std::end(kFailedEnds), report.end) != std::end(kFailedEnds);
}

JsonWriter::Value LastRunJson(JsonWriter& writer, const LastRunReport& report, const bool running) {
    const auto object = writer.Object();
    writer.AddString(object, "outcome", LastRunOutcome(report, running));
    writer.AddBool(object, "failed", LastRunFailed(report, running));
    writer.AddString(object, "started_at", report.started_at);
    AddOptional(writer, object, "ended_at", report.ended_at);
    if (report.exit_code)
        writer.AddInteger(object, "exit_code", *report.exit_code);
    else
        writer.AddNull(object, "exit_code");
    writer.AddUnsignedInteger(object, "presented", report.presented);
    AddOptional(writer, object, "error", report.error);
    AddOptional(writer, object, "exit_error", report.exit_error);
    return object;
}

}  // namespace ogplay::app
