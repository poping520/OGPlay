#include <string>
#include <vector>

#include <doctest/doctest.h>

#include "ogplay/session/profile_session_bootstrap.h"

namespace {

constexpr std::string_view kHashA =
    "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
constexpr std::string_view kHashB =
    "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";

[[nodiscard]] ogplay::session::TitleProfile ExactProfile() {
    ogplay::session::TitleProfile profile;
    profile.schema = 1;
    profile.identity = {
        "org.example.exact", "exact", {7}, {std::string(kHashA)},
        "armeabi-v7a"};
    profile.runtime.lifecycle =
        ogplay::session::ProfileLifecycle::custom_jni;
    profile.input = ogplay::session::ProfileInput{"exact_input"};
    return profile;
}

[[nodiscard]] ogplay::session::TitleProfile GenericDefault() {
    ogplay::session::TitleProfile profile;
    profile.identity.name = "generic";
    profile.runtime.lifecycle =
        ogplay::session::ProfileLifecycle::native_activity;
    return profile;
}

[[nodiscard]] ogplay::input::InputTemplateCatalog InputTemplates() {
    const auto direct = [](const std::span<const ogplay::hal::InputEvent> events) {
        return std::vector<ogplay::hal::InputEvent>(events.begin(), events.end());
    };
    return {"generic_input",
            {{"generic_input", direct}, {"exact_input", direct}}};
}

[[nodiscard]] ogplay::session::TitleIdentity ExactIdentity() {
    return {"org.example.exact", 7, std::string(kHashA)};
}

}  // namespace

TEST_CASE("session bootstrap assembles only the exact matched Profile") {
    const ogplay::session::TitleProfileCatalog profiles{{ExactProfile()}};
    const auto inputs = InputTemplates();
    auto result = ogplay::session::BootstrapProfileSession(
        profiles, ExactIdentity(), GenericDefault(), {}, {}, inputs, {});

    CHECK(result.selection ==
          ogplay::session::ProfileSessionSelection::exact_profile);
    CHECK(result.plan.Profile().identity.name == "exact");
    CHECK(result.plan.Lifecycle().lifecycle ==
          ogplay::session::ProfileLifecycle::custom_jni);
    CHECK(result.plan.InputTemplate() == "exact_input");
}

TEST_CASE("session bootstrap uses the explicit generic default on no match") {
    const ogplay::session::TitleProfileCatalog profiles{{ExactProfile()}};
    const auto inputs = InputTemplates();
    const ogplay::session::TitleIdentity unknown{
        "org.example.unknown", 7, std::string(kHashB)};
    auto result = ogplay::session::BootstrapProfileSession(
        profiles, unknown, GenericDefault(), {}, {}, inputs, {});

    CHECK(result.selection ==
          ogplay::session::ProfileSessionSelection::generic_default);
    CHECK(result.plan.Profile().identity.name == "generic");
    CHECK(result.plan.Lifecycle().lifecycle ==
          ogplay::session::ProfileLifecycle::native_activity);
    CHECK(result.plan.InputTemplate() == "generic_input");
}

TEST_CASE("session bootstrap rejects invalid identity before defaulting") {
    const ogplay::session::TitleProfileCatalog profiles{{ExactProfile()}};
    const auto inputs = InputTemplates();
    auto invalid = ExactIdentity();
    invalid.so_sha256 = "short";

    CHECK_THROWS_WITH_AS(
        static_cast<void>(ogplay::session::BootstrapProfileSession(
            profiles, invalid, GenericDefault(), {}, {}, inputs, {})),
        "Title Profile match requires package, positive versionCode and SHA-256",
        ogplay::session::TitleProfileError);
}

TEST_CASE("session bootstrap never falls back after selected assembly fails") {
    auto exact = ExactProfile();
    exact.java_classes = {
        {"fixture/RequiredBridge", {{"start", "()V", "required.start"}}}};
    const ogplay::session::TitleProfileCatalog profiles{{exact}};
    const auto inputs = InputTemplates();
    CHECK_THROWS_WITH_AS(
        static_cast<void>(ogplay::session::BootstrapProfileSession(
            profiles, ExactIdentity(), GenericDefault(), {}, {}, inputs, {})),
        "Profile Java method has no registered implementation: required.start",
        ogplay::session::ProfileJavaError);

    auto generic = GenericDefault();
    generic.data = ogplay::session::ProfileData{
        .mounts = {{"/required", ogplay::session::ProfileSource::external,
                    true}}};
    const ogplay::session::TitleIdentity unknown{
        "org.example.unknown", 7, std::string(kHashB)};
    CHECK_THROWS_WITH_AS(
        static_cast<void>(ogplay::session::BootstrapProfileSession(
            profiles, unknown, generic, {}, {}, inputs, {})),
        "required profile VFS mount is missing: /required",
        ogplay::session::ProfileVfsError);
}
