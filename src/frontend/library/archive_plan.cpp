#include "ogplay/app/archive_import.h"
#include "data_layout.h"
#include "external_data.h"

#include "ogplay/app/gbk_table.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <system_error>
#include <tuple>
#include <utility>
#include <vector>

namespace ogplay::app {
namespace {

constexpr std::size_t kChunkBytes = 1U << 20U;

[[nodiscard]] std::string Lower(std::string_view text) {
    std::string result(text);
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return result;
}

[[nodiscard]] std::vector<std::string> Split(std::string_view path) {
    std::vector<std::string> parts;
    for (std::size_t start = 0; start <= path.size();) {
        const auto end = std::min(path.find('/', start), path.size());
        parts.emplace_back(path.substr(start, end - start));
        start = end + 1;
    }
    return parts;
}

[[nodiscard]] std::string Join(const std::vector<std::string>& parts, std::size_t from, std::size_t to) {
    std::string result;
    for (auto index = from; index < to; ++index) {
        if (index != from) result += '/';
        result += parts[index];
    }
    return result;
}

// Java package shape: two or more dot-separated identifiers.
[[nodiscard]] bool PackageLike(std::string_view name) {
    if (name.find('.') == std::string_view::npos) return false;
    std::size_t start = 0;
    while (start <= name.size()) {
        const auto end = std::min(name.find('.', start), name.size());
        const auto part = name.substr(start, end - start);
        if (part.empty() || std::isdigit(static_cast<unsigned char>(part.front()))) return false;
        if (!std::all_of(part.begin(), part.end(), [](unsigned char c) { return std::isalnum(c) || c == '_'; }))
            return false;
        start = end + 1;
    }
    return true;
}

// An Android expansion file name, main|patch.<version>.<package>.obb; returns the package.
[[nodiscard]] std::optional<std::string> ExpansionPackage(std::string_view name) {
    const auto lower = Lower(name);
    std::size_t at;
    if (lower.starts_with("main."))
        at = 5;
    else if (lower.starts_with("patch."))
        at = 6;
    else
        return std::nullopt;
    if (!lower.ends_with(".obb")) return std::nullopt;
    const auto digits = at;
    while (at < lower.size() && std::isdigit(static_cast<unsigned char>(lower[at])))
        ++at;
    if (at == digits || at >= lower.size() || lower[at] != '.') return std::nullopt;
    const auto package = name.substr(at + 1, name.size() - at - 1 - 4);
    if (!PackageLike(package)) return std::nullopt;
    return std::string(package);
}

[[nodiscard]] bool IsApk(std::string_view path) {
    return Lower(path).ends_with(".apk");
}

// Readmes, store pages, icons and installer manifests packed beside the game; never data.
[[nodiscard]] bool PackJunk(std::string_view name) {
    const auto lower = Lower(name);
    const auto dot = lower.rfind('.');
    if (dot == std::string::npos) return false;
    static const std::set<std::string, std::less<>> extensions{
        "txt",  "url", "htm", "html", "nfo", "png", "jpg", "jpeg", "gif", "bmp", "webp", "dat",
        "data", "ini", "md5", "sfv",  "lnk", "db",  "doc", "docx", "pdf", "rtf", "xml",  "json"};
    return extensions.contains(lower.substr(dot + 1));
}

// "mod" as its own word in the file name marks the modded build packs often carry beside the original.
[[nodiscard]] bool ModdedApk(std::string_view path) {
    const auto lower = Lower(path.substr(path.rfind('/') == std::string_view::npos ? 0 : path.rfind('/') + 1));
    for (auto at = lower.find("mod"); at != std::string::npos; at = lower.find("mod", at + 1)) {
        const bool before = at == 0 || !std::isalpha(static_cast<unsigned char>(lower[at - 1]));
        const bool after = at + 3 >= lower.size() || !std::isalpha(static_cast<unsigned char>(lower[at + 3]));
        if (before && after) return true;
    }
    return false;
}

// The directories every member lies in from the top, one level at a time while nothing but readmes sits
// beside the next one: a pack's wrappers and the game directory they hold.
[[nodiscard]] std::vector<std::string> LoneDirectories(std::span<const std::vector<std::string>* const> members) {
    std::vector<std::string> chain;
    for (std::size_t depth = 0;; ++depth) {
        const std::string* name = nullptr;
        for (const auto* parts : members) {
            if (parts->size() < depth + 1) continue;  // a readme beside an outer level
            if (parts->size() == depth + 1) {
                if (PackJunk(parts->back())) continue;
                return chain;
            }
            if (!name)
                name = &(*parts)[depth];
            else if ((*parts)[depth] != *name)
                return chain;
        }
        if (!name) return chain;
        chain.push_back(*name);
    }
}

// Which of the lone directories is an early Gameloft game's <GameID> directory: the innermost one named like a
// part of the package (any case; the ID need not start with Gloft), else the only one. A picked directory
// (first in chain) stands for the game unless a deeper one matches.
[[nodiscard]] std::optional<std::size_t> GameloftGameDirectory(const std::vector<std::string>& chain,
                                                               std::string_view package, bool picked) {
    std::set<std::string> segments;
    std::string_view rest = package;
    while (!rest.empty()) {
        const auto dot = std::min(rest.find('.'), rest.size());
        segments.insert(Lower(rest.substr(0, dot)));
        rest.remove_prefix(std::min(dot + 1, rest.size()));
    }
    for (auto index = chain.size(); index-- > 0;)
        if (segments.contains(Lower(chain[index]))) return index;
    if (picked || chain.size() == 1) return chain.empty() ? std::nullopt : std::optional<std::size_t>(0);
    return std::nullopt;
}

[[nodiscard]] bool ValidUtf8(std::string_view text) {
    for (std::size_t index = 0; index < text.size();) {
        const auto lead = static_cast<unsigned char>(text[index]);
        std::size_t length;
        char32_t minimum;
        if (lead < 0x80) {
            ++index;
            continue;
        }
        if ((lead & 0xE0U) == 0xC0U)
            length = 2, minimum = 0x80;
        else if ((lead & 0xF0U) == 0xE0U)
            length = 3, minimum = 0x800;
        else if ((lead & 0xF8U) == 0xF0U)
            length = 4, minimum = 0x10000;
        else
            return false;
        if (index + length > text.size()) return false;
        char32_t value = lead & (0x7FU >> length);
        for (std::size_t offset = 1; offset < length; ++offset) {
            const auto next = static_cast<unsigned char>(text[index + offset]);
            if ((next & 0xC0U) != 0x80U) return false;
            value = (value << 6U) | (next & 0x3FU);
        }
        if (value < minimum || value > 0x10FFFF || (value >= 0xD800 && value <= 0xDFFF)) return false;
        index += length;
    }
    return true;
}

void AppendUtf8(std::string& output, char32_t value) {
    if (value < 0x80) {
        output += static_cast<char>(value);
    } else if (value < 0x800) {
        output += static_cast<char>(0xC0U | (value >> 6U));
        output += static_cast<char>(0x80U | (value & 0x3FU));
    } else {
        output += static_cast<char>(0xE0U | (value >> 12U));
        output += static_cast<char>(0x80U | ((value >> 6U) & 0x3FU));
        output += static_cast<char>(0x80U | (value & 0x3FU));
    }
}

}  // namespace

ImportSource::~ImportSource() = default;

std::string DecodeArchiveName(std::string_view bytes) {
    if (ValidUtf8(bytes)) return std::string(bytes);
    std::string result;
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        const auto lead = static_cast<unsigned char>(bytes[index]);
        if (lead < 0x80) {
            result += static_cast<char>(lead);
            continue;
        }
        if (lead == 0x80) {
            AppendUtf8(result, 0x20AC);
            continue;
        }
        if (lead < 0xFF && index + 1 < bytes.size()) {
            const auto trail = static_cast<unsigned char>(bytes[index + 1]);
            if (trail >= 0x40 && trail <= 0xFE && trail != 0x7F) {
                const auto value = generated::kGbkTable[(lead - 0x81U) * 191U + (trail - 0x40U)];
                if (value) {
                    AppendUtf8(result, value);
                    ++index;
                    continue;
                }
            }
        }
        AppendUtf8(result, 0xFFFD);
    }
    return result;
}

