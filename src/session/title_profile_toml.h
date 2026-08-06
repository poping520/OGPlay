#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace ogplay::session::detail {

struct TomlValue final {
    using Array = std::vector<TomlValue>;
    using Table = std::map<std::string, TomlValue, std::less<>>;
    using Storage = std::variant<bool, std::int64_t, double, std::string, Array, Table>;

    Storage value;
};

[[nodiscard]] TomlValue::Table ParseDataToml(std::string_view text);

}  // namespace ogplay::session::detail
