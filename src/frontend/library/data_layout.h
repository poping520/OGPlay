#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace ogplay::app {

// Phone storage layout, shared by both mount guesses: GuessDataMount for a picked directory and PlanArchive for
// a game archive's members. Each caller decides which names count as a package; the layout is decided here.

struct StorageMarker final {
    std::string guest;      // the /sdcard path the marker stands for
    std::size_t parts{};    // components it spans; the ones after it map below guest
};

// A layout marker starting at parts[at] (ASCII case folded): Android/data|obb|media, a trailing Android,
// sdcard, emulated/0, or gameloft/games (spelled lower case in the guest). Nullopt anywhere else
// ("Android/games" is just a folder called Android).
[[nodiscard]] std::optional<StorageMarker> MatchStorageMarker(std::span<const std::string_view> parts, std::size_t at);

// An app's own directory named after its package: Android/obb/<name> when it holds expansion (.obb) files
// directly, otherwise Android/data/<name>.
[[nodiscard]] std::string PackageDirectoryMount(std::string_view name, bool holds_obb);

// Early Gameloft games (package containing "gameloft", any case) keep their data outside Android/, in
// kGameloftGames/<GameID>; a pack for one mounts at kGameloftGames itself.
inline constexpr std::string_view kGameloftGames = "/sdcard/gameloft/games";
[[nodiscard]] bool GameloftPackage(std::string_view package);

// A file name with the .obb extension, in any case.
[[nodiscard]] bool IsObbName(std::string_view name);

}  // namespace ogplay::app