std::optional<std::string> NormalizeArchivePath(std::string_view utf8) {
    if (!ValidUtf8(utf8)) return std::nullopt;
    std::string text(utf8);
    std::replace(text.begin(), text.end(), '\\', '/');
    std::string result;
    for (const auto& part : Split(text)) {
        if (part.empty() || part == ".") continue;
        if (part == ".." || part.find(':') != std::string::npos) return std::nullopt;
        if (std::any_of(part.begin(), part.end(), [](unsigned char c) { return c < 0x20 || c == 0x7F; }))
            return std::nullopt;
        if (!result.empty()) result += '/';
        result += part;
    }
    return result;
}

std::filesystem::path ImportFilePath(std::string_view utf8) {
    return std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(utf8.data()), utf8.size()));
}

ImportFileWriter::ImportFileWriter(const std::filesystem::path& destination) : destination_(destination) {
    std::error_code error;
    std::filesystem::create_directories(destination.parent_path(), error);
    if (error)
        throw LibraryError(LibraryErrorCode::io_error, "cannot create import directory", destination.parent_path());
    if (std::filesystem::exists(destination, error))
        throw LibraryError(LibraryErrorCode::io_error, "duplicate file in the archive",
                           UiMessage{"host.error.archiveDuplicate"}, destination);
    output_.open(destination, std::ios::binary | std::ios::trunc);
    if (!output_) throw LibraryError(LibraryErrorCode::io_error, "cannot create imported file", destination);
}

