#pragma once

#include <map>
#include <stdexcept>
#include <string>
#include <utility>

namespace ogplay::frontend {
// Private GUI template error; path/status values stay separate from the translation key.
class GuiTextError final : public std::runtime_error {
public:
    GuiTextError(std::string message, std::map<std::string, std::string> params)
        : std::runtime_error(std::move(message)), params_(std::move(params)) {}

    const std::map<std::string, std::string>& Params() const { return params_; }

private:
    std::map<std::string, std::string> params_;
};
}  // namespace ogplay::frontend
