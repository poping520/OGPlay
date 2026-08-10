#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <variant>

#include "ogplay/session/profile_java.h"
#include "ogplay/session/title_profile.h"

namespace {

constexpr std::string_view kHashA =
    "0000000000000000000000000000000000000000000000000000000000000000";
constexpr std::string_view kHashB =
    "1111111111111111111111111111111111111111111111111111111111111111";

[[nodiscard]] std::string BaseProfile(
    const std::string_view package = "org.example.legacy",
    const std::string_view hash = kHashA,
    const std::string_view abi = "armeabi-v7a") {
    return "schema = 1\n"
           "\n"
           "[identity]\n"
           "package = \"" +
           std::string(package) +
           "\"\n"
           "name = \"Generic 旧版 Fixture\"\n"
           "version_code = [1, 2]\n"
           "so_sha256 = [\n"
           "  \"" +
           std::string(hash) +
           "\", # exact native library identity\n"
           "]\n"
           "abi = \"" +
           std::string(abi) +
           "\"\n"
           "\n"
           "[runtime]\n"
           "api_level = 19\n"
           "lifecycle = \"gl_surface_view\"\n"
           "surface = { width = 1280, height = 720 }\n";
}

[[nodiscard]] std::string CompleteProfile() {
    return BaseProfile() +
           "\n"
           "[data]\n"
           "mounts = [\n"
           "  { guest = \"/sdcard/game\", source = \"external\", required = true },\n"
           "  { guest = \"/data/local\", source = \"apk\", required = false },\n"
           "]\n"
           "working_directory = \"/sdcard/game\"\n"
           "manifest = [{ path = \"files/archive.dat\", required = true }]\n"
           "\n"
           "[audio]\n"
           "cover_music = { source = \"apk\", path = \"res/raw/music.ogg\", loop = true }\n"
           "sound_pool = { source = \"apk\", path_pattern = \"res/raw/fx_{resource:03}.ogg\" }\n"
           "\n"
           "[[java.class]]\n"
           "name = \"org/example/Legacy\"\n"
           "\n"
           "[[java.class.method]]\n"
           "name = \"load\"\n"
           "sig = \"(I)[B\"\n"
           "impl = \"resource.load\"\n"
           "static = false\n"
           "\n"
           "[quirks]\n"
           "enabled = [\"legacy_reads\"]\n"
           "\n"
           "[quirks.legacy_reads]\n"
           "range = [\"0x1000\", \"0x2000\"]\n"
           "ratio = 1.5\n"
           "strict = true\n"
           "\n"
           "[input]\n"
           "profile = \"generic_touch\"\n";
}

[[nodiscard]] std::string NativeCallProfile() {
    return BaseProfile() +
           "\n"
           "[[runtime.native_call]]\n"
           "phase = \"startup\"\n"
           "class = \"org/example/Renderer\"\n"
           "method = \"nativeInit\"\n"
           "signature = \"(II)V\"\n"
           "dispatch = \"instance\"\n"
           "arguments = [{ source = \"surface_width\" }, "
           "{ source = \"constant\", value = 7 }]\n"
           "\n"
           "[[runtime.native_call]]\n"
           "phase = \"pointer_down\"\n"
           "class = \"org/example/Input\"\n"
           "method = \"nativeTouch\"\n"
           "signature = \"(III)V\"\n"
           "dispatch = \"static\"\n"
           "arguments = [{ source = \"input_x\" }, { source = \"input_y\" }, "
           "{ source = \"input_pointer\" }]\n";
}

class TempDirectory final {
public:
    TempDirectory() {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = std::filesystem::temp_directory_path() /
                ("ogplay-title-profile-" + std::to_string(stamp));
        std::filesystem::create_directories(path_);
    }

    ~TempDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }

    TempDirectory(const TempDirectory&) = delete;
    TempDirectory& operator=(const TempDirectory&) = delete;

    [[nodiscard]] const std::filesystem::path& Path() const noexcept { return path_; }

