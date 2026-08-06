#include <doctest/doctest.h>

#include <array>
#include <filesystem>
#include <string>

#include "ogplay/hal/host_environment.h"

TEST_CASE("host environment overrides restore nested process state") {
    constexpr auto name = "OGPLAY_TEST_SCOPED_HOST_ENVIRONMENT";
    const auto initial = ogplay::hal::HostEnvironmentValue(name);
    {
        const std::array outer{
            ogplay::hal::HostEnvironmentOverride{
                name, std::string("outer")}};
        ogplay::hal::ScopedHostEnvironment outer_scope(outer);
        CHECK(ogplay::hal::HostEnvironmentValue(name) == "outer");
        {
            const std::array inner{
                ogplay::hal::HostEnvironmentOverride{
                    name, std::string("inner")}};
            ogplay::hal::ScopedHostEnvironment inner_scope(inner);
            CHECK(ogplay::hal::HostEnvironmentValue(name) == "inner");
        }
        CHECK(ogplay::hal::HostEnvironmentValue(name) == "outer");
    }
    CHECK(ogplay::hal::HostEnvironmentValue(name) == initial);
}

TEST_CASE("host environment rejects an invalid override set atomically") {
    constexpr auto name = "OGPLAY_TEST_INVALID_HOST_ENVIRONMENT";
    const auto initial = ogplay::hal::HostEnvironmentValue(name);
    const std::array duplicate{
        ogplay::hal::HostEnvironmentOverride{
            name, std::string("first")},
        ogplay::hal::HostEnvironmentOverride{
            name, std::string("second")}};
    const auto create_duplicate_scope = [&duplicate] {
        ogplay::hal::ScopedHostEnvironment scope(duplicate);
    };
    CHECK_THROWS_AS(create_duplicate_scope(), std::invalid_argument);
    CHECK(ogplay::hal::HostEnvironmentValue(name) == initial);
}

TEST_CASE("host executable directory is absolute and contains the test binary") {
    const auto directory = ogplay::hal::HostExecutableDirectory();
    CHECK(directory.is_absolute());
    CHECK(std::filesystem::is_directory(directory));
}
