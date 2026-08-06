#include <algorithm>
#include <vector>

#include <doctest/doctest.h>

#include "ogplay/session/profile_input.h"

namespace {

[[nodiscard]] ogplay::input::InputTemplateCatalog Catalog() {
    return {
        "direct",
        {{"direct",
          [](const std::span<const ogplay::hal::InputEvent> events) {
              return std::vector<ogplay::hal::InputEvent>(events.begin(),
                                                          events.end());
          }},
         {"pointer_only",
          [](const std::span<const ogplay::hal::InputEvent> events) {
              std::vector<ogplay::hal::InputEvent> result;
              std::copy_if(events.begin(), events.end(),
                           std::back_inserter(result), [](const auto& event) {
                               return event.type ==
                                          ogplay::hal::InputEventType::pointer_motion ||
                                      event.type ==
                                          ogplay::hal::InputEventType::pointer_button;
                           });
              return result;
          }}},
    };
}

[[nodiscard]] std::vector<ogplay::hal::InputEvent> Events() {
    return {
        {.type = ogplay::hal::InputEventType::key, .code = 7, .pressed = true},
        {.type = ogplay::hal::InputEventType::pointer_motion,
         .x = 10.0F,
         .y = 20.0F},
        {.type = ogplay::hal::InputEventType::pointer_button,
         .code = 1,
         .pressed = true},
    };
}

}  // namespace

TEST_CASE("input template catalog applies immutable code-defined mappers") {
    const auto catalog = Catalog();
    const auto events = Events();
    CHECK(catalog.DefaultTemplate() == "direct");
    CHECK(catalog.Contains("pointer_only"));
    CHECK_FALSE(catalog.Contains("missing"));

    const auto direct = catalog.Map("direct", events);
    REQUIRE(direct.size() == events.size());
    CHECK(direct[0].type == ogplay::hal::InputEventType::key);
    CHECK(direct[2].type == ogplay::hal::InputEventType::pointer_button);

    const auto pointer = catalog.Map("pointer_only", events);
    REQUIRE(pointer.size() == 2);
    CHECK(pointer[0].type == ogplay::hal::InputEventType::pointer_motion);
    CHECK(pointer[1].type == ogplay::hal::InputEventType::pointer_button);
}

TEST_CASE("Profile input selects an exact template or the explicit default") {
    const auto catalog = Catalog();
    const auto events = Events();
    ogplay::session::TitleProfile profile;

    CHECK(ogplay::session::ApplyProfileInput(profile, catalog, events).size() ==
          events.size());
    profile.input = ogplay::session::ProfileInput{"pointer_only"};
    CHECK(ogplay::session::ApplyProfileInput(profile, catalog, events).size() ==
          2);

    profile.input = ogplay::session::ProfileInput{"missing"};
    CHECK_THROWS_WITH_AS(
        static_cast<void>(
            ogplay::session::ApplyProfileInput(profile, catalog, events)),
        "input template is not registered: missing",
        ogplay::input::InputTemplateError);
}

TEST_CASE("input template catalog rejects invalid ambiguous definitions") {
    const auto mapper = [](const std::span<const ogplay::hal::InputEvent> events) {
        return std::vector<ogplay::hal::InputEvent>(events.begin(), events.end());
    };
    CHECK_THROWS_AS(
        static_cast<void>(ogplay::input::InputTemplateCatalog{
            "direct", {{"direct", mapper}, {"direct", mapper}}}),
        ogplay::input::InputTemplateError);
    CHECK_THROWS_AS(
        static_cast<void>(ogplay::input::InputTemplateCatalog{
            "missing", {{"direct", mapper}}}),
        ogplay::input::InputTemplateError);
    CHECK_THROWS_AS(
        static_cast<void>(ogplay::input::InputTemplateCatalog{
            "GameSpecific", {{"GameSpecific", mapper}}}),
        ogplay::input::InputTemplateError);
    CHECK_THROWS_AS(
        static_cast<void>(ogplay::input::InputTemplateCatalog{
            "direct", {{"direct", {}}}}),
        ogplay::input::InputTemplateError);
}
