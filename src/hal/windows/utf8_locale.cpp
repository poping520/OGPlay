#include "ogplay/hal/utf8_locale.h"

namespace ogplay::hal {

// Windows libraries convert through wide strings; there is no thread locale to change.
ScopedUtf8Locale::ScopedUtf8Locale() = default;
ScopedUtf8Locale::~ScopedUtf8Locale() = default;

}  // namespace ogplay::hal
