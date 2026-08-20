#include "ogplay/hal/from_chars.h"

namespace ogplay::hal {

std::from_chars_result FromChars(const char* first, const char* last,
                                 float& value, std::chars_format fmt) {
    return std::from_chars(first, last, value, fmt);
}

std::from_chars_result FromChars(const char* first, const char* last,
                                 double& value, std::chars_format fmt) {
    return std::from_chars(first, last, value, fmt);
}

}  // namespace ogplay::hal