    void Write(const std::string_view filename, const std::string_view content) const {
        std::ofstream output(path_ / filename, std::ios::binary | std::ios::trunc);
        REQUIRE(output);
        output.write(content.data(), static_cast<std::streamsize>(content.size()));
        REQUIRE(output.good());
    }

private:
    std::filesystem::path path_;
};

}  // namespace

TEST_CASE("Title Profile C++ loader decodes strict identity and runtime") {
    const auto profile =
        ogplay::session::LoadTitleProfileText(BaseProfile(), "org.example.legacy");

    CHECK(profile.schema == 1);
    CHECK(profile.identity.package == "org.example.legacy");
    CHECK(profile.identity.name == "Generic 旧版 Fixture");
    CHECK(profile.identity.version_codes == std::vector<std::uint32_t>{1, 2});
    CHECK(profile.identity.so_sha256 == std::vector<std::string>{std::string(kHashA)});
    CHECK(profile.identity.abi == ogplay::session::ProfileAbi::armeabi_v7a);
    CHECK(ogplay::session::ToString(profile.identity.abi) == "armeabi-v7a");
    const auto legacy = ogplay::session::LoadTitleProfileText(
        BaseProfile("org.example.legacy", kHashA, "armeabi"),
        "org.example.legacy");
    CHECK(legacy.identity.abi == ogplay::session::ProfileAbi::armeabi);
    CHECK(ogplay::session::ToString(legacy.identity.abi) == "armeabi");
    CHECK(profile.runtime.api_level == 19);
    CHECK(profile.runtime.lifecycle == ogplay::session::ProfileLifecycle::gl_surface_view);
    CHECK(profile.runtime.surface.width == 1280);
    CHECK(profile.runtime.surface.height == 720);
    CHECK(ogplay::session::ToString(profile.runtime.lifecycle) == "gl_surface_view");
}

TEST_CASE("Dungeon Hunter Profile declares its DEX activity callbacks") {
    const auto path = std::filesystem::path{OGPLAY_SOURCE_DIR} /
                      "data/profiles/com.gameloft.android.GAND.GloftDUNQ."
                      "DungeonHunter.profile.toml";
    const auto profile = ogplay::session::LoadTitleProfile(path);
    REQUIRE(profile.java_classes.size() == 1U);
    const auto& java_class = profile.java_classes.front();
    CHECK(java_class.name ==
          "com/gameloft/android/GAND/GloftDUNQ/DungeonHunter/DungeonHunter");
    REQUIRE(java_class.methods.size() == 15U);
    CHECK(java_class.methods[0].name == "sendAppToBackground");
    CHECK(java_class.methods[0].signature == "()V");
    CHECK(java_class.methods[0].implementation ==
          "activity.send_to_background");
    CHECK(java_class.methods[0].is_static);
    CHECK(java_class.methods[1].name == "Exit");
    CHECK(java_class.methods[1].signature == "()V");
    CHECK(java_class.methods[1].implementation == "process.exit");
    CHECK(java_class.methods[1].is_static);
    CHECK(java_class.methods[2].name == "unlockDemo");
    CHECK(java_class.methods[2].signature == "()V");
    CHECK(java_class.methods[3].name == "GetPlayMode");
    CHECK(java_class.methods[3].signature == "()I");
    CHECK(java_class.methods[4].name == "LaunchBilling");
    CHECK(java_class.methods[6].name == "GetNumbOfLaunch");
    CHECK(java_class.methods[7].name == "OpenGLive");
    CHECK(java_class.methods[7].signature == "(I)V");
    CHECK(java_class.methods[8].name == "NotifyTrophy");
    CHECK(java_class.methods[8].signature == "(I)I");
    CHECK(java_class.methods[11].name == "GetDoubleOptionText1");
    CHECK(java_class.methods[11].signature == "()[B");
    CHECK(java_class.methods[14].name == "TrackingRegisterFirstRun");
    CHECK(java_class.methods[14].implementation ==
          "analytics.track_first_run");
    CHECK(std::ranges::all_of(java_class.methods, [](const auto& method) {
        return method.is_static;
    }));
}

