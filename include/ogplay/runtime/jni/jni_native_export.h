#pragma once

#include <stdexcept>
#include <string>
#include <string_view>

namespace ogplay::runtime {

struct JniNativeExportNames final {
    std::string short_name;
    std::string long_name;
};

class JniNativeExportError final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

[[nodiscard]] JniNativeExportNames BuildJniNativeExportNames(
    std::string_view class_name, std::string_view method_name,
    std::string_view method_descriptor);

}  // namespace ogplay::runtime