void ImportFileWriter::Write(std::span<const std::byte> bytes) {
    output_.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!output_)
        throw LibraryError(LibraryErrorCode::io_error, "cannot write to the library",
                           UiMessage{"host.error.writeLibrary"}, destination_);
}

void ImportFileWriter::Finish() {
    output_.close();
    if (!output_)
        throw LibraryError(LibraryErrorCode::io_error, "cannot write to the library",
                           UiMessage{"host.error.writeLibrary"}, destination_);
}

void WriteImportFile(const std::filesystem::path& destination, std::uint64_t size,
                     const std::function<std::size_t(std::span<std::byte>)>& read, ImportProgress* progress) {
    ImportFileWriter output(destination);
    std::vector<std::byte> buffer(kChunkBytes);
    for (std::uint64_t written = 0; written < size;) {
        ThrowIfImportCancelled(progress);
        const auto want = static_cast<std::size_t>(std::min<std::uint64_t>(buffer.size(), size - written));
        const auto count = read(std::span(buffer).first(want));
        if (count == 0 || count > want)
            throw LibraryError(LibraryErrorCode::io_error, "the archive is incomplete",
                               UiMessage{"host.error.archiveIncomplete"}, destination);
        output.Write(std::span(buffer).first(count));
        written += count;
        if (progress) progress->copied_bytes.fetch_add(count, std::memory_order_relaxed);
    }
    output.Finish();
}

