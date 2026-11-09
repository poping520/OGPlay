#pragma once
#include "ogplay/app/export.h"
#include "ogplay/app/json.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace ogplay::app {

// GUI-23: how a library game's last run went (`last-run.json` in the entry directory). The game
// process writes an unfinished report when the run starts and replaces it when the run ends, so a
// report still unfinished while nothing runs the entry means the process died mid-run.
struct LastRunReport final {
    bool finished{};
    std::string started_at;
    std::optional<std::string> ended_at;
    // ADR-0146 run end (startup_failed, fault, crashed, no_frame, closed, exited); empty while unfinished.
    std::string end;
    std::optional<std::int64_t> exit_code;
    std::uint64_t presented{};
    // The first fault of the run, or why it could not start.
    std::optional<std::string> error;
    // A failure while shutting down a run that itself went well (an onDestroy error after closing).
    std::optional<std::string> exit_error;

    bool operator==(const LastRunReport&) const = default;
};

// Atomic replace; error texts are cut to a bounded size and made valid UTF-8.
OGPLAYCORE_API void WriteLastRunReport(const std::filesystem::path& file, LastRunReport report);
// None when the file is absent or this build cannot read it.
[[nodiscard]] OGPLAYCORE_API std::optional<LastRunReport> LoadLastRunReport(const std::filesystem::path& file);

// running/interrupted for an unfinished report (by whether the entry runs now), else the run end.
[[nodiscard]] OGPLAYCORE_API std::string_view LastRunOutcome(const LastRunReport& report, bool running) noexcept;
// Worth investigating: a failed end, an exit error, or a process that died mid-run.
[[nodiscard]] OGPLAYCORE_API bool LastRunFailed(const LastRunReport& report, bool running) noexcept;
// The `last_run` object both the GUI and the CLI list.
[[nodiscard]] OGPLAYCORE_API JsonWriter::Value LastRunJson(JsonWriter& writer, const LastRunReport& report,
                                                           bool running);

}  // namespace ogplay::app
