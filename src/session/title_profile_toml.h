#pragma once

#include <cstdint>
#include <initializer_list>
#include <map>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "ogplay/session/control_config.h"

namespace ogplay::session::detail {

struct TomlValue final {
    using Array = std::vector<TomlValue>;
    using Table = std::map<std::string, TomlValue, std::less<>>;
    using Storage = std::variant<bool, std::int64_t, double, std::string, Array, Table>;

    Storage value;
};

[[nodiscard]] TomlValue::Table ParseDataToml(std::string_view text);
// Decodes an [input]-shaped table (template, zone, binding); throws input::ControlConfigError.
[[nodiscard]] ControlLayer DecodeControlLayer(const TomlValue::Table& table, std::string_view field);

void ValidateExactTableKeys(
    const TomlValue::Table& table, std::string_view field,
    std::initializer_list<std::string_view> allowed,
    std::initializer_list<std::string_view> required = {});

}  // namespace ogplay::session::detail
