#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <variant>

#include "ogplay/session/title_profile.h"

namespace {

constexpr std::string_view kHash =
    "0000000000000000000000000000000000000000000000000000000000000000";

[[nodiscard]] std::string BaseProfile(
    const std::string_view package = "org.example.game") {
    return "schema = 2\n"
           "[identity]\n"
           "package = \"" + std::string(package) + "\"\n"
           "name = \"DexVM Fixture\"\n"
           "version_code = [7]\n"
           "so_sha256 = [\"" + std::string(kHash) + "\"]\n"
           "abi = \"armeabi-v7a\"\n"
           "[runtime]\n"
           "api_level = 19\n"
           "lifecycle = \"dex_activity\"\n"
           "[runtime.surface]\n"
           "width = 800\n"
           "height = 480\n";
}

[[nodiscard]] std::string ScopedProfile() {
    return BaseProfile() +
           "[runtime.entry]\n"
           "launch_activity = \"org.example.game.MainActivity\"\n"
           "[[runtime.presets]]\n"
           "class = \"org.example.game.InstallState\"\n"
           "field = \"ready\"\n"
           "type = \"Z\"\n"
           "value = true\n"
           "reason = \"fixture data is provisioned\"\n"
           "[data]\n"
           "working_directory = \"/sdcard/game\"\n"
           "mounts = [{ guest = \"/sdcard/game\", source = \"external\", required = true }]\n"
           "manifest = [{ path = \"archive.dat\", required = true }]\n";
}

[[nodiscard]] std::string V3Profile() {
    return "schema = 3\n"
           "[identity]\n"
           "package = \"org.example.game\"\n"
           "name = \"Optional Fixture\"\n"
           "[runtime]\n"
           "api_level = 19\n";
}

void CheckRejected(const std::string& text, const std::string_view message) {
    try {
        static_cast<void>(ogplay::session::LoadTitleProfileText(
            text, "org.example.game"));
        FAIL("invalid Profile was accepted");
    } catch (const ogplay::session::TitleProfileError& error) {
        CHECK(std::string_view(error.what()).find(message) !=
              std::string_view::npos);
    }
}

}  // namespace

TEST_CASE("Title Profile v2 loader decodes exact identity and runtime") {
    const auto profile = ogplay::session::LoadTitleProfileText(
        BaseProfile(), "org.example.game");
    CHECK(profile.schema == 2U);
    CHECK(profile.identity.package == "org.example.game");
    CHECK(profile.identity.version_codes == std::vector<std::uint32_t>{7U});
    CHECK(profile.runtime.lifecycle ==
          ogplay::session::ProfileLifecycle::dex_activity);
    CHECK(profile.runtime.surface.width == 800U);
    CHECK(profile.runtime.surface.height == 480U);
    CHECK_FALSE(profile.runtime.entry.has_value());
    CHECK(profile.runtime.presets.empty());
}

TEST_CASE("Title Profile v2 validates the DexVM interpreter backend") {
    auto threaded = BaseProfile();
    threaded += "[runtime.dexvm]\ninterpreter = \"threaded\"\n";
    const auto profile = ogplay::session::LoadTitleProfileText(
        threaded, "org.example.game");
    REQUIRE(profile.runtime.dexvm.has_value());
    CHECK(profile.runtime.dexvm->interpreter ==
          ogplay::session::ProfileRuntime::DexVm::Interpreter::threaded);

    auto invalid = BaseProfile();
    invalid += "[runtime.dexvm]\ninterpreter = \"jit\"\n";
    CheckRejected(invalid,
                  "runtime.dexvm.interpreter must be switch or threaded");
}

TEST_CASE("Title Profile v2 decodes entry override and audited preset") {
    const auto profile = ogplay::session::LoadTitleProfileText(
        ScopedProfile(), "org.example.game");
    REQUIRE(profile.runtime.entry.has_value());
    CHECK(profile.runtime.entry->launch_activity ==
          "org.example.game.MainActivity");
    REQUIRE(profile.runtime.presets.size() == 1U);
    const auto& preset = profile.runtime.presets.front();
    CHECK(preset.class_name == "org.example.game.InstallState");
    CHECK(preset.field == "ready");
    CHECK(preset.type == "Z");
    CHECK(std::get<bool>(preset.value));
    CHECK(preset.reason == "fixture data is provisioned");
}

