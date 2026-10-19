#include <doctest/doctest.h>
#include <filesystem>
#include <fstream>
#include <string>
#include "ogplay/session/title_profile.h"

namespace {
std::string Profile() {
    return R"(schema = 4
[identity]
package = "org.example.game"
version_code = [7]
so_sha256 = ["0000000000000000000000000000000000000000000000000000000000000000"]
)";
}
}

TEST_CASE("Title Profile v4 accepts only identity quirks and input") {
    using namespace ogplay::session;
    const auto profile = LoadTitleProfileText(Profile(), "org.example.game");
    CHECK(profile.schema == 4U);
    CHECK(profile.identity.version_codes == std::vector<std::uint32_t>{7});
    const TitleProfileCatalog catalog({profile});
    CHECK(catalog.Profiles().size() == 1);
    for (const auto schema : {1, 2, 3, 5}) {
        auto text = Profile();
        text.replace(9, 1, std::to_string(schema));
        CHECK_THROWS_AS(static_cast<void>(LoadTitleProfileText(text, "org.example.game")), TitleProfileError);
    }
    for (const auto removed : {"runtime", "data", "audio", "java"}) {
        CHECK_THROWS_AS(static_cast<void>(LoadTitleProfileText(Profile() + "[" + removed + "]\n",
                                            "org.example.game")), TitleProfileError);
    }
    for (const auto removed : {"abi", "name", "root_library"}) {
        CHECK_THROWS_AS(static_cast<void>(LoadTitleProfileText(Profile() + removed + " = \"unused\"\n",
                                            "org.example.game")), TitleProfileError);
    }
}

TEST_CASE("Title Profile v4 rejects invalid and overlapping identity scopes") {
    using namespace ogplay::session;
    CHECK_THROWS_AS(static_cast<void>(LoadTitleProfileText(Profile(), "org.example.other")), TitleProfileError);
    auto text = Profile();
    text.replace(text.find("[7]"), 3, "[7, 7]");
    CHECK_THROWS_AS(static_cast<void>(LoadTitleProfileText(text, "org.example.game")), TitleProfileError);
    text = Profile();
    text[text.find("0000")] = 'G';
    CHECK_THROWS_AS(static_cast<void>(LoadTitleProfileText(text, "org.example.game")), TitleProfileError);
    const auto profile = LoadTitleProfileText(Profile(), "org.example.game");
    CHECK_THROWS_AS(TitleProfileCatalog({profile, profile}), TitleProfileError);
    CHECK_THROWS_AS(static_cast<void>(LoadTitleProfileText(Profile() + std::string(200, '\n'),
                                        "org.example.game")), TitleProfileError);
    CHECK_THROWS_AS(static_cast<void>(LoadTitleProfileText(Profile() + std::string(1, char(0xff)),
                                        "org.example.game")), TitleProfileError);
}

TEST_CASE("Title Profile v4 file loader enforces filename") {
    const auto directory = std::filesystem::temp_directory_path() / "ogplay-profile-v4";
    std::filesystem::create_directories(directory);
    const auto path = directory / "org.example.game.profile.toml";
    { std::ofstream output(path, std::ios::binary); output << Profile(); }
    CHECK(ogplay::session::LoadTitleProfile(path).schema == 4U);
    std::filesystem::remove_all(directory);
}
