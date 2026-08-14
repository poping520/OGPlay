// SharedPreferences XML (SBX-6, ADR-0020 design 03 §6). The format has to
// match what the platform writes, because some titles read shared_prefs
// files directly instead of going through the API.

#include <doctest/doctest.h>

#include <bit>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#include "ogplay/runtime/framework/preferences_xml.h"
#include "ogplay/runtime/vfs/vfs.h"

namespace {

using namespace ogplay::runtime;

[[nodiscard]] std::string ReadGuestFile(VirtualFileSystem& vfs,
                                        const std::string& path) {
    const auto info = vfs.Stat(path);
    const auto descriptor = vfs.Open(path, {.read = true});
    std::vector<std::byte> bytes(static_cast<std::size_t>(info.size));
    const auto count = vfs.Read(descriptor, bytes);
    vfs.Close(descriptor);
    std::string text;
    for (std::size_t index = 0; index < count; ++index) {
        text.push_back(static_cast<char>(bytes[index]));
    }
    return text;
}

}  // namespace

TEST_CASE("preferences XML round-trips every supported type") {
    PreferenceMap values;
    values["flag"] = true;
    values["off"] = false;
    values["count"] = std::int32_t{-7};
    values["big"] = std::int64_t{4294967296};
    values["ratio"] = 1.5F;
    values["name"] = std::string("Player One");

    const auto xml = RenderPreferencesXml(values);
    // Platform shape: attributes for scalars, element text for strings.
    CHECK(xml.find("<boolean name=\"flag\" value=\"true\" />") !=
          std::string::npos);
    CHECK(xml.find("<int name=\"count\" value=\"-7\" />") !=
          std::string::npos);
    CHECK(xml.find("<long name=\"big\" value=\"4294967296\" />") !=
          std::string::npos);
    CHECK(xml.find("<string name=\"name\">Player One</string>") !=
          std::string::npos);

    const auto parsed = ParsePreferencesXml(xml);
    CHECK(std::get<bool>(parsed.at("flag")));
    CHECK_FALSE(std::get<bool>(parsed.at("off")));
    CHECK(std::get<std::int32_t>(parsed.at("count")) == -7);
    CHECK(std::get<std::int64_t>(parsed.at("big")) == 4294967296);
    CHECK(std::get<float>(parsed.at("ratio")) == doctest::Approx(1.5));
    CHECK(std::get<std::string>(parsed.at("name")) == "Player One");
    // Rendering is stable, so a rewrite with no edits produces one file.
    CHECK(RenderPreferencesXml(parsed) == xml);
}

TEST_CASE("preferences XML preserves every float bit through rendering") {
    const std::vector<float> cases{
        0.123456789F,
        0.0F,
        -0.0F,
        std::numeric_limits<float>::min(),
        std::numeric_limits<float>::denorm_min(),
        std::numeric_limits<float>::max(),
    };
    for (const auto value : cases) {
        PreferenceMap values;
        values["precise"] = value;
        const auto parsed = ParsePreferencesXml(RenderPreferencesXml(values));
        CHECK(std::bit_cast<std::uint32_t>(
                  std::get<float>(parsed.at("precise"))) ==
              std::bit_cast<std::uint32_t>(value));
    }
}

TEST_CASE("preferences XML escapes and restores hostile text") {
    PreferenceMap values;
    values["k<&\">"] = std::string("a<b&c\"d'e>f");
    const auto xml = RenderPreferencesXml(values);
    CHECK(xml.find("a<b") == std::string::npos);  // really escaped
    const auto parsed = ParsePreferencesXml(xml);
    CHECK(std::get<std::string>(parsed.at("k<&\">")) == "a<b&c\"d'e>f");
}

TEST_CASE("preferences XML reads what the platform writes") {
    // Byte-for-byte shape of a real Android shared_prefs file.
    const std::string platform =
        "<?xml version='1.0' encoding='utf-8' standalone='yes' ?>\n"
        "<map>\n"
        "    <int name=\"launches\" value=\"3\" />\n"
        "    <string name=\"user\">tester</string>\n"
        "    <boolean name=\"first_run\" value=\"false\" />\n"
        "</map>\n";
    const auto parsed = ParsePreferencesXml(platform);
    CHECK(std::get<std::int32_t>(parsed.at("launches")) == 3);
    CHECK(std::get<std::string>(parsed.at("user")) == "tester");
    CHECK_FALSE(std::get<bool>(parsed.at("first_run")));
}

TEST_CASE("preferences XML refuses what it cannot represent") {
    const auto fails = [](const std::string& xml) {
        try {
            static_cast<void>(ParsePreferencesXml(xml));
        } catch (const PreferencesXmlError&) {
            return true;
        }
        return false;
    };
    // A set would be silently lost if it were skipped, so it is refused.
    CHECK(fails("<map><set name=\"k\"><string>a</string></set></map>"));
    CHECK(fails("<map><int name=\"k\" value=\"not-a-number\" /></map>"));
    CHECK(fails("<map><float name=\"k\" value=\"\" /></map>"));
    CHECK(fails("<map><float name=\"k\" value=\" 1.5\" /></map>"));
    CHECK(fails("<map><float name=\"k\" value=\"1.5x\" /></map>"));
    CHECK(fails("<map><float name=\"k\" value=\"1e1000\" /></map>"));
    CHECK(fails("<map><boolean name=\"k\" value=\"yes\" /></map>"));
    CHECK(fails("<map><int value=\"1\" /></map>"));
    CHECK(fails("<map><int name=\"k\" value=\"1\" extra=\"x\" /></map>"));
    CHECK(fails("<map><string name=\"k\">&unknown;</string></map>"));
    CHECK(fails("<!DOCTYPE map><map></map>"));
    CHECK(fails("<int name=\"k\" value=\"1\" />"));  // no map
    CHECK(fails("nonsense"));
}

TEST_CASE("preferences load and store go through the guest filesystem") {
    VirtualFileSystem vfs;
    const auto path = PreferencesGuestPath("com.example.game", "settings");
    CHECK(path == "/data/data/com.example.game/shared_prefs/settings.xml");

    // First run: no file yet is an empty map, not an error.
    CHECK(LoadPreferences(vfs, path).empty());

    PreferenceMap values;
    values["launches"] = std::int32_t{1};
    StorePreferences(vfs, path, values);

    // The file view and the API view are the same fact.
    const auto text = ReadGuestFile(vfs, path);
    CHECK(text.find("<int name=\"launches\" value=\"1\" />") !=
          std::string::npos);
    CHECK(std::get<std::int32_t>(LoadPreferences(vfs, path).at("launches")) ==
          1);
}

TEST_CASE("preferences load treats only ENOENT as first run") {
    VirtualFileSystem vfs;
    const std::vector<VfsLazyMountEntry> entries{{
        "settings.xml", 4,
        []() -> std::vector<std::byte> {
            throw std::runtime_error("backing store failed");
        }}};
    vfs.MountLazyReadOnly(VfsSource::apk, "/prefs", entries);

    CHECK_THROWS_WITH_AS(
        static_cast<void>(LoadPreferences(vfs, "/prefs/settings.xml")),
        doctest::Contains("cannot read /prefs/settings.xml"),
        PreferencesXmlError);
    CHECK(LoadPreferences(vfs, "/prefs/missing.xml").empty());
}
