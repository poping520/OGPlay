#pragma once

#include <filesystem>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>

namespace ogplay::session {

struct TitleProfile;

struct QuirkDefinition final {
    std::string id;
    std::string summary;
    std::string reason;
    std::string risk;
    std::string test;
    std::string owner;
};

class QuirkRegistryError final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class QuirkRegistry final {
public:
    [[nodiscard]] static QuirkRegistry Load(const std::filesystem::path& path,
                                            const std::filesystem::path& source_root);
    // Packaged runtimes retain and validate the test reference shape, while
    // source existence is enforced before packaging by Load/CI validation.
    [[nodiscard]] static QuirkRegistry LoadPackaged(
        const std::filesystem::path& path);

    [[nodiscard]] const QuirkDefinition* Find(std::string_view id) const noexcept;
    [[nodiscard]] const std::map<std::string, QuirkDefinition, std::less<>>&
    Definitions() const noexcept;
    void Validate(const TitleProfile& profile) const;

private:
    explicit QuirkRegistry(
        std::map<std::string, QuirkDefinition, std::less<>> definitions);

    std::map<std::string, QuirkDefinition, std::less<>> definitions_;
};

}  // namespace ogplay::session
