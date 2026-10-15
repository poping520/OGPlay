#pragma once
#include "ogplay/app/export.h"
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>
namespace ogplay::app {
struct ProfileSummary final {
    std::string profile_id, display_name;
    bool requires_external_data{};
    std::vector<std::string> quirks{};
    std::optional<std::string> input_template{};
};
class OGPLAYCORE_API ProfileCatalog final {
public:
    ProfileCatalog();
    static ProfileCatalog LoadBundled();
    static ProfileCatalog Load(const std::filesystem::path& directory);
    std::optional<ProfileSummary> Match(std::span<const std::byte> apk) const;
    std::optional<ProfileSummary> Find(std::string_view id) const;
private:
    struct Impl;
    std::shared_ptr<const Impl> impl_;
    friend struct ProfileAccess;
};
}
