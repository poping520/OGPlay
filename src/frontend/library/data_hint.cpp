#include "ogplay/app/data_hint.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <span>
#include <string_view>
#include <unordered_set>
#include <utility>

#include "ogplay/app/game_run.h"
#include "ogplay/app/json.h"
#include "ogplay/app/library.h"

namespace ogplay::app {
namespace {

constexpr std::size_t kIndexLimit = 200000, kExamples = 3, kCandidates = 3, kNeedsDataThreshold = 3;

[[nodiscard]] std::string Fold(std::string_view text) {
    std::string result(text);
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c); });
    return result;
}

[[nodiscard]] std::string Utf8(const std::filesystem::path& path) {
    const auto text = path.generic_u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}

[[nodiscard]] std::vector<std::string_view> Components(std::string_view path) {
    std::vector<std::string_view> parts;
    while (!path.empty()) {
        const auto slash = path.find('/');
        const auto part = path.substr(0, slash);
        if (!part.empty()) parts.push_back(part);
        if (slash == std::string_view::npos) break;
        path.remove_prefix(slash + 1);
    }
    return parts;
}

[[nodiscard]] std::string Join(std::span<const std::string_view> parts, bool absolute) {
    std::string result;
    for (const auto part : parts) {
        if (absolute || !result.empty()) result.push_back('/');
        result.append(part);
    }
    return result.empty() && absolute ? "/" : result;
}

// Android expansion file naming, main|patch.<version>.<package>.obb: a game that looks for one
// cannot run without it, so a single miss is evidence where other names need several.
[[nodiscard]] bool IsExpansionFile(std::string_view path) {
    const auto name = Fold(path.substr(path.rfind('/') + 1));
    std::string_view rest(name);
    if (rest.starts_with("main."))
        rest.remove_prefix(5);
    else if (rest.starts_with("patch."))
        rest.remove_prefix(6);
    else
        return false;
    const auto digits = rest.find_first_not_of("0123456789");
    if (digits == 0 || digits == std::string_view::npos || rest[digits] != '.') return false;
    rest.remove_prefix(digits + 1);
    return rest.size() > 4 && rest.ends_with(".obb");
}

// Relative paths (files and directories) of the data pack, ASCII case-folded like the VFS.
[[nodiscard]] std::unordered_set<std::string> IndexDataPack(const std::filesystem::path& root) {
    std::unordered_set<std::string> index;
    std::error_code error;
    for (std::filesystem::recursive_directory_iterator it(root, error), end;
         !error && it != end && index.size() < kIndexLimit; it.increment(error))
        index.insert(Fold(Utf8(it->path().lexically_relative(root))));
    return index;
}

}  // namespace

