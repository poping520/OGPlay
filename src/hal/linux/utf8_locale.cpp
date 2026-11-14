#include "ogplay/hal/utf8_locale.h"

#include <initializer_list>
#include <locale.h>

namespace ogplay::hal {

ScopedUtf8Locale::ScopedUtf8Locale() {
    for (const char* name : {"C.UTF-8", "C.utf8", "en_US.UTF-8"}) {
        if (locale_t created = newlocale(LC_CTYPE_MASK, name, static_cast<locale_t>(0))) {
            locale_ = created;
            previous_ = uselocale(created);
            return;
        }
    }
}

ScopedUtf8Locale::~ScopedUtf8Locale() {
    if (!locale_) return;
    uselocale(static_cast<locale_t>(previous_));
    freelocale(static_cast<locale_t>(locale_));
}

}  // namespace ogplay::hal