TEST_CASE("Title Profile C++ loader rejects schema and TOML violations") {
    auto mismatched = BaseProfile();
    CHECK_THROWS_AS(
        static_cast<void>(
            ogplay::session::LoadTitleProfileText(mismatched, "org.example.other")),
        ogplay::session::TitleProfileError);

    auto unknown = BaseProfile();
    unknown.insert(unknown.find('\n') + 1, "script = \"run\"\n");
    CHECK_THROWS_AS(static_cast<void>(ogplay::session::LoadTitleProfileText(
                        unknown, "org.example.legacy")),
                    ogplay::session::TitleProfileError);

    auto duplicate = BaseProfile();
    duplicate.replace(duplicate.find("api_level = 19"), std::string("api_level = 19").size(),
                      "api_level = 19\napi_level = 22");
    CHECK_THROWS_AS(static_cast<void>(ogplay::session::LoadTitleProfileText(
                        duplicate, "org.example.legacy")),
                    ogplay::session::TitleProfileError);

    auto bad_hash = BaseProfile();
    bad_hash.replace(bad_hash.find(kHashA), kHashA.size(), "ABC");
    CHECK_THROWS_AS(static_cast<void>(ogplay::session::LoadTitleProfileText(
                        bad_hash, "org.example.legacy")),
                    ogplay::session::TitleProfileError);

    auto bad_abi = BaseProfile("org.example.legacy", kHashA, "x86");
    CHECK_THROWS_WITH(static_cast<void>(ogplay::session::LoadTitleProfileText(
                          bad_abi, "org.example.legacy")),
                      "identity.abi must be armeabi or armeabi-v7a");

    auto bad_api = BaseProfile();
    bad_api.replace(bad_api.find("api_level = 19"),
                    std::string("api_level = 19").size(), "api_level = 21");
    CHECK_THROWS_AS(static_cast<void>(ogplay::session::LoadTitleProfileText(
                        bad_api, "org.example.legacy")),
                    ogplay::session::TitleProfileError);

    auto bad_lifecycle = BaseProfile();
    bad_lifecycle.replace(bad_lifecycle.find("gl_surface_view"),
                          std::string("gl_surface_view").size(), "per_game_loop");
    CHECK_THROWS_AS(static_cast<void>(ogplay::session::LoadTitleProfileText(
                        bad_lifecycle, "org.example.legacy")),
                    ogplay::session::TitleProfileError);

    auto invalid_utf8 = BaseProfile();
    invalid_utf8.push_back(static_cast<char>(0xFF));
    CHECK_THROWS_AS(static_cast<void>(ogplay::session::LoadTitleProfileText(
                        invalid_utf8, "org.example.legacy")),
                    ogplay::session::TitleProfileError);
}

TEST_CASE("Title Profile native calls remain typed declarative lifecycle data") {
    const auto profile = ogplay::session::LoadTitleProfileText(
        NativeCallProfile(), "org.example.legacy");
    REQUIRE(profile.runtime.native_calls.size() == 2);
    const auto& startup = profile.runtime.native_calls[0];
    CHECK(startup.phase == ogplay::session::ProfileNativeCallPhase::startup);
    CHECK(startup.class_name == "org/example/Renderer");
    CHECK(startup.method == "nativeInit");
    CHECK(startup.signature == "(II)V");
    CHECK(startup.dispatch == ogplay::session::ProfileNativeDispatch::instance);
    REQUIRE(startup.arguments.size() == 2);
    CHECK(startup.arguments[0].source ==
          ogplay::session::ProfileNativeArgumentSource::surface_width);
    CHECK(startup.arguments[1].value == 7);
    CHECK(ogplay::session::ToString(profile.runtime.native_calls[1].phase) ==
          "pointer_down");
    CHECK(ogplay::session::ToString(profile.runtime.native_calls[1].dispatch) ==
          "static");
    CHECK(ogplay::session::ToString(
              profile.runtime.native_calls[1].arguments[2].source) ==
          "input_pointer");

    auto wrong_count = NativeCallProfile();
    const auto signature = wrong_count.find("signature = \"(II)V\"");
    REQUIRE(signature != std::string::npos);
    wrong_count.replace(signature, std::string("signature = \"(II)V\"").size(),
                        "signature = \"(I)V\"");
    CHECK_THROWS_AS(static_cast<void>(ogplay::session::LoadTitleProfileText(
                        wrong_count, "org.example.legacy")),
                    ogplay::session::TitleProfileError);

    auto wrong_phase = NativeCallProfile();
    const auto pointer = wrong_phase.find("phase = \"pointer_down\"");
    REQUIRE(pointer != std::string::npos);
    wrong_phase.replace(pointer, std::string("phase = \"pointer_down\"").size(),
                        "phase = \"frame\"");
    CHECK_THROWS_AS(static_cast<void>(ogplay::session::LoadTitleProfileText(
                        wrong_phase, "org.example.legacy")),
                    ogplay::session::TitleProfileError);
}

