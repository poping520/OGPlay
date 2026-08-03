#include <ostream>

#include <doctest/doctest.h>

#include "ogplay/runtime/bionic_profile.h"

TEST_CASE("Bionic profiles select only API 19 22 and 23") {
    const auto& api19 = ogplay::runtime::SelectBionicProfile(19);
    const auto& api22 = ogplay::runtime::SelectBionicProfile(22);
    const auto& api23 = ogplay::runtime::SelectBionicProfile(23);
    CHECK(api19.api == ogplay::runtime::AndroidApi::api19);
    CHECK(api19.android_release == "4.4");
    CHECK(api19.data_directory == "bionic/19");
    CHECK(api22.api == ogplay::runtime::AndroidApi::api22);
    CHECK(api22.data_directory == "bionic/22");
    CHECK(api23.api == ogplay::runtime::AndroidApi::api23);
    CHECK(api23.data_directory == "bionic/23");
    CHECK(api19.guest_libraries.size() == 5);
    CHECK(api19.boundary_libraries.size() == 9);
    CHECK_THROWS_AS(static_cast<void>(ogplay::runtime::SelectBionicProfile(21)),
                    ogplay::runtime::BionicProfileError);
}

TEST_CASE("Bionic routing separates guest intercept and HLE boundary symbols") {
    const auto& profile = ogplay::runtime::SelectBionicProfile(19);
    CHECK(ogplay::runtime::RouteBionicSymbol(profile, "libc.so", "snprintf") ==
          ogplay::runtime::BionicSymbolRoute::guest_execution);
    CHECK(ogplay::runtime::RouteBionicSymbol(profile, "libc.so", "memcpy") ==
          ogplay::runtime::BionicSymbolRoute::host_intercept);
    CHECK(ogplay::runtime::RouteBionicSymbol(profile, "libc.so",
                                             "pthread_create") ==
          ogplay::runtime::BionicSymbolRoute::host_intercept);
    CHECK(ogplay::runtime::RouteBionicSymbol(profile, "libEGL.so", "eglSwapBuffers") ==
          ogplay::runtime::BionicSymbolRoute::host_boundary);
    CHECK(ogplay::runtime::RouteBionicSymbol(profile, "liblog.so",
                                             "__android_log_write") ==
          ogplay::runtime::BionicSymbolRoute::host_boundary);
    CHECK_THROWS_AS(static_cast<void>(ogplay::runtime::RouteBionicSymbol(
                        profile, "libc.so", "")),
                    ogplay::runtime::BionicProfileError);
}
