#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ogplay::session {

struct ProfileIdentity final {
    std::string package;
    std::vector<std::uint32_t> version_codes;
    std::vector<std::string> so_sha256;
};

struct TitleProfile final {
    std::uint32_t schema{};
    ProfileIdentity identity;
};

class TitleProfileError final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

[[nodiscard]] TitleProfile LoadTitleProfileText(std::string_view text, std::string_view expected_package);
[[nodiscard]] TitleProfile LoadTitleProfile(const std::filesystem::path& path);

class TitleProfileCatalog final {
public:
    explicit TitleProfileCatalog(std::vector<TitleProfile> profiles);

    [[nodiscard]] static TitleProfileCatalog LoadDirectory(const std::filesystem::path& directory);
    [[nodiscard]] const std::vector<TitleProfile>& Profiles() const noexcept;

private:
    void Validate() const;

    std::vector<TitleProfile> profiles_;
};

}  // namespace ogplay::session