TEST_CASE("Title Profile file loader enforces filename and line bounds") {
    TempDirectory directory;
    directory.Write("org.example.legacy.profile.toml", BaseProfile());
    CHECK(ogplay::session::LoadTitleProfile(
              directory.Path() / "org.example.legacy.profile.toml")
              .identity.package == "org.example.legacy");

    directory.Write("wrong.profile.toml", BaseProfile());
    CHECK_THROWS_AS(static_cast<void>(ogplay::session::LoadTitleProfile(
                        directory.Path() / "wrong.profile.toml")),
                    ogplay::session::TitleProfileError);

    std::string oversized = BaseProfile();
    oversized.append(201, '\n');
    directory.Write("org.example.legacy.profile.toml", oversized);
    CHECK_THROWS_AS(static_cast<void>(ogplay::session::LoadTitleProfile(
                        directory.Path() / "org.example.legacy.profile.toml")),
                    ogplay::session::TitleProfileError);
}

TEST_CASE("Title Profile data and audio stay declarative and normalized") {
    const auto profile =
        ogplay::session::LoadTitleProfileText(CompleteProfile(), "org.example.legacy");
    REQUIRE(profile.data.has_value());
    REQUIRE(profile.data->mounts.size() == 2);
    CHECK(profile.data->mounts[0].guest == "/sdcard/game");
    CHECK(profile.data->mounts[0].source == ogplay::session::ProfileSource::external);
    CHECK(profile.data->mounts[0].required);
    CHECK(profile.data->mounts[1].source == ogplay::session::ProfileSource::apk);
    CHECK(profile.data->working_directory == "/sdcard/game");
    REQUIRE(profile.data->manifest.size() == 1);
    CHECK(profile.data->manifest[0].path == "files/archive.dat");
    REQUIRE(profile.audio.has_value());
    REQUIRE(profile.audio->cover_music.has_value());
    CHECK(profile.audio->cover_music->path == "res/raw/music.ogg");
    CHECK(profile.audio->cover_music->loop);
    REQUIRE(profile.audio->sound_pool.has_value());
    CHECK(profile.audio->sound_pool->source ==
          ogplay::session::ProfileSource::apk);
    CHECK(profile.audio->sound_pool->path_pattern ==
          "res/raw/fx_{resource:03}.ogg");

    auto escaping = CompleteProfile();
    escaping.replace(escaping.find("/sdcard/game"), std::string("/sdcard/game").size(),
                     "/sdcard/../game");
    CHECK_THROWS_AS(static_cast<void>(ogplay::session::LoadTitleProfileText(
                        escaping, "org.example.legacy")),
                    ogplay::session::TitleProfileError);

    auto bad_source = CompleteProfile();
    bad_source.replace(bad_source.find("source = \"external\""),
                       std::string("source = \"external\"").size(),
                       "source = \"host_path\"");
    CHECK_THROWS_AS(static_cast<void>(ogplay::session::LoadTitleProfileText(
                        bad_source, "org.example.legacy")),
                    ogplay::session::TitleProfileError);

    auto bad_type = CompleteProfile();
    bad_type.replace(bad_type.find("required = true"),
                     std::string("required = true").size(), "required = \"yes\"");
    CHECK_THROWS_AS(static_cast<void>(ogplay::session::LoadTitleProfileText(
                        bad_type, "org.example.legacy")),
                    ogplay::session::TitleProfileError);

    auto bad_pattern = CompleteProfile();
    bad_pattern.replace(bad_pattern.find("{resource:03}"),
                        std::string("{resource:03}").size(),
                        "{resource:00}");
    CHECK_THROWS_WITH_AS(
        static_cast<void>(ogplay::session::LoadTitleProfileText(
            bad_pattern, "org.example.legacy")),
        "audio.sound_pool.path_pattern resource placeholder is invalid",
        ogplay::session::TitleProfileError);
}

