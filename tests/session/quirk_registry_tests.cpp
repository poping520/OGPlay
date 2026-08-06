#include <doctest/doctest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

#include "ogplay/session/quirk_registry.h"
#include "ogplay/session/title_profile.h"

namespace {

constexpr std::string_view kHash =
    "0000000000000000000000000000000000000000000000000000000000000000";

class TempTree final {
public:
    TempTree() {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        root_ = std::filesystem::temp_directory_path() /
                ("ogplay-quirk-registry-" + std::to_string(stamp));
        std::filesystem::create_directories(root_);
    }

    ~TempTree() {
        std::error_code ignored;
        std::filesystem::remove_all(root_, ignored);
    }

    TempTree(const TempTree&) = delete;
    TempTree& operator=(const TempTree&) = delete;

    [[nodiscard]] const std::filesystem::path& Root() const noexcept { return root_; }

    [[nodiscard]] std::filesystem::path Write(const std::filesystem::path& relative,
                                              const std::string_view content) const {
        const auto path = root_ / relative;
        std::filesystem::create_directories(path.parent_path());
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        REQUIRE(output);
        output.write(content.data(), static_cast<std::streamsize>(content.size()));
        REQUIRE(output.good());
        return path;
    }

private:
    std::filesystem::path root_;
};

[[nodiscard]] std::string RegistryText(
    const std::string_view test_case = "legacy_reads required when disabled") {
    return "schema = 1\n"
           "\n"
           "[legacy_reads]\n"
           "summary = \"Allow one historical read behavior\"\n"
           "reason = \"\"\"\n"
           "The fixture deliberately requires this behavior.\n"
           "# remains reason text rather than a TOML comment.\n"
           "\"\"\"\n"
           "risk = \"May hide an invalid low-address read\"\n"
           "test = \"tests/quirk_required_tests.cpp:" +
           std::string(test_case) +
           "\"\n"
           "owner = \"runtime/memory\"\n";
}

[[nodiscard]] std::string ProfileText(
    const std::string_view quirk_id = "legacy_reads") {
    return "schema = 1\n"
           "[identity]\n"
           "package = \"org.example.legacy\"\n"
           "version_code = [1]\n"
           "so_sha256 = [\"" +
           std::string(kHash) +
           "\"]\n"
           "abi = \"armeabi-v7a\"\n"
           "[runtime]\n"
           "api_level = 19\n"
           "lifecycle = \"native_activity\"\n"
           "surface = { width = 1, height = 1 }\n"
           "[quirks]\n"
           "enabled = [\"" +
           std::string(quirk_id) +
           "\"]\n"
           "[quirks." +
           std::string(quirk_id) +
           "]\n"
           "range = [\"0x1000\", \"0x2000\"]\n";
}

[[nodiscard]] ogplay::session::QuirkRegistry BuildRegistry(TempTree& tree) {
    static_cast<void>(tree.Write(
        "tests/quirk_required_tests.cpp",
        "TEST_CASE(\"legacy_reads required when disabled\") {}"));
    const auto path = tree.Write("data/quirks.toml", RegistryText());
    return ogplay::session::QuirkRegistry::Load(path, tree.Root());
}

}  // namespace

TEST_CASE("C++ quirk registry loads complete definitions and multiline reasons") {
    TempTree tree;
    const auto registry = BuildRegistry(tree);
    REQUIRE(registry.Definitions().size() == 1);
    const auto* definition = registry.Find("legacy_reads");
    REQUIRE(definition != nullptr);
    CHECK(definition->owner == "runtime/memory");
    CHECK(definition->reason.find("# remains reason text") != std::string::npos);
    CHECK(registry.Find("missing") == nullptr);
}

TEST_CASE("C++ quirk registry rejects incomplete and stale test references") {
    TempTree tree;
    static_cast<void>(tree.Write(
        "tests/quirk_required_tests.cpp",
        "TEST_CASE(\"legacy_reads required when disabled\") {}"));
    auto incomplete = RegistryText();
    const auto risk = incomplete.find("risk = ");
    REQUIRE(risk != std::string::npos);
    incomplete.erase(risk, incomplete.find('\n', risk) - risk + 1);
    const auto registry_path = tree.Write("data/quirks.toml", incomplete);
    CHECK_THROWS_AS(
        static_cast<void>(
            ogplay::session::QuirkRegistry::Load(registry_path, tree.Root())),
        ogplay::session::QuirkRegistryError);

    static_cast<void>(
        tree.Write("data/quirks.toml", RegistryText("missing test case")));
    CHECK_THROWS_AS(
        static_cast<void>(
            ogplay::session::QuirkRegistry::Load(registry_path, tree.Root())),
        ogplay::session::QuirkRegistryError);

    auto invalid_utf8 = RegistryText();
    invalid_utf8.push_back(static_cast<char>(0xFF));
    static_cast<void>(tree.Write("data/quirks.toml", invalid_utf8));
    CHECK_THROWS_AS(
        static_cast<void>(
            ogplay::session::QuirkRegistry::Load(registry_path, tree.Root())),
        ogplay::session::QuirkRegistryError);
}

TEST_CASE("Title Profile catalogs require registered quirks before matching") {
    TempTree tree;
    const auto registry = BuildRegistry(tree);
    const auto profile = ogplay::session::LoadTitleProfileText(
        ProfileText(), "org.example.legacy");
    CHECK_NOTHROW(registry.Validate(profile));
    CHECK_THROWS_AS(
        static_cast<void>(ogplay::session::TitleProfileCatalog({profile})),
        ogplay::session::TitleProfileError);

    const ogplay::session::TitleProfileCatalog catalog({profile}, registry);
    CHECK(catalog.Match({"org.example.legacy", 1, std::string(kHash)}) != nullptr);

    const auto profiles = tree.Root() / "data" / "profiles";
    std::filesystem::create_directories(profiles);
    static_cast<void>(
        tree.Write("data/profiles/org.example.legacy.profile.toml", ProfileText()));
    const auto from_directory =
        ogplay::session::TitleProfileCatalog::LoadDirectory(profiles, registry);
    CHECK(from_directory.Profiles().size() == 1);

    auto incomplete = ogplay::session::LoadTitleProfileText(
        ProfileText(), "org.example.legacy");
    incomplete.quirks->parameters.clear();
    CHECK_THROWS_AS(registry.Validate(incomplete),
                    ogplay::session::QuirkRegistryError);
}

TEST_CASE("Title Profile catalogs reject unregistered quirk ids") {
    TempTree tree;
    static_cast<void>(tree.Write("data/quirks.toml", "schema = 1\n"));
    const auto registry = ogplay::session::QuirkRegistry::Load(
        tree.Root() / "data" / "quirks.toml", tree.Root());
    const auto profile = ogplay::session::LoadTitleProfileText(
        ProfileText(), "org.example.legacy");
    CHECK_THROWS_AS(registry.Validate(profile),
                    ogplay::session::QuirkRegistryError);
    CHECK_THROWS_AS(
        static_cast<void>(
            ogplay::session::TitleProfileCatalog({profile}, registry)),
        ogplay::session::QuirkRegistryError);

}

TEST_CASE("repository quirk registry is loadable without registered game quirks") {
    const auto root = std::filesystem::path(OGPLAY_SOURCE_DIR);
    const auto registry =
        ogplay::session::QuirkRegistry::Load(root / "data" / "quirks.toml", root);
    CHECK(registry.Definitions().empty());
}
