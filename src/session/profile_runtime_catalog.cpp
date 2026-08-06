#include "ogplay/session/profile_runtime_catalog.h"

#include <algorithm>
#include <cctype>
#include <set>
#include <string>
#include <utility>

namespace ogplay::session {
namespace {

[[nodiscard]] bool ValidComponent(const std::string_view value) {
    if (value.empty() ||
        std::islower(static_cast<unsigned char>(value.front())) == 0) {
        return false;
    }
    return std::all_of(value.begin() + 1, value.end(), [](const char character) {
        const auto byte = static_cast<unsigned char>(character);
        return std::islower(byte) != 0 || std::isdigit(byte) != 0 ||
               character == '_';
    });
}

[[nodiscard]] bool ValidImplementationId(const std::string_view value) {
    const auto separator = value.find('.');
    if (separator == std::string_view::npos) return false;
    std::size_t begin = 0;
    while (begin <= value.size()) {
        const auto end = value.find('.', begin);
        const auto component = value.substr(
            begin, end == std::string_view::npos ? value.size() - begin
                                                 : end - begin);
        if (!ValidComponent(component)) return false;
        if (end == std::string_view::npos) break;
        begin = end + 1;
    }
    return true;
}

}  // namespace

ProfileRuntimeCatalog::ProfileRuntimeCatalog(
    std::vector<ProfileJavaImplementation> java_implementations,
    input::InputTemplateCatalog input_templates)
    : java_implementations_(std::move(java_implementations)),
      input_templates_(std::move(input_templates)) {
    std::set<std::string, std::less<>> ids;
    for (const auto& implementation : java_implementations_) {
        if (!ValidImplementationId(implementation.implementation) ||
            !implementation.handler) {
            throw ProfileRuntimeCatalogError(
                "Profile runtime Java implementation is invalid");
        }
        if (!ids.insert(implementation.implementation).second) {
            throw ProfileRuntimeCatalogError(
                "Profile runtime Java implementation is duplicated: " +
                implementation.implementation);
        }
    }
}

bool ProfileRuntimeCatalog::ContainsJavaImplementation(
    const std::string_view implementation) const noexcept {
    return std::any_of(
        java_implementations_.begin(), java_implementations_.end(),
        [implementation](const ProfileJavaImplementation& candidate) {
            return candidate.implementation == implementation;
        });
}

std::span<const ProfileJavaImplementation>
ProfileRuntimeCatalog::JavaImplementations() const noexcept {
    return java_implementations_;
}

const input::InputTemplateCatalog&
ProfileRuntimeCatalog::InputTemplates() const noexcept {
    return input_templates_;
}

}  // namespace ogplay::session