TEST_CASE("Title Profile Java quirks and input decode without executable content") {
    const auto profile =
        ogplay::session::LoadTitleProfileText(CompleteProfile(), "org.example.legacy");
    REQUIRE(profile.java_classes.size() == 1);
    CHECK(profile.java_classes[0].name == "org/example/Legacy");
    REQUIRE(profile.java_classes[0].methods.size() == 1);
    CHECK(profile.java_classes[0].methods[0].signature == "(I)[B");
    CHECK(profile.java_classes[0].methods[0].implementation == "resource.load");
    CHECK_FALSE(profile.java_classes[0].methods[0].is_static);
    auto static_text = CompleteProfile();
    static_text.replace(static_text.find("static = false"),
                        std::string("static = false").size(),
                        "static = true");
    const auto static_profile = ogplay::session::LoadTitleProfileText(
        static_text, "org.example.legacy");
    const std::array implementations{
        ogplay::session::ProfileJavaImplementation{
            "resource.load",
            [](const ogplay::runtime::JniInvocation&) {
                return ogplay::runtime::JniValue{
                    ogplay::runtime::JniReference{1}};
            }}};
    const auto assembly = ogplay::session::AssembleProfileJava(
        static_profile, implementations);
    REQUIRE(assembly.bindings.size() == 1);
    CHECK(assembly.classes->GetMethodId(
              assembly.bindings[0].class_identity, "load", "(I)[B", true)
              .has_value());
    CHECK_FALSE(assembly.classes->GetMethodId(
        assembly.bindings[0].class_identity, "load", "(I)[B", false));
    REQUIRE(profile.quirks.has_value());
    CHECK(profile.quirks->enabled == std::vector<std::string>{"legacy_reads"});
    const auto& parameters = profile.quirks->parameters.at("legacy_reads");
    CHECK(std::get<bool>(parameters.at("strict").value));
    const auto& range =
        std::get<ogplay::session::ProfileValue::Array>(parameters.at("range").value);
    CHECK(std::get<std::string>(range[0].value) == "0x1000");
    CHECK(std::get<double>(parameters.at("ratio").value) == doctest::Approx(1.5));
    REQUIRE(profile.input.has_value());
    CHECK(profile.input->profile == "generic_touch");

    auto disabled = CompleteProfile();
    disabled.replace(disabled.find("enabled = [\"legacy_reads\"]"),
                     std::string("enabled = [\"legacy_reads\"]").size(), "enabled = []");
    CHECK_THROWS_AS(static_cast<void>(ogplay::session::LoadTitleProfileText(
                        disabled, "org.example.legacy")),
                    ogplay::session::TitleProfileError);

    auto duplicate_method = CompleteProfile();
    const auto quirks = duplicate_method.find("\n[quirks]\n");
    REQUIRE(quirks != std::string::npos);
    duplicate_method.insert(
        quirks,
        "\n[[java.class.method]]\n"
        "name = \"load\"\n"
        "sig = \"(I)[B\"\n"
        "impl = \"resource.load\"\n");
    CHECK_THROWS_AS(static_cast<void>(ogplay::session::LoadTitleProfileText(
                        duplicate_method, "org.example.legacy")),
                    ogplay::session::TitleProfileError);

    auto missing_parameters = CompleteProfile();
    const auto parameter_begin = missing_parameters.find("[quirks.legacy_reads]");
    const auto input_begin = missing_parameters.find("[input]");
    REQUIRE(parameter_begin != std::string::npos);
    REQUIRE(input_begin != std::string::npos);
    missing_parameters.erase(parameter_begin, input_begin - parameter_begin);
    CHECK_THROWS_AS(static_cast<void>(ogplay::session::LoadTitleProfileText(
                        missing_parameters, "org.example.legacy")),
                    ogplay::session::TitleProfileError);

    auto invalid_input = CompleteProfile();
    invalid_input.replace(invalid_input.find("generic_touch"),
                          std::string("generic_touch").size(), "GameSpecific");
    CHECK_THROWS_AS(static_cast<void>(ogplay::session::LoadTitleProfileText(
                        invalid_input, "org.example.legacy")),
                    ogplay::session::TitleProfileError);

    auto invalid_float = CompleteProfile();
    invalid_float.replace(invalid_float.find("ratio = 1.5"),
                          std::string("ratio = 1.5").size(),
                          "ratio = 1.5suffix");
    CHECK_THROWS_AS(static_cast<void>(ogplay::session::LoadTitleProfileText(
                        invalid_float, "org.example.legacy")),
                    ogplay::session::TitleProfileError);
}

