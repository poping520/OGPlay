#pragma once

#include <span>
#include <stdexcept>
#include <string_view>
#include <vector>

#include "ogplay/input/template_catalog.h"
#include "ogplay/session/profile_java.h"

namespace ogplay::session {

class ProfileRuntimeCatalogError final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class ProfileRuntimeCatalog final {
public:
    ProfileRuntimeCatalog(
        std::vector<ProfileJavaImplementation> java_implementations,
        input::InputTemplateCatalog input_templates);

    [[nodiscard]] bool ContainsJavaImplementation(
        std::string_view implementation) const noexcept;
    [[nodiscard]] std::span<const ProfileJavaImplementation>
    JavaImplementations() const noexcept;
    [[nodiscard]] const input::InputTemplateCatalog&
    InputTemplates() const noexcept;

private:
    std::vector<ProfileJavaImplementation> java_implementations_;
    input::InputTemplateCatalog input_templates_;
};

}  // namespace ogplay::session