MissingDataReport LoadMissingDataReport(const std::filesystem::path& file) {
    std::ifstream input(file, std::ios::binary);
    if (!input) throw LibraryError(LibraryErrorCode::not_found, "missing data report is absent", file);
    const std::string text{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    JsonParseError error;
    const auto document = JsonDocument::ParseStrict(text, error);
    const auto corrupt = [&] {
        return LibraryError(LibraryErrorCode::corrupt_config, "missing data report is malformed", file);
    };
    if (!document) throw corrupt();
    const auto root = document->Root();
    const auto schema = root.Member("schema"), guest = root.Member("external_guest_dir"),
               missing = root.Member("missing"), truncated = root.Member("truncated"),
               external = root.Member("external_dir"), end = root.Member("end"), found = root.Member("found");
    if (!schema || schema->UnsignedInteger() != 2U || !guest || !guest->String() || !missing || !missing->IsArray() ||
        !truncated || !truncated->Bool() || !external || !end || !end->String() || !found || !found->IsArray())
        throw corrupt();
    MissingDataReport report;
    report.external_guest_dir = std::string(*guest->String());
    report.truncated = *truncated->Bool();
    // The game frontend's run ends: only closed and exited mean the game ran.
    static constexpr std::string_view kFailedEnds[] = {"startup_failed", "fault", "crashed", "no_frame"};
    const auto run_end = *end->String();
    report.failed = std::find(std::begin(kFailedEnds), std::end(kFailedEnds), run_end) != std::end(kFailedEnds);
    if (!report.failed && run_end != "closed" && run_end != "exited") throw corrupt();
    if (const auto text_value = external->String())
        report.external_dir = std::filesystem::path(
            std::u8string(reinterpret_cast<const char8_t*>(text_value->data()), text_value->size()));
    else if (!external->IsNull())
        throw corrupt();
    for (const auto& [list, into] : {std::pair{*missing, &report.missing}, std::pair{*found, &report.found}}) {
        for (std::size_t index = 0; index < list.Size(); ++index) {
            const auto value = list.Element(index);
            const auto path = value ? value->String() : std::nullopt;
            if (!path || !path->starts_with("/sdcard")) throw corrupt();
            into->emplace_back(*path);
        }
    }
    return report;
}

DataMountHint InferDataMount(const MissingDataReport& report, const std::optional<std::filesystem::path>& data_dir,
                             const std::string_view current_guest_dir) {
    DataMountHint hint;
    hint.missing = report.missing.size();
    // A game that ran probes paths it can do without; only a failed run makes misses evidence.
    if (!report.failed || report.missing.empty()) return hint;
    if (!data_dir) {
        std::vector<std::string> evidence;
        std::copy_if(report.missing.begin(), report.missing.end(), std::back_inserter(evidence),
                     [](const auto& path) { return IsExpansionFile(path); });
        if (evidence.empty() && report.missing.size() >= kNeedsDataThreshold) evidence = report.missing;
        if (evidence.empty()) return hint;
        // The deepest directory shared by every miss is where the game looks for its data.
        auto common = Components(evidence.front());
        if (!common.empty()) common.pop_back();
        for (const auto& path : evidence) {
            auto parts = Components(path);
            if (!parts.empty()) parts.pop_back();
            std::size_t shared = 0;
            while (shared < common.size() && shared < parts.size() && Fold(common[shared]) == Fold(parts[shared]))
                ++shared;
            common.resize(shared);
        }
        hint.kind = DataMountHintKind::needs_data;
        hint.search_root = Join(common, true);
        for (std::size_t index = 0; index < evidence.size() && index < kExamples; ++index)
            hint.examples.push_back(evidence[index]);
        return hint;
    }
    const auto index = IndexDataPack(*data_dir);

    struct Tally {
        std::string spelling;
        std::set<std::size_t> paths;
        std::size_t depth{};
        bool expansion{};
    };

    std::map<std::string, std::size_t> depths;
    std::map<std::string, Tally> tallies;
    const auto current = Fold(current_guest_dir);
    // Pack files the game already reads through the current mount; moving the pack would lose them.
    std::size_t served{};
    if (!current.empty()) {
        const auto prefix = current.ends_with('/') ? current : current + "/";
        for (const auto& path : report.found) {
            const auto folded = Fold(path);
            served += folded.starts_with(prefix) && index.contains(folded.substr(prefix.size())) ? 1U : 0U;
        }
    }
    for (std::size_t miss = 0; miss < report.missing.size(); ++miss) {
        const auto parts = Components(report.missing[miss]);
        // Split /X.../r...: X is a candidate mount when r exists in the data pack.
        for (std::size_t split = 1; split < parts.size(); ++split) {
            const std::span<const std::string_view> all(parts);
            if (!index.contains(Fold(Join(all.subspan(split), false)))) continue;
            auto mount = Join(all.first(split), true);
            auto& tally = tallies[Fold(mount)];
            if (tally.spelling.empty()) tally.spelling = std::move(mount);
            tally.paths.insert(miss);
            tally.depth = std::max(tally.depth, parts.size() - split);
            tally.expansion = tally.expansion || IsExpansionFile(report.missing[miss]);
        }
    }
    for (auto& [folded, tally] : tallies) {
        // One shallow coincidence (a lone Readme.txt) is not evidence unless it is the expansion
        // file itself, nor is a move that loses at least as many files as it gains (a game
        // probing paths inside its own pack).
        if (folded == current || (tally.depth < 2 && tally.paths.size() < 2 && !tally.expansion) ||
            tally.paths.size() <= served)
            continue;
        DataMountCandidate candidate{tally.spelling, tally.paths.size(), {}};
        depths[candidate.guest_dir] = tally.depth;
        for (const auto miss : tally.paths) {
            if (candidate.examples.size() == kExamples) break;
            candidate.examples.push_back(report.missing[miss]);
        }
        hint.candidates.push_back(std::move(candidate));
    }
    // Deeper matches are more specific evidence when two mounts explain as much.
    std::stable_sort(hint.candidates.begin(), hint.candidates.end(), [&](const auto& left, const auto& right) {
        if (left.explained != right.explained) return left.explained > right.explained;
        return depths[left.guest_dir] > depths[right.guest_dir];
    });
    if (hint.candidates.size() > kCandidates) hint.candidates.resize(kCandidates);
    if (!hint.candidates.empty()) hint.kind = DataMountHintKind::remount;
    return hint;
}

std::optional<std::string> GuessDataMount(const std::filesystem::path& dir, const std::string_view package) {
    const auto text = Utf8(std::filesystem::absolute(dir).lexically_normal());
    const auto parts = Components(text);
    std::optional<std::string> guest;
    const auto below = [&](std::size_t index, std::string_view prefix) {
        std::string result(prefix);
        for (auto part : std::span(parts).subspan(index))
            result.append("/").append(part);
        return result;
    };
    // The innermost layout marker wins, so a phone backup nested in another one maps correctly.
    for (std::size_t index = parts.size(); index-- > 0 && !guest;) {
        const auto part = Fold(parts[index]);
        const auto next = index + 1 < parts.size() ? Fold(parts[index + 1]) : std::string{};
        if (part == "android" && (next.empty() || next == "data" || next == "obb" || next == "media"))
            guest = next.empty() ? below(index + 1, "/sdcard/Android") : below(index + 2, "/sdcard/Android/" + next);
        else if (part == "sdcard")
            guest = below(index + 1, "/sdcard");
        else if (part == "0" && index > 0 && Fold(parts[index - 1]) == "emulated")
            guest = below(index + 1, "/sdcard");
    }
    if (!guest && !parts.empty()) {
        bool obb{}, android{};
        std::error_code error;
        for (std::filesystem::directory_iterator it(dir, error), end; !error && it != end; it.increment(error)) {
            obb = obb || (it->is_regular_file(error) && Fold(Utf8(it->path().extension())) == ".obb");
            android = android || (it->is_directory(error) && Fold(Utf8(it->path().filename())) == "android");
        }
        // A copied sdcard root keeps /sdcard; otherwise the picked directory itself appears below it.
        if (!package.empty() && Fold(parts.back()) == Fold(package))
            guest = std::string(obb ? "/sdcard/Android/obb/" : "/sdcard/Android/data/").append(parts.back());
        else if (!android)
            guest = std::string("/sdcard/").append(parts.back());
    }
    if (!guest || *guest == "/sdcard") return std::nullopt;
    try {
        ValidateExternalGuestDirectory(*guest);
    } catch (const std::exception&) {
        return std::nullopt;  // host names the guest cannot express keep the default
    }
    return guest;
}

LibraryDataHint SuggestDataMount(const LibraryStore& store, const std::string_view id,
                                 const std::optional<std::filesystem::path>& picked) {
    const auto entries = store.LoadEntries();
    const auto found = std::find_if(entries.begin(), entries.end(), [&](const auto& entry) { return entry.key == id; });
    if (found == entries.end() || found->Damaged() || !found->metadata)
        throw LibraryError(LibraryErrorCode::not_found, "library entry is unavailable");
    const auto settings = LoadGameSettings(found->directory);
    const auto effective = EffectiveGameEntry(*found, settings);
    LibraryDataHint result{.current_guest_dir = effective.external_guest_dir,
                           .external_dir = picked ? picked : effective.external_dir,
                           .owned = !picked && effective.external_owned};
    if (!std::get<bool>(EffectiveGameSetting(settings, LoadLibraryConfig(store.Root()), *found, "data_mount_hint")))
        return result;
    MissingDataReport report;
    try {
        report = LoadMissingDataReport(found->directory / "last-run-missing.json");
    } catch (const LibraryError&) {
        return result;  // no report, or one this build cannot read: nothing to suggest
    }
    if (!picked && (report.external_guest_dir != effective.external_guest_dir ||
                    report.external_dir.has_value() != effective.external_dir.has_value()))
        return result;
    result.truncated = report.truncated;
    result.hint = InferDataMount(report, result.external_dir,
                                 picked ? std::string_view{} : std::string_view(effective.external_guest_dir));
    return result;
}

}  // namespace ogplay::app
