#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ogplay::frontend {

// Open-source acknowledgements shipped as data/credits.json (written by tools/credits.py). A notice
// carries its text inline, or names a notice that already ships elsewhere under the data root.
struct CreditNotice final {
    std::string title;
    std::string text;
    std::string path;  // relative to the data root; empty for an inline text
};

struct CreditProject final {
    std::string id;
    std::string name;
    std::string version;  // empty when upstream has no release number for the vendored copy
    std::string url;
    std::optional<std::string> license;  // SPDX-style id; none when the texts carry several
    std::vector<CreditNotice> notices;
};

// Throws when the list is missing or malformed, or a notice path leaves the data root.
[[nodiscard]] std::vector<CreditProject> LoadCredits(const std::filesystem::path& data_root);

// The license texts of one project, in list order, as (title, text); throws for an unknown id
// or an unreadable notice.
[[nodiscard]] std::vector<std::pair<std::string, std::string>> ReadCreditNotices(
    const std::filesystem::path& data_root, std::string_view id);

}  // namespace ogplay::frontend
