#include "frontend/gui/credits.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <iterator>
#include <stdexcept>

#include "ogplay/app/json.h"

namespace ogplay::frontend {
namespace {

constexpr std::uintmax_t kMaximumNoticeBytes = 1024U * 1024U;

constexpr std::string_view CurrentPlatform() {
#if defined(_WIN32)
    return "Windows";
#elif defined(__APPLE__)
    return "Darwin";
#else
    return "Linux";
#endif
}

[[nodiscard]] bool AppliesToCurrentPlatform(const app::JsonValue& project) {
    const auto platforms = project.Member("platforms");
    if (!platforms) return true;
    if (!platforms->IsArray() || platforms->Size() == 0)
        throw std::runtime_error("credits.json: platforms must be a non-empty array");
    constexpr std::array<std::string_view, 3> supported{"Windows", "Darwin", "Linux"};
    bool matches = false;
    for (std::size_t index = 0; index < platforms->Size(); ++index) {
        const auto platform = platforms->Element(index)->String();
        if (!platform || std::find(supported.begin(), supported.end(), *platform) == supported.end())
            throw std::runtime_error("credits.json: invalid platform");
        matches = matches || *platform == CurrentPlatform();
    }
    return matches;
}

[[nodiscard]] std::string ReadBounded(const std::filesystem::path& path) {
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (error || size > kMaximumNoticeBytes) throw std::runtime_error("credits file unavailable: " + path.generic_string());
    std::ifstream file(path, std::ios::binary);
    std::string text{std::istreambuf_iterator<char>(file), {}};
    if (!file && !file.eof()) throw std::runtime_error("credits file unreadable: " + path.generic_string());
    return text;
}

[[nodiscard]] std::string RequiredString(const app::JsonValue& object, const std::string_view name) {
    const auto value = object.Member(name);
    if (!value || !value->String()) throw std::runtime_error("credits.json: missing " + std::string(name));
    return std::string(*value->String());
}

// Notices live inside the data root; reject absolute paths and parent steps.
[[nodiscard]] std::string NoticePath(std::string path) {
    const std::filesystem::path relative(path);
    if (path.empty() || relative.is_absolute() || relative.has_root_name() ||
        std::any_of(relative.begin(), relative.end(), [](const auto& part) { return part == ".."; }))
        throw std::runtime_error("credits.json: notice path leaves the data directory: " + path);
    return path;
}

}  // namespace

std::vector<CreditProject> LoadCredits(const std::filesystem::path& data_root) {
    app::JsonParseError error;
    const auto document = app::JsonDocument::ParseStrict(ReadBounded(data_root / "credits.json"), error);
    if (!document) throw std::runtime_error("credits.json: " + error.message);
    const auto root = document->Root();
    if (const auto schema = root.Member("schema"); !schema || schema->UnsignedInteger() != 1U)
        throw std::runtime_error("credits.json: unsupported schema");
    const auto projects = root.Member("projects");
    if (!projects || !projects->IsArray()) throw std::runtime_error("credits.json: missing projects");
    std::vector<CreditProject> result;
    for (std::size_t index = 0; index < projects->Size(); ++index) {
        const auto item = *projects->Element(index);
        if (!AppliesToCurrentPlatform(item)) continue;
        CreditProject project{RequiredString(item, "id"), RequiredString(item, "name"),
                              RequiredString(item, "version"), RequiredString(item, "url"), std::nullopt, {}, {}, {}};
        if (item.Member("original_url")) project.original_url = RequiredString(item, "original_url");
        if (const auto license = item.Member("license"); license && !license->IsNull()) {
            if (!license->String()) throw std::runtime_error("credits.json: license must be a string or null");
            project.license = std::string(*license->String());
        }
        if (item.Member("parent")) {
            project.parent = RequiredString(item, "parent");
            const auto is_parent = [&](const CreditProject& other) { return other.id == project.parent; };
            if (std::none_of(result.begin(), result.end(), is_parent))
                throw std::runtime_error("credits.json: " + project.id + " must follow its parent " + project.parent);
        }
        const auto notices = item.Member("notices");
        if (!notices || !notices->IsArray() || notices->Size() == 0)
            throw std::runtime_error("credits.json: " + project.id + " has no notices");
        for (std::size_t notice = 0; notice < notices->Size(); ++notice) {
            const auto value = *notices->Element(notice);
            if (value.Member("text").has_value() == value.Member("path").has_value())
                throw std::runtime_error("credits.json: a notice needs exactly one of text or path");
            if (value.Member("text"))
                project.notices.push_back({RequiredString(value, "title"), RequiredString(value, "text"), {}});
            else
                project.notices.push_back({RequiredString(value, "title"), {}, NoticePath(RequiredString(value, "path"))});
        }
        result.push_back(std::move(project));
    }
    return result;
}

std::vector<std::pair<std::string, std::string>> ReadCreditNotices(const std::filesystem::path& data_root,
                                                                   const std::string_view id) {
    const auto projects = LoadCredits(data_root);
    const auto found = std::find_if(projects.begin(), projects.end(), [&](const auto& item) { return item.id == id; });
    if (found == projects.end()) throw std::invalid_argument("unknown credits project: " + std::string(id));
    std::vector<std::pair<std::string, std::string>> result;
    for (const auto& notice : found->notices)
        result.emplace_back(notice.title,
                            notice.path.empty() ? notice.text : ReadBounded(data_root / std::filesystem::path(notice.path)));
    return result;
}

}  // namespace ogplay::frontend
