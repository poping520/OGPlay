#include "ogplay/runtime/bionic_profile.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace ogplay::runtime {
namespace {

constexpr std::array<std::string_view, 5> kGuestLibraries{
    "libc.so", "libm.so", "libdl.so", "libstdc++.so", "libz.so"};
constexpr std::array<std::string_view, 9> kBoundaryLibraries{
    "libEGL.so",       "libGLESv1_CM.so", "libGLESv2.so",
    "libGLESv3.so",   "libOpenSLES.so",  "libandroid.so",
    "libjnigraphics.so", "liblog.so",     "libmediandk.so"};
constexpr std::array<std::string_view, 24> kInterceptedLibcSymbols{
    "memcmp",  "memcpy",  "memmove", "memset",  "strcat", "strchr",
    "strcmp",  "strcpy",  "strcspn", "strlen",  "strncat", "strncmp",
    "strncpy", "strnlen", "strpbrk", "strrchr", "strspn", "strstr",
    "pthread_create", "pthread_exit", "pthread_join", "pthread_self",
    "pthread_mutex_lock", "pthread_mutex_unlock"};

constexpr BionicProfile kApi19{AndroidApi::api19, "4.4", "bionic/19",
                               kGuestLibraries, kBoundaryLibraries};
constexpr BionicProfile kApi22{AndroidApi::api22, "5.1", "bionic/22",
                               kGuestLibraries, kBoundaryLibraries};
constexpr BionicProfile kApi23{AndroidApi::api23, "6.0", "bionic/23",
                               kGuestLibraries, kBoundaryLibraries};

[[nodiscard]] bool Contains(const std::span<const std::string_view> values,
                            const std::string_view value) {
    return std::find(values.begin(), values.end(), value) != values.end();
}

}  // namespace

const BionicProfile& SelectBionicProfile(const std::uint32_t api) {
    switch (api) {
        case 19:
            return kApi19;
        case 22:
            return kApi22;
        case 23:
            return kApi23;
        default:
            throw BionicProfileError("unsupported Android Bionic API " +
                                     std::to_string(api));
    }
}

BionicSymbolRoute RouteBionicSymbol(const BionicProfile& profile,
                                    const std::string_view library,
                                    const std::string_view symbol) {
    if (library.empty() || symbol.empty()) {
        throw BionicProfileError("Bionic route requires a library and symbol");
    }
    if (Contains(profile.boundary_libraries, library)) {
        return BionicSymbolRoute::host_boundary;
    }
    if (library == "libc.so" &&
        std::find(kInterceptedLibcSymbols.begin(),
                  kInterceptedLibcSymbols.end(), symbol) !=
            kInterceptedLibcSymbols.end()) {
        return BionicSymbolRoute::host_intercept;
    }
    return BionicSymbolRoute::guest_execution;
}

}  // namespace ogplay::runtime
