#include "data_layout.h"

#include <algorithm>
#include <cctype>

namespace ogplay::app {
namespace {

[[nodiscard]] std::string Fold(std::string_view text) {
    std::string result(text);
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return result;
}

}  // namespace

std::optional<StorageMarker> MatchStorageMarker(std::span<const std::string_view> parts, std::size_t at) {
    if (at >= parts.size()) return std::nullopt;
    const auto part = Fold(parts[at]);
    const auto next = at + 1 < parts.size() ? Fold(parts[at + 1]) : std::string{};
    if (part == "android") {
        if (next.empty()) return StorageMarker{"/sdcard/Android", 1};
        if (next == "data" || next == "obb" || next == "media") return StorageMarker{"/sdcard/Android/" + next, 2};
        return std::nullopt;
    }
    if (part == "sdcard") return StorageMarker{"/sdcard", 1};
    if (part == "emulated" && next == "0") return StorageMarker{"/sdcard", 2};
    if (part == "gameloft" && next == "games") return StorageMarker{std::string(kGameloftGames), 2};
    return std::nullopt;
}

std::string PackageDirectoryMount(std::string_view name, bool holds_obb) {
    return std::string(holds_obb ? "/sdcard/Android/obb/" : "/sdcard/Android/data/").append(name);
}

bool GameloftPackage(std::string_view package) {
    return Fold(package).find("gameloft") != std::string::npos;
}

bool IsObbName(std::string_view name) {
    return name.size() > 4 && Fold(name.substr(name.size() - 4)) == ".obb";
}

}  // namespace ogplay::app
