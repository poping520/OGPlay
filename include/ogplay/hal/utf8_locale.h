#pragma once

namespace ogplay::hal {

// Makes the calling thread's character type UTF-8 while it lives, for C libraries that convert text through the
// current locale (libarchive turns RAR member names into the locale's charset; in the default "C" locale every
// non-ASCII name would be lost). Nothing changes for other threads; a no-op on Windows, whose libraries use wide
// strings, and when the host has no UTF-8 locale.
class ScopedUtf8Locale final {
public:
    ScopedUtf8Locale();
    ~ScopedUtf8Locale();
    ScopedUtf8Locale(const ScopedUtf8Locale&) = delete;
    ScopedUtf8Locale& operator=(const ScopedUtf8Locale&) = delete;

private:
    void* locale_{};
    void* previous_{};
};

}  // namespace ogplay::hal