ArchivePlan PlanArchive(const std::vector<ImportFile>& files, std::string_view package) {
    std::vector<std::vector<std::string>> split;
    split.reserve(files.size());
    for (const auto& file : files)
        split.push_back(Split(file.path));
    // A single wrapper directory (the pack's own name) is looked through.
    std::size_t wrappers = 0;
    std::vector<std::vector<std::string_view>> views;
    for (const auto& parts : split)
        views.emplace_back(parts.begin(), parts.end());
    for (;;) {
        if (files.empty()) break;
        const auto& first = split.front();
        if (first.size() <= wrappers + 1) break;
        const auto name = first[wrappers];
        if (MatchStorageMarker(views.front(), wrappers) || PackageLike(name)) break;
        if (!std::all_of(split.begin(), split.end(),
                         [&](const auto& parts) { return parts.size() > wrappers + 1 && parts[wrappers] == name; }))
            break;
        ++wrappers;
    }
    const auto lower_package = Lower(package);
    const auto classify = [&](std::size_t skip) {
        // Package directories holding .obb files directly are OBB directories; other ones are app data.
        std::set<std::string> obb_directories;
        for (const auto& parts : split)
            if (parts.size() == skip + 2 && IsObbName(parts[skip + 1])) obb_directories.insert(parts[skip]);
        ArchivePlan plan;
        std::map<std::string, std::size_t> extras;
        for (std::size_t index = 0; index < files.size(); ++index) {
            const auto& parts = split[index];
            const auto depth = parts.size() - skip;
            const auto& top = parts[skip];
            const auto lower = Lower(top);
            // The outermost storage layout marker maps the member as it stands (a member's deeper parts are the
            // pack's own content, unlike a picked directory's host path, where the innermost wins); below a package
            // directory nothing counts. Anything else directly under Android/ (a readme) is not data.
            std::optional<std::pair<std::size_t, StorageMarker>> layout;
            for (auto at = skip; at + 1 < parts.size() && !layout; ++at) {
                if (const auto marker = MatchStorageMarker(views[index], at))
                    layout.emplace(at, *marker);
                else if (PackageLike(parts[at]))
                    break;
            }
            if (layout && layout->first + layout->second.parts < parts.size()) {
                plan.data.push_back({index, layout->second.guest + "/" +
                                                Join(parts, layout->first + layout->second.parts, parts.size())});
                continue;
            }
            if (lower == "android") continue;
            if (IsApk(files[index].path)) {
                plan.apks.push_back(index);
                continue;
            }
            if (depth == 1) {
                if (const auto owner = ExpansionPackage(top)) {
                    plan.data.push_back({index, PackageDirectoryMount(*owner, true) + "/" + top});
                    continue;
                }
                if (PackJunk(top)) continue;
            } else if (PackageLike(top) || (!lower_package.empty() && lower == lower_package)) {
                plan.data.push_back({index, PackageDirectoryMount(top, obb_directories.contains(top)) + "/" +
                                                Join(parts, skip + 1, parts.size())});
                continue;
            }
            auto [found, inserted] = extras.try_emplace(top, plan.extras.size());
            if (inserted) plan.extras.push_back({top, {}, 0});
            auto& extra = plan.extras[found->second];
            extra.members.push_back({index, "/sdcard/" + Join(parts, skip, parts.size())});
            extra.bytes += files[index].size;
            extra.directory = extra.directory || depth > 1;
        }
        std::stable_sort(plan.apks.begin(), plan.apks.end(), [&](std::size_t left, std::size_t right) {
            const auto key = [&](std::size_t index) {
                return std::tuple(split[index].size(), ModdedApk(files[index].path), files[index].path);
            };
            return key(left) < key(right);
        });
        return plan;
    };
    auto plan = classify(wrappers);
    // An early Gameloft game's pack with nothing else recognized holds its <GameID> directory: the lone directory
    // (wrappers included) named like a part of the package, or the only one.
    if (GameloftPackage(package) && plan.data.empty() && !plan.extras.empty()) {
        std::vector<const std::vector<std::string>*> members;
        for (const auto& extra : plan.extras)
            for (const auto& member : extra.members)
                members.push_back(&split[member.index]);
        const auto chain = LoneDirectories(members);
        if (const auto game = GameloftGameDirectory(chain, package, false)) {
            for (const auto* parts : members)
                if (parts->size() > *game + 1)
                    plan.data.push_back({static_cast<std::size_t>(parts - split.data()),
                                         std::string(kGameloftGames) + "/" + Join(*parts, *game, parts->size())});
            std::sort(plan.data.begin(), plan.data.end(),
                      [](const ArchiveData& left, const ArchiveData& right) { return left.index < right.index; });
            plan.extras.clear();
            return plan;
        }
    }
    // Wrappers are only looked through for what they reveal; with nothing recognized below them they are content.
    if (wrappers > 0 && plan.data.empty() && plan.apks.empty()) plan = classify(0);
    return plan;
}

