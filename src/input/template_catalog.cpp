#include "ogplay/input/template_catalog.h"

#include <string>
#include <utility>

#include "ogplay/core/text.h"

namespace ogplay::input {

InputTemplateCatalog::InputTemplateCatalog(
    std::string default_template, std::vector<InputTemplate> templates)
    : default_template_(std::move(default_template)) {
    if (!core::IsValidLowercaseIdentifier(default_template_)) {
        throw InputTemplateError("input default template id is invalid");
    }
    for (auto& input_template : templates) {
        if (!core::IsValidLowercaseIdentifier(input_template.id) || !input_template.mapper) {
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
