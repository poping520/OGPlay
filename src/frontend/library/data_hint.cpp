#include "ogplay/app/data_hint.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <span>
#include <unordered_set>

#include "ogplay/app/json.h"
#include "ogplay/app/library.h"

namespace ogplay::app {
namespace {

constexpr std::size_t kIndexLimit = 200000, kExamples = 3, kCandidates = 3, kNeedsDataThreshold = 3;

[[nodiscard]] std::string Fold(std::string_view text) {
    std::string result(text);
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) {
        return static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
    });
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
    const auto corrupt = [&] { return LibraryError(LibraryErrorCode::corrupt_config, "missing data report is malformed", file); };
    if (!document) throw corrupt();
    const auto root = document->Root();
    const auto schema = root.Member("schema"), guest = root.Member("external_guest_dir"),
               missing = root.Member("missing"), truncated = root.Member("truncated"), external = root.Member("external_dir");
    if (!schema || schema->UnsignedInteger() != 1U || !guest || !guest->String() || !missing || !missing->IsArray() ||
        !truncated || !truncated->Bool() || !external) throw corrupt();
    MissingDataReport report;
    report.external_guest_dir = std::string(*guest->String());
    report.truncated = *truncated->Bool();
    if (const auto text_value = external->String()) report.external_dir = std::filesystem::path(std::u8string(
        reinterpret_cast<const char8_t*>(text_value->data()), text_value->size()));
    else if (!external->IsNull()) throw corrupt();
    for (std::size_t index = 0; index < missing->Size(); ++index) {
        const auto value = missing->Element(index);
        const auto path = value ? value->String() : std::nullopt;
        if (!path || !path->starts_with("/sdcard")) throw corrupt();
        report.missing.emplace_back(*path);
    }
    return report;
}

DataMountHint InferDataMount(const MissingDataReport& report, const std::optional<std::filesystem::path>& data_dir,
                             const std::string_view current_guest_dir) {
    DataMountHint hint;
    hint.missing = report.missing.size();
    if (report.missing.empty()) return hint;
    if (!data_dir) {
        if (report.missing.size() < kNeedsDataThreshold) return hint;
        // The deepest directory shared by every miss is where the game looks for its data.
        auto common = Components(report.missing.front());
        if (!common.empty()) common.pop_back();
        for (const auto& path : report.missing) {
            auto parts = Components(path);
            if (!parts.empty()) parts.pop_back();
            std::size_t shared = 0;
            while (shared < common.size() && shared < parts.size() && Fold(common[shared]) == Fold(parts[shared])) ++shared;
            common.resize(shared);
        }
        hint.kind = DataMountHintKind::needs_data;
        hint.search_root = Join(common, true);
        for (std::size_t index = 0; index < report.missing.size() && index < kExamples; ++index)
            hint.examples.push_back(report.missing[index]);
        return hint;
    }
    const auto index = IndexDataPack(*data_dir);
    struct Tally { std::string spelling; std::set<std::size_t> paths; std::size_t depth{}; };
    std::map<std::string, std::size_t> depths;
    std::map<std::string, Tally> tallies;
    const auto current = Fold(current_guest_dir);
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
        }
    }
    for (auto& [folded, tally] : tallies) {
        // One shallow coincidence (a lone Readme.txt) is not evidence.
        if (folded == current || (tally.depth < 2 && tally.paths.size() < 2)) continue;
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

LibraryDataHint SuggestDataMount(const LibraryStore& store, const std::string_view id,
                                 const std::optional<std::filesystem::path>& picked) {
    const auto entries = store.LoadEntries();
    const auto found = std::find_if(entries.begin(), entries.end(), [&](const auto& entry) { return entry.key == id; });
    if (found == entries.end() || found->Damaged() || !found->metadata)
        throw LibraryError(LibraryErrorCode::not_found, "library entry is unavailable");
    const auto settings = LoadGameSettings(found->directory);
    const auto effective = EffectiveGameEntry(*found, settings);
    const auto& metadata = *effective.metadata;
    LibraryDataHint result{.current_guest_dir = metadata.external_guest_dir,
                           .external_dir = picked ? picked : metadata.external_dir,
                           .owned = !picked && metadata.external_owned};
    if (!std::get<bool>(EffectiveGameSetting(settings, LoadLibraryConfig(store.Root()), *found, "data_mount_hint")))
        return result;
    MissingDataReport report;
    try {
        report = LoadMissingDataReport(found->directory / "last-run-missing.json");
    } catch (const LibraryError&) {
        return result;  // no report, or one this build cannot read: nothing to suggest
    }
    if (!picked && (report.external_guest_dir != metadata.external_guest_dir ||
                    report.external_dir.has_value() != metadata.external_dir.has_value())) return result;
    result.truncated = report.truncated;
    result.hint = InferDataMount(report, result.external_dir,
                                 picked ? std::string_view{} : std::string_view(metadata.external_guest_dir));
    return result;
}

}  // namespace ogplay::app