DataDirectoryPlan PlanDataDirectory(const std::vector<ImportFile>& files, std::string_view base,
                                    std::string_view package) {
    const auto base_parts = Split(base.substr(base.starts_with('/') ? 1 : 0));

    // Each file's guest parts, Android/<kind> spelled canonically, and its package directory depth if any.
    struct Placed final {
        std::vector<std::string> guest;
        std::size_t package_at{};  // index of the package name in guest; 0 when outside one
    };

    std::vector<Placed> placed;
    placed.reserve(files.size());
    std::set<std::string> folded_names;
    for (const auto& file : files) {
        Placed item{base_parts, 0};
        for (auto& part : Split(file.path))
            item.guest.push_back(std::move(part));
        auto& guest = item.guest;
        if (guest.size() > 4 && guest[0] == "sdcard" && Lower(guest[1]) == "android") {
            const auto kind = Lower(guest[2]);
            if (kind == "data" || kind == "obb" || kind == "media") {
                guest[1] = "Android";
                guest[2] = kind;
                item.package_at = 3;
                folded_names.insert(Lower(guest[3]));
            }
        }
        placed.push_back(std::move(item));
    }
    const auto path = [](const std::vector<std::string>& parts, std::size_t count) {
        return "/" + Join(parts, 0, count);
    };
    DataDirectoryPlan result;
    const auto wanted = Lower(package);
    if (folded_names.contains(wanted))
        result.package = std::string(package);
    else if (folded_names.size() == 1 && PackageLike(*folded_names.begin()))
        result.package = *folded_names.begin();
    if (result.package.empty()) {
        // A directory picked for an early Gameloft game sits at kGameloftGames/<name> (GuessDataMount); it is the
        // game's own directory unless a lone directory below it is the one named like a part of the package.
        if (GameloftPackage(package) && base_parts.size() == 4 && Lower(base_parts[1]) == "gameloft" &&
            Lower(base_parts[2]) == "games") {
            std::vector<std::vector<std::string>> relative;
            std::vector<const std::vector<std::string>*> members;
            relative.reserve(files.size());
            for (const auto& file : files) {
                relative.push_back(Split(file.path));
                relative.back().insert(relative.back().begin(), base_parts[3]);
            }
            for (const auto& parts : relative)
                members.push_back(&parts);
            const auto game = GameloftGameDirectory(LoneDirectories(members), package, true).value_or(0);
            if (game > 0) {
                for (std::size_t index = 0; index < files.size(); ++index)
                    if (relative[index].size() > game + 1)
                        result.plan.data.push_back({index, std::string(kGameloftGames) + "/" +
                                                               Join(relative[index], game, relative[index].size())});
                return result;
            }
        }
        for (std::size_t index = 0; index < files.size(); ++index)
            result.plan.data.push_back({index, path(placed[index].guest, placed[index].guest.size())});
        return result;
    }
    const auto chosen = Lower(result.package);
    // The real spelling of the package directory, for the result.
    std::set<std::string> ancestors;  // relative parts leading to (and naming) a chosen package directory
    for (const auto& item : placed)
        if (item.package_at && Lower(item.guest[item.package_at]) == chosen) {
            result.package = item.guest[item.package_at];
            for (auto count = base_parts.size() + 1; count <= item.package_at + 1; ++count)
                ancestors.insert(Join(item.guest, base_parts.size(), count));
        }
    std::map<std::string, std::size_t> extras;
    for (std::size_t index = 0; index < files.size(); ++index) {
        const auto& item = placed[index];
        if (item.package_at && Lower(item.guest[item.package_at]) == chosen &&
            item.package_at + 1 < item.guest.size()) {
            result.plan.data.push_back({index, path(item.guest, item.guest.size())});
            continue;
        }
        const auto depth = item.guest.size() - base_parts.size();
        if (IsApk(files[index].path) || (depth == 1 && PackJunk(item.guest.back()))) continue;
        auto count = base_parts.size() + 1;
        while (count < item.guest.size() && ancestors.contains(Join(item.guest, base_parts.size(), count)))
            ++count;
        const auto name = Join(item.guest, base_parts.size(), count);
        auto [found, inserted] = extras.try_emplace(name, result.plan.extras.size());
        if (inserted) result.plan.extras.push_back({name, {}, 0});
        auto& extra = result.plan.extras[found->second];
        extra.members.push_back({index, path(item.guest, item.guest.size())});
        extra.bytes += files[index].size;
        extra.directory = extra.directory || count < item.guest.size();
    }
    return result;
}

