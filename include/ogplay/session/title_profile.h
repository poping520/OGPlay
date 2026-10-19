#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "ogplay/session/control_config.h"

namespace ogplay::session {

class QuirkRegistry;

struct ProfileIdentity final {
    std::string package;
    std::vector<std::uint32_t> version_codes;
    std::vector<std::string> so_sha256;
};

struct ProfileValue final {
    using Array = std::vector<ProfileValue>;
    using Table = std::map<std::string, ProfileValue, std::less<>>;
    using Storage = std::variant<bool, std::int64_t, double, std::string, Array, Table>;

    Storage value;
};

struct ProfileQuirks final {
    std::vector<std::string> enabled;
    std::map<std::string, ProfileValue::Table, std::less<>> parameters;
};

// [input]: template id, zones and bindings; merged by BuildControlConfig.
using ProfileInput = ControlLayer;

struct TitleProfile final {
    std::uint32_t schema{};
    ProfileIdentity identity;
    std::optional<ProfileQuirks> quirks;
    std::optional<ProfileInput> input;
};

class TitleProfileError final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

[[nodiscard]] TitleProfile LoadTitleProfileText(std::string_view text,
                                                std::string_view expected_package);
[[nodiscard]] TitleProfile LoadTitleProfile(const std::filesystem::path& path);

class TitleProfileCatalog final {
public:
    explicit TitleProfileCatalog(std::vector<TitleProfile> profiles);
    TitleProfileCatalog(std::vector<TitleProfile> profiles,
                        const QuirkRegistry& registry);

    [[nodiscard]] static TitleProfileCatalog LoadDirectory(
        const std::filesystem::path& directory);
    [[nodiscard]] static TitleProfileCatalog LoadDirectory(
        const std::filesystem::path& directory, const QuirkRegistry& registry);
    [[nodiscard]] const std::vector<TitleProfile>& Profiles() const noexcept;

private:
    void Validate(const QuirkRegistry* registry) const;

    std::vector<TitleProfile> profiles_;
};

}  // namespace ogplay::session
