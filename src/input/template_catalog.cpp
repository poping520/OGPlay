#include "ogplay/input/template_catalog.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <utility>

namespace ogplay::input {
namespace {

[[nodiscard]] bool ValidId(const std::string_view value) {
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

}  // namespace

InputTemplateCatalog::InputTemplateCatalog(
    std::string default_template, std::vector<InputTemplate> templates)
    : default_template_(std::move(default_template)) {
    if (!ValidId(default_template_)) {
        throw InputTemplateError("input default template id is invalid");
    }
    for (auto& input_template : templates) {
        if (!ValidId(input_template.id) || !input_template.mapper) {
            throw InputTemplateError(
                "input template requires a valid id and mapper");
        }
        if (!templates_
                 .emplace(std::move(input_template.id),
                          std::move(input_template.mapper))
                 .second) {
            throw InputTemplateError("input template id is duplicated");
        }
    }
    if (!templates_.contains(default_template_)) {
        throw InputTemplateError(
            "input default template is not registered");
    }
}

std::string_view InputTemplateCatalog::DefaultTemplate() const noexcept {
    return default_template_;
}

bool InputTemplateCatalog::Contains(const std::string_view id) const noexcept {
    return templates_.contains(id);
}

std::vector<hal::InputEvent> InputTemplateCatalog::Map(
    const std::string_view id,
    const std::span<const hal::InputEvent> events) const {
    const auto found = templates_.find(id);
    if (found == templates_.end()) {
        throw InputTemplateError("input template is not registered: " +
                                 std::string(id));
    }
    return found->second(events);
}

}  // namespace ogplay::input