TEST_CASE("Title Profile catalog matches only the complete exact fingerprint") {
    auto first =
        ogplay::session::LoadTitleProfileText(BaseProfile(), "org.example.legacy");
    auto second = ogplay::session::LoadTitleProfileText(
        BaseProfile("net.example.other", kHashB), "net.example.other");
    ogplay::session::TitleProfileCatalog catalog({first, second});

    const auto* match = catalog.Match({"org.example.legacy", 2, std::string(kHashA)});
    REQUIRE(match != nullptr);
    CHECK(match->identity.package == "org.example.legacy");
    CHECK(catalog.Match({"org.example.legacy", 3, std::string(kHashA)}) == nullptr);
    CHECK(catalog.Match({"org.example.legacy", 2, std::string(kHashB)}) == nullptr);
    CHECK(catalog.Match({"net.example.other", 2, std::string(kHashA)}) == nullptr);
    CHECK_THROWS_AS(
        static_cast<void>(catalog.Match({"org.example.legacy", 0, std::string(kHashA)})),
        ogplay::session::TitleProfileError);
    CHECK_THROWS_AS(
        static_cast<void>(catalog.Match({"org.example.legacy", 2, "not-a-hash"})),
        ogplay::session::TitleProfileError);
    CHECK_THROWS_AS(
        static_cast<void>(
            ogplay::session::TitleProfileCatalog({first, first})),
        ogplay::session::TitleProfileError);
    auto invalid_abi = first;
    invalid_abi.identity.abi = static_cast<ogplay::session::ProfileAbi>(0xffU);
    CHECK_THROWS_AS(
        static_cast<void>(ogplay::session::TitleProfileCatalog({invalid_abi})),
        ogplay::session::TitleProfileError);
    first.identity.version_codes.clear();
    CHECK_THROWS_AS(
        static_cast<void>(ogplay::session::TitleProfileCatalog({first})),
        ogplay::session::TitleProfileError);
}

TEST_CASE("Title Profile directory catalog is deterministic and ignores non-profiles") {
    TempDirectory directory;
    directory.Write("org.example.legacy.profile.toml", BaseProfile());
    directory.Write("net.example.other.profile.toml",
                    BaseProfile("net.example.other", kHashB));
    directory.Write("README.md", "not a profile");

    const auto catalog =
        ogplay::session::TitleProfileCatalog::LoadDirectory(directory.Path());
    REQUIRE(catalog.Profiles().size() == 2);
    CHECK(catalog.Profiles()[0].identity.package == "net.example.other");
    CHECK(catalog.Profiles()[1].identity.package == "org.example.legacy");
    CHECK(catalog.Match({"net.example.other", 1, std::string(kHashB)}) != nullptr);
}