TEST_CASE("Title Profile v1 is adapted without restoring replay startup") {
    auto v1 = BaseProfile();
    v1.replace(v1.find("schema = 2"), 10U, "schema = 1");
    v1.replace(v1.find("dex_activity"), 12U, "gl_surface_view");
    v1 += "[[runtime.native_call]]\nphase = \"startup\"\n"
          "[[java.class]]\nname = \"org/example/Legacy\"\n";
    const auto adapted = ogplay::session::LoadTitleProfileText(
        v1, "org.example.game");
    CHECK(adapted.schema == 1U);
    CHECK(adapted.runtime.lifecycle ==
          ogplay::session::ProfileLifecycle::dex_activity);

    auto native_call = BaseProfile();
    native_call += "[[runtime.native_call]]\nphase = \"startup\"\n";
    CheckRejected(native_call, "runtime has unknown field native_call");

    auto java = BaseProfile();
    java += "[[java.class]]\nname = \"org/example/Legacy\"\n";
    CheckRejected(java, "forbids legacy java replay glue");
}

TEST_CASE("Title Profile v3 permits optional applicability guards") {
    const auto profile = ogplay::session::LoadTitleProfileText(
        V3Profile(), "org.example.game");
    CHECK(profile.schema == 3U);
    CHECK(profile.identity.version_codes.empty());
    CHECK(profile.identity.so_sha256.empty());
    CHECK_FALSE(profile.identity.has_abi_guard);
    CHECK(profile.runtime.lifecycle ==
          ogplay::session::ProfileLifecycle::dex_activity);
    CHECK(profile.runtime.surface.width == 800U);
    CHECK(profile.runtime.surface.height == 480U);

    auto abi = V3Profile();
    abi.replace(abi.find("name ="), 0U, "abi = \"armeabi\"\n");
    CheckRejected(abi, "identity has unknown field abi");

    auto root = V3Profile();
    root += "root_library = \"libgame.so\"\n";
    CheckRejected(root, "runtime has unknown field root_library");

    auto api22 = V3Profile();
    api22.replace(api22.find("api_level = 19"),
                  std::string_view{"api_level = 19"}.size(),
                  "api_level = 22");
    CheckRejected(api22, "runtime.api_level must be 19 for schema 3");
}

TEST_CASE("entry scope requires a real provisioned data fact") {
    auto profile = ScopedProfile();
    const auto manifest = profile.find("[data]");
    profile.erase(manifest);
    CheckRejected(profile,
                  "runtime entry scope requires a required data manifest fact");
}

TEST_CASE("entry and preset Java names are structural") {
    auto entry = ScopedProfile();
    entry.replace(entry.find("org.example.game.MainActivity"), 29U,
                  "org/example/game/MainActivity");
    CheckRejected(entry, "launch_activity is not a binary Java class name");

    auto field = ScopedProfile();
    field.replace(field.find("field = \"ready\""), 15U,
                  "field = \"bad.name\"");
    CheckRejected(field, "invalid Java class or field name");
}

TEST_CASE("preset value must match a supported primitive or String type") {
    auto mismatch = ScopedProfile();
    mismatch.replace(mismatch.find("value = true"), 12U, "value = 1");
    CheckRejected(mismatch, "runtime.presets[].value has wrong type");

    auto reference = ScopedProfile();
    reference.replace(reference.find("type = \"Z\""), 10U,
                      "type = \"Ljava/lang/Object;\"");
    CheckRejected(reference,
                  "type must be primitive or java.lang.String");

    auto empty_reason = ScopedProfile();
    empty_reason.replace(
        empty_reason.find("reason = \"fixture data is provisioned\""), 38U,
        "reason = \"\"");
    CheckRejected(empty_reason, "runtime.presets[].reason must not be empty");
}

TEST_CASE("Title Profile catalog matches only the exact v2 fingerprint") {
    auto profile = ogplay::session::LoadTitleProfileText(
        BaseProfile(), "org.example.game");
    const ogplay::session::TitleProfileCatalog catalog({std::move(profile)});
    CHECK(catalog.Match({"org.example.game", 7U, std::string(kHash)}) !=
          nullptr);
    CHECK(catalog.Match({"org.example.game", 8U, std::string(kHash)}) ==
          nullptr);
}

TEST_CASE("legacy exact lookup does not reinterpret schema v3") {
    auto profile = ogplay::session::LoadTitleProfileText(
        V3Profile(), "org.example.game");
    const ogplay::session::TitleProfileCatalog catalog({std::move(profile)});
    CHECK(catalog.Match({"org.example.game", 7U, std::string(kHash)}) ==
          nullptr);
}

TEST_CASE("Title Profile file loader enforces filename and UTF-8") {
    const auto directory = std::filesystem::temp_directory_path() /
                           "ogplay-profile-v2-loader-test";
    std::filesystem::create_directories(directory);
    const auto path = directory / "org.example.game.profile.toml";
    {
        std::ofstream output(path, std::ios::binary);
        output << BaseProfile();
    }
    CHECK(ogplay::session::LoadTitleProfile(path).schema == 2U);
    std::filesystem::remove_all(directory);
}
