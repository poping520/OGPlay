#pragma once

#include <functional>
#include <map>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "ogplay/hal/window_input.h"

namespace ogplay::input {

using InputTemplateMapper = std::function<std::vector<hal::InputEvent>(
    std::span<const hal::InputEvent>)>;

struct InputTemplate final {
    std::string id;
    InputTemplateMapper mapper;
};

class InputTemplateError final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class InputTemplateCatalog final {
public:
    InputTemplateCatalog(std::string default_template,
                         std::vector<InputTemplate> templates);

    [[nodiscard]] std::string_view DefaultTemplate() const noexcept;
    [[nodiscard]] bool Contains(std::string_view id) const noexcept;
    [[nodiscard]] std::vector<hal::InputEvent> Map(
        std::string_view id, std::span<const hal::InputEvent> events) const;

private:
    std::string default_template_;
    std::map<std::string, InputTemplateMapper, std::less<>> templates_;
};

}  // namespace ogplay::input
