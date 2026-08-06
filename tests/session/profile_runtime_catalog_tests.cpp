#include <string>
#include <vector>

#include <doctest/doctest.h>

#include "ogplay/session/profile_runtime_catalog.h"

namespace {

[[nodiscard]] ogplay::input::InputTemplateCatalog InputTemplates() {
    return {"direct",
            {{"direct",
              [](const std::span<const ogplay::hal::InputEvent> events) {
                  return std::vector<ogplay::hal::InputEvent>(events.begin(),
                                                              events.end());
              }}}};
}

[[nodiscard]] ogplay::runtime::JniValue Handler(
    const ogplay::runtime::JniInvocation&) {
    return std::monostate{};
}

}  // namespace

TEST_CASE("Profile runtime catalog owns stable generic implementations") {
    std::vector implementations{
        ogplay::session::ProfileJavaImplementation{"resource.open", Handler},
        ogplay::session::ProfileJavaImplementation{"state.reset", Handler},
    };
    ogplay::session::ProfileRuntimeCatalog catalog{implementations,
                                                    InputTemplates()};
    implementations.front().implementation = "mutated.value";

    REQUIRE(catalog.JavaImplementations().size() == 2);
    CHECK(catalog.JavaImplementations()[0].implementation == "resource.open");
    CHECK(catalog.JavaImplementations()[1].implementation == "state.reset");
    CHECK(catalog.ContainsJavaImplementation("state.reset"));
    CHECK_FALSE(catalog.ContainsJavaImplementation("state"));
    CHECK(catalog.InputTemplates().DefaultTemplate() == "direct");
}

TEST_CASE("Profile runtime catalog rejects invalid and duplicate handlers") {
    CHECK_THROWS_AS(
        static_cast<void>(ogplay::session::ProfileRuntimeCatalog{
            {{"invalid", Handler}}, InputTemplates()}),
        ogplay::session::ProfileRuntimeCatalogError);
    CHECK_THROWS_AS(
        static_cast<void>(ogplay::session::ProfileRuntimeCatalog{
            {{"Invalid.value", Handler}}, InputTemplates()}),
        ogplay::session::ProfileRuntimeCatalogError);
    CHECK_THROWS_AS(
        static_cast<void>(ogplay::session::ProfileRuntimeCatalog{
            {{"valid.value", {}}}, InputTemplates()}),
        ogplay::session::ProfileRuntimeCatalogError);
    CHECK_THROWS_WITH_AS(
        static_cast<void>(ogplay::session::ProfileRuntimeCatalog{
            {{"valid.value", Handler}, {"valid.value", Handler}},
            InputTemplates()}),
        "Profile runtime Java implementation is duplicated: valid.value",
        ogplay::session::ProfileRuntimeCatalogError);
}
