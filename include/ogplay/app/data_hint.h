#pragma once
#include "ogplay/app/export.h"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ogplay::app {

// ADR-0146: what one game run looked for under /sdcard and did not find.
struct MissingDataReport final {
    std::optional<std::filesystem::path> external_dir;
    std::string external_guest_dir{"/sdcard"};
    std::vector<std::string> missing;
    bool truncated{};
};

// Throws LibraryError(corrupt_config) for a malformed report, not_found when absent.
[[nodiscard]] OGPLAYCORE_API MissingDataReport LoadMissingDataReport(const std::filesystem::path& file);

struct DataMountCandidate final {
    std::string guest_dir;
    std::size_t explained{};             // distinct missing paths found in the data pack
    std::vector<std::string> examples;   // a few of those missing guest paths
};

enum class DataMountHintKind { none, remount, needs_data };

struct DataMountHint final {
    DataMountHintKind kind{DataMountHintKind::none};
    std::vector<DataMountCandidate> candidates;  // remount: best first
    std::string search_root;                     // needs_data: deepest common missing directory
    std::vector<std::string> examples;           // needs_data: sample missing paths
    std::size_t missing{};
};

// Compares missing guest paths with the files of a data directory; never reads the APK.
// Without a data directory it only reports that the game may need one.
[[nodiscard]] OGPLAYCORE_API DataMountHint InferDataMount(
    const MissingDataReport& report, const std::optional<std::filesystem::path>& data_dir,
    std::string_view current_guest_dir);

class LibraryStore;

// A hint for one library entry after its last run, with the mount it applies to.
struct LibraryDataHint final {
    DataMountHint hint;
    std::string current_guest_dir;
    std::optional<std::filesystem::path> external_dir;  // directory the hint was computed against
    bool owned{};
    bool truncated{};
};

// Reads the entry's last-run report. kind none when hints are disabled, the report is
// absent or unreadable, or it describes another mount than the current one. A freshly
// picked directory is judged on its own, independent of the current mount.
[[nodiscard]] OGPLAYCORE_API LibraryDataHint SuggestDataMount(
    const LibraryStore& store, std::string_view installation_id,
    const std::optional<std::filesystem::path>& picked_dir = std::nullopt);

}  // namespace ogplay::app
