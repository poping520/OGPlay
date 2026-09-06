#pragma once

#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unicode/unistr.h>
#include <unicode/utypes.h>

namespace ogplay::runtime::dexvm {

void InitializePinnedIcu();

inline void CheckIcu(const UErrorCode status) {
  if (U_FAILURE(status)) {
    if (status == U_MEMORY_ALLOCATION_ERROR) throw std::bad_alloc{};
    throw std::invalid_argument(std::string("ICU51: ") + u_errorName(status));
  }
}

inline icu::UnicodeString IcuString(const std::u16string_view text) {
  if (text.size() > static_cast<std::size_t>(INT32_MAX))
    throw std::length_error("ICU string exceeds int32 length");
  return icu::UnicodeString(reinterpret_cast<const UChar*>(text.data()),
                            static_cast<std::int32_t>(text.size()));
}

inline std::u16string FromIcu(const icu::UnicodeString& text) {
  return std::u16string(reinterpret_cast<const char16_t*>(text.getBuffer()),
                        static_cast<std::size_t>(text.length()));
}

}  // namespace ogplay::runtime::dexvm