std::string ArchiveMount(const ArchivePlan& plan, std::span<const std::string> extras) {
    std::vector<const ArchiveData*> chosen;
    for (const auto& item : plan.data)
        chosen.push_back(&item);
    bool any_extra = false;
    for (const auto& name : extras) {
        const auto found = std::find_if(plan.extras.begin(), plan.extras.end(),
                                        [&](const ArchiveExtra& extra) { return extra.name == name; });
        if (found == plan.extras.end())
            throw LibraryError(LibraryErrorCode::invalid_argument, "the chosen item is not in the archive: " + name,
                               UiMessage{"host.error.archiveEntryMissing", {{"name", name}}});
        any_extra = true;
        for (const auto& member : found->members)
            chosen.push_back(&member);
    }
    if (chosen.empty()) return {};
    if (any_extra) return "/sdcard";
    auto common = Split(chosen.front()->guest_path.substr(1));
    common.pop_back();
    for (const auto* item : chosen) {
        auto parts = Split(item->guest_path.substr(1));
        parts.pop_back();
        std::size_t shared = 0;
        while (shared < common.size() && shared < parts.size() && common[shared] == parts[shared])
            ++shared;
        common.resize(shared);
    }
    // sdcard/Android/<kind>/<package>: one app's own directory is as deep as a mount goes; Gameloft's games
    // directory holds each game's own one and mounts whole.
    if (common.size() > 4 && common[1] == "Android") common.resize(4);
    if (common.size() > 3 && Lower(common[1]) == "gameloft" && Lower(common[2]) == "games") common.resize(3);
    return "/" + Join(common, 0, common.size());
}

std::string ExtractArchiveData(ImportSource& source, const ArchivePlan& plan, std::span<const std::string> extras,
                               const std::filesystem::path& destination, ImportProgress* progress) {
    const auto mount = ArchiveMount(plan, extras);
    if (mount.empty()) return mount;
    std::vector<ImportExtraction> work;
    std::uint64_t total{};
    const auto add = [&](const ArchiveData& item) {
        const auto relative = item.guest_path.substr(mount.size() + 1);
        work.push_back({item.index, destination / ImportFilePath(relative)});
        total += source.Files()[item.index].size;
    };
    for (const auto& item : plan.data)
        add(item);
    for (const auto& name : extras)
        for (const auto& extra : plan.extras)
            if (extra.name == name)
                for (const auto& member : extra.members)
                    add(member);
    std::error_code error;
    std::filesystem::create_directories(destination, error);
    if (error) throw LibraryError(LibraryErrorCode::io_error, "cannot create import directory", destination);
    if (progress) progress->total_bytes.fetch_add(total, std::memory_order_relaxed);
    source.Extract(work, progress);
    return mount;
}

}  // namespace ogplay::app
