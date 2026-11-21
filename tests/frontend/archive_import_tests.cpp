#include <doctest/doctest.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "ogplay/app/archive_import.h"
#include "zip_fixture.h"

namespace {
using namespace ogplay::app;
using ogplay::test::WriteZip;

class TemporaryDirectory final {
public:
    TemporaryDirectory() {
        static std::atomic<std::uint64_t> sequence{};
        path = std::filesystem::temp_directory_path() /
               ("ogplay-archive-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" +
                std::to_string(sequence.fetch_add(1)));
        std::filesystem::create_directories(path);
    }

    ~TemporaryDirectory() {
        std::error_code error;
        static_cast<void>(std::filesystem::remove_all(path, error));
    }

    std::filesystem::path path;
};

std::string Read(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), {}};
}

std::vector<ImportFile> Files(std::initializer_list<const char*> paths) {
    std::vector<ImportFile> files;
    for (const auto* path : paths)
        files.push_back({*NormalizeArchivePath(path), 1});
    return files;
}

std::string GuestOf(const ArchivePlan& plan, const std::vector<ImportFile>& files, std::string_view path) {
    for (const auto& item : plan.data)
        if (files[item.index].path == path) return item.guest_path;
    return "<not data>";
}
}  // namespace

TEST_CASE("archive plan maps pack layouts to sdcard paths") {
    SUBCASE("Android/obb tree, readme beside it, APK at the root") {
        const auto files = Files({"Android/ByFenCom.txt", "Android/obb/com.a.b/main.72.com.a.b.obb", "Game_v1.5.apk"});
        const auto plan = PlanArchive(files, {});
        REQUIRE(plan.apks.size() == 1);
        CHECK(files[plan.apks[0]].path == "Game_v1.5.apk");
        CHECK(plan.data.size() == 1);
        CHECK(GuestOf(plan, files, "Android/obb/com.a.b/main.72.com.a.b.obb") ==
              "/sdcard/Android/obb/com.a.b/main.72.com.a.b.obb");
        CHECK(plan.extras.empty());
        CHECK(ArchiveMount(plan, {}) == "/sdcard/Android/obb/com.a.b");
    }
    SUBCASE("package directory: expansion files make it OBB, a files/ tree makes it app data") {
        const auto files = Files({"com.a.b\\main.11.com.a.b.obb", "application.apk", "icon.png", "mainifest.dat",
                                  "com.c.d/files/Packs/a.zip", "picture/cover.png", "TitaniumBackup/x.tar.gz"});
        const auto plan = PlanArchive(files, {});
        CHECK(GuestOf(plan, files, "com.a.b/main.11.com.a.b.obb") == "/sdcard/Android/obb/com.a.b/main.11.com.a.b.obb");
        CHECK(GuestOf(plan, files, "com.c.d/files/Packs/a.zip") == "/sdcard/Android/data/com.c.d/files/Packs/a.zip");
        REQUIRE(plan.extras.size() == 2);
        CHECK(plan.extras[0].name == "picture");
        CHECK(plan.extras[1].name == "TitaniumBackup");
        CHECK(ArchiveMount(plan, {}) == "/sdcard/Android");
        const std::vector<std::string> chosen{"picture"};
        CHECK(ArchiveMount(plan, chosen) == "/sdcard");
        CHECK_THROWS_AS(static_cast<void>(ArchiveMount(plan, std::vector<std::string>{"nothing"})), LibraryError);
    }
    SUBCASE("a wrapper directory is looked through and modded builds come last") {
        const auto files = Files({"Pack/Horn_Mod.apk", "Pack/Horn.apk", "Pack/Android/data/com.a.b/files/x/y.dat",
                                  "Pack/main.3.com.a.b.obb"});
        const auto plan = PlanArchive(files, {});
        REQUIRE(plan.apks.size() == 2);
        CHECK(files[plan.apks[0]].path == "Pack/Horn.apk");
        CHECK(GuestOf(plan, files, "Pack/main.3.com.a.b.obb") == "/sdcard/Android/obb/com.a.b/main.3.com.a.b.obb");
        CHECK(ArchiveMount(plan, {}) == "/sdcard/Android");
    }
    SUBCASE("layout markers are the ones a picked directory is read by (GuessDataMount)") {
        const auto files = Files({"storage/emulated/0/Android/obb/com.a.b/main.2.com.a.b.obb", "x.apk"});
        CHECK(ArchiveMount(PlanArchive(files, {}), {}) == "/sdcard/Android/obb/com.a.b");
    }
    SUBCASE("a deep tree mounts at the app's own directory") {
        const auto files = Files({"Android/data/com.a.b/files/a/b.dat", "Android/data/com.a.b/files/a/c.dat", "x.apk"});
        CHECK(ArchiveMount(PlanArchive(files, {}), {}) == "/sdcard/Android/data/com.a.b");
    }
    SUBCASE("the APK's package counts as a package directory whatever its shape") {
        const auto files = Files({"Game/data.bin", "Other/data.bin", "game.apk"});
        CHECK(PlanArchive(files, "Game").data.size() == 1);
        CHECK(PlanArchive(files, {}).data.empty());
    }
}

TEST_CASE("archive plan keeps unrecognized wrappers and places early Gameloft games") {
    constexpr std::string_view r6 = "com.gameloft.android.ANMP.GloftR6HP";
    SUBCASE("a lone directory with nothing recognized below it is content, files are not directories") {
        const auto files = Files({"Title/.nomedia", "Title/data/a.bin", "Title/Save.bin"});
        const auto plan = PlanArchive(files, "com.a.b");
        CHECK(plan.data.empty());
        REQUIRE(plan.extras.size() == 1);
        CHECK(plan.extras[0].name == "Title");
        CHECK(plan.extras[0].directory);
        CHECK(plan.extras[0].members[2].guest_path == "/sdcard/Title/Save.bin");
        const auto loose = PlanArchive(Files({"Save.bin", "data/a.bin"}), "com.a.b");
        REQUIRE(loose.extras.size() == 2);
        CHECK_FALSE(loose.extras[0].directory);
        CHECK(loose.extras[1].directory);
    }
    SUBCASE("the <GameID> directory goes to gameloft/games, which is the mount") {
        const auto files = Files({"GloftR6HP/.nomedia", "GloftR6HP/data/3d.zip", "GloftR6HP/SaveGame.bin", "r6.apk"});
        const auto plan = PlanArchive(files, r6);
        CHECK(plan.extras.empty());
        CHECK(GuestOf(plan, files, "GloftR6HP/data/3d.zip") == "/sdcard/gameloft/games/GloftR6HP/data/3d.zip");
        CHECK(GuestOf(plan, files, "GloftR6HP/.nomedia") == "/sdcard/gameloft/games/GloftR6HP/.nomedia");
        CHECK(ArchiveMount(plan, {}) == "/sdcard/gameloft/games");
    }
    SUBCASE("the ID need not start with Gloft; wrappers and readmes around it are dropped") {
        const auto files = Files({"Dungeon Hunter HD/DungeonHunter/data/a.bin", "Dungeon Hunter HD/DungeonHunter/b.bin",
                                  "Dungeon Hunter HD/readme.txt"});
        const auto plan = PlanArchive(files, "com.gameloft.android.GAND.GloftDUNQ.DungeonHunter");
        CHECK(GuestOf(plan, files, "Dungeon Hunter HD/DungeonHunter/data/a.bin") ==
              "/sdcard/gameloft/games/DungeonHunter/data/a.bin");
        CHECK(plan.data.size() == 2);
        CHECK(ArchiveMount(plan, {}) == "/sdcard/gameloft/games");
        // A lone directory is the game whatever its name; several unmatched ones are not guessed.
        CHECK(ArchiveMount(PlanArchive(Files({"Title/data/a.bin", "Title/b.bin"}), r6), {}) ==
              "/sdcard/gameloft/games");
        const auto unsure = PlanArchive(Files({"Pack/Title/data/a.bin", "Pack/Title/b.bin"}), r6);
        CHECK(unsure.data.empty());
        REQUIRE(unsure.extras.size() == 1);
        CHECK(unsure.extras[0].name == "Pack");
    }
    SUBCASE("a gameloft/games tree is a layout marker for any package") {
        const auto files = Files({"Pack/GameLoft/Games/GloftR6HP/data/a.bin", "Pack/GameLoft/Games/GloftR6HP/b.bin"});
        const auto plan = PlanArchive(files, "com.a.b");
        CHECK(GuestOf(plan, files, "Pack/GameLoft/Games/GloftR6HP/b.bin") == "/sdcard/gameloft/games/GloftR6HP/b.bin");
        CHECK(ArchiveMount(plan, {}) == "/sdcard/gameloft/games");
        const auto sdcard = PlanArchive(Files({"sdcard/gameloft/games/GloftR6HP/b.bin"}), r6);
        CHECK(ArchiveMount(sdcard, {}) == "/sdcard/gameloft/games");
    }
    SUBCASE("Android data of a Gameloft game keeps the usual layout") {
        const auto files = Files({"Android/data/com.gameloft.x/files/a.bin", "Title/b.bin"});
        const auto plan = PlanArchive(files, "com.gameloft.x");
        CHECK(ArchiveMount(plan, {}) == "/sdcard/Android/data/com.gameloft.x");
        REQUIRE(plan.extras.size() == 1);
        CHECK(plan.extras[0].name == "Title");
    }
}

TEST_CASE("data directory plan places a picked early Gameloft game directory") {
    constexpr std::string_view r6 = "com.gameloft.android.ANMP.GloftR6HP";
    const auto guests = [](const ArchivePlan& plan) {
        std::vector<std::string> result;
        for (const auto& item : plan.data)
            result.push_back(item.guest_path);
        return result;
    };
    const auto own = PlanDataDirectory({{"data/a.bin", 1}, {".nomedia", 1}}, "/sdcard/gameloft/games/Title", r6);
    CHECK(guests(own.plan) ==
          std::vector<std::string>{"/sdcard/gameloft/games/Title/data/a.bin", "/sdcard/gameloft/games/Title/.nomedia"});
    CHECK(ArchiveMount(own.plan, {}) == "/sdcard/gameloft/games");
    const auto wrapped =
        PlanDataDirectory({{"GloftR6HP/data/a.bin", 1}, {"readme.txt", 1}}, "/sdcard/gameloft/games/R6 data", r6);
    CHECK(guests(wrapped.plan) == std::vector<std::string>{"/sdcard/gameloft/games/GloftR6HP/data/a.bin"});
    CHECK(ArchiveMount(wrapped.plan, {}) == "/sdcard/gameloft/games");
}

TEST_CASE("data directory plan keeps the package's own directories wherever the pick sits") {
    const auto guests = [](const ArchivePlan& plan) {
        std::vector<std::string> result;
        for (const auto& item : plan.data)
            result.push_back(item.guest_path);
        return result;
    };
    SUBCASE("an Android folder and the package folder inside it mount the same") {
        const std::vector<ImportFile> android{{"data/com.a.b/files/a.pak", 3}, {"readme.txt", 1}};
        const auto from_android = PlanDataDirectory(android, "/sdcard/Android", "com.a.b");
        CHECK(from_android.package == "com.a.b");
        CHECK(guests(from_android.plan) == std::vector<std::string>{"/sdcard/Android/data/com.a.b/files/a.pak"});
        CHECK(from_android.plan.extras.empty());
        CHECK(ArchiveMount(from_android.plan, {}) == "/sdcard/Android/data/com.a.b");
        const auto from_package = PlanDataDirectory({{"files/a.pak", 3}}, "/sdcard/Android/data/com.a.b", "com.a.b");
        CHECK(guests(from_package.plan) == guests(from_android.plan));
        CHECK(ArchiveMount(from_package.plan, {}) == "/sdcard/Android/data/com.a.b");
    }
    SUBCASE("data and obb share a mount; other packages and folders are extras") {
        const std::vector<ImportFile> files{{"android/Data/com.a.b/files/a", 1},
                                            {"Android/obb/com.a.b/main.1.com.a.b.obb", 2},
                                            {"Android/data/com.other/x", 4},
                                            {"gameloft/save", 8},
                                            {"game.apk", 16},
                                            {"notes.txt", 32}};
        const auto planned = PlanDataDirectory(files, "/sdcard", "com.a.b");
        CHECK(planned.package == "com.a.b");
        CHECK(guests(planned.plan) == std::vector<std::string>{"/sdcard/Android/data/com.a.b/files/a",
                                                               "/sdcard/Android/obb/com.a.b/main.1.com.a.b.obb"});
        CHECK(ArchiveMount(planned.plan, {}) == "/sdcard/Android");
        REQUIRE(planned.plan.extras.size() == 2);
        CHECK(planned.plan.extras[0].name == "Android/data/com.other");
        CHECK(planned.plan.extras[0].members[0].guest_path == "/sdcard/Android/data/com.other/x");
        CHECK(planned.plan.extras[1].name == "gameloft");
    }
    SUBCASE("a lone foreign package folder is taken; none falls back to the whole directory") {
        const auto renamed = PlanDataDirectory({{"data/com.original/f", 1}}, "/sdcard/Android", "com.mod");
        CHECK(renamed.package == "com.original");
        CHECK(ArchiveMount(renamed.plan, {}) == "/sdcard/Android/data/com.original");
        const auto plain = PlanDataDirectory({{"games/x/f", 1}, {"readme.txt", 1}}, "/sdcard/gameloft", "com.a.b");
        CHECK(plain.package.empty());
        CHECK(plain.plan.data.size() == 2);
        CHECK(ArchiveMount(plain.plan, {}) == "/sdcard/gameloft");
        const auto several =
            PlanDataDirectory({{"data/com.x/f", 1}, {"data/com.y/f", 1}}, "/sdcard/Android", "com.a.b");
        CHECK(several.package.empty());
        CHECK(several.plan.data.size() == 2);
    }
}

TEST_CASE("archive names: legacy GBK decodes, unsafe paths are refused") {
    CHECK(DecodeArchiveName("\xC4\xE3\xBA\xC3.apk") == "\xE4\xBD\xA0\xE5\xA5\xBD.apk");
    CHECK(DecodeArchiveName("\xE4\xBD\xA0.apk") == "\xE4\xBD\xA0.apk");
    CHECK(NormalizeArchivePath("./a//b\\c") == "a/b/c");
    CHECK_FALSE(NormalizeArchivePath("a/../b"));
    CHECK_FALSE(NormalizeArchivePath("C:/b"));
}

TEST_CASE("zip archives are told from APKs and extract into the planned layout") {
    TemporaryDirectory directory;
    const auto apk = directory.path / "game.apk";
    WriteZip(apk, {{"AndroidManifest.xml", "manifest"}, {"classes.dex", "dex"}});
    CHECK(DetectImportSource(apk) == ImportSourceKind::apk);

    const auto pack = directory.path / "pack.zip";
    WriteZip(pack, {{"\xD3\xCE\xCF\xB7.apk", "apk bytes"},
                    {"com.a.b/", ""},
                    {"com.a.b/main.5.com.a.b.obb", "expansion", true},
                    {"\xB4\xE6\xB5\xB5/save.sav", "save"},
                    {"readme.txt", "hello"}});
    CHECK(DetectImportSource(pack) == ImportSourceKind::archive);
    const auto archive = OpenArchive(pack);
    const auto& files = archive->Files();
    REQUIRE(files.size() == 4);
    CHECK(files[0].path == "\xE6\xB8\xB8\xE6\x88\x8F.apk");
    const auto plan = PlanArchive(files, "com.a.b");
    REQUIRE(plan.extras.size() == 1);
    CHECK(plan.extras[0].name == "\xE5\xAD\x98\xE6\xA1\xA3");

    ImportProgress progress;
    const auto data = directory.path / "data";
    CHECK(ExtractArchiveData(*archive, plan, {}, data, &progress) == "/sdcard/Android/obb/com.a.b");
    CHECK(Read(data / "main.5.com.a.b.obb") == "expansion");
    CHECK(progress.copied_bytes == 9);
    CHECK(progress.total_bytes == 9);

    const std::vector<std::string> extras{plan.extras[0].name};
    const auto with_extras = directory.path / "with-extras";
    CHECK(ExtractArchiveData(*archive, plan, extras, with_extras, nullptr) == "/sdcard");
    CHECK(Read(with_extras / "Android/obb/com.a.b/main.5.com.a.b.obb") == "expansion");
    CHECK(Read(with_extras / std::filesystem::path(u8"\u5B58\u6863/save.sav")) == "save");

    const auto apk_out = directory.path / "out.apk";
    const ImportExtraction extraction{plan.apks.at(0), apk_out};
    archive->Extract(std::span(&extraction, 1), nullptr);
    CHECK(Read(apk_out) == "apk bytes");

    ImportProgress cancelled;
    cancelled.cancel_requested = true;
    CHECK_THROWS_AS(static_cast<void>(ExtractArchiveData(*archive, plan, {}, directory.path / "cancelled", &cancelled)),
                    LibraryError);
}

TEST_CASE("encrypted zip members and escaping paths are refused") {
    TemporaryDirectory directory;
    const auto encrypted = directory.path / "locked.zip";
    WriteZip(encrypted, {{"a.apk", "x", false, 1}});
    CHECK_THROWS_AS(static_cast<void>(OpenArchive(encrypted)), LibraryError);
    const auto escaping = directory.path / "escape.zip";
    WriteZip(escaping, {{"../evil.apk", "x"}});
    CHECK_THROWS_AS(static_cast<void>(OpenArchive(escaping)), LibraryError);
    const auto control = directory.path / "control.zip";
    WriteZip(control, {{std::string("test.apk\0", 9), "x"}});
    CHECK_THROWS_AS(static_cast<void>(OpenArchive(control)), LibraryError);
}

TEST_CASE("archive ZIP Unicode Path overrides legacy names") {
    TemporaryDirectory directory;
    const auto pack = directory.path / "unicode.zip";
    WriteZip(pack, {{"legacy.apk", "unicode", false, 0, 0, "\xE6\xB8\xB8\xE6\x88\x8F.apk"},
                    {"\xE5\xAD\x98\xE6\xA1\xA3.txt", "utf-8", false, 0x800}});
    const auto archive = OpenArchive(pack);
    REQUIRE(archive->Files().size() == 2);
    CHECK(archive->Files()[0].path == "\xE6\xB8\xB8\xE6\x88\x8F.apk");
    CHECK(archive->Files()[1].path == "\xE5\xAD\x98\xE6\xA1\xA3.txt");
    const ImportExtraction extraction{0, directory.path / "out.apk"};
    archive->Extract(std::span(&extraction, 1), nullptr);
    CHECK(Read(extraction.destination) == "unicode");
}

TEST_CASE("archive 7z streams selected files from a solid LZMA2 and BCJ2 block") {
    const auto path = std::filesystem::path(OGPLAY_SOURCE_DIR) / "tests/fixtures/archive_import/solid.7z";
    CHECK(DetectImportSource(path) == ImportSourceKind::archive);
    auto archive = OpenArchive(path);
    const auto& files = archive->Files();
    REQUIRE(files.size() == 3);
    CHECK(files[2].path == "\xE5\xAD\x98\xE6\xA1\xA3.txt");
    TemporaryDirectory directory;
    // Select two files out of order and repeat the call with the last file alone.
    const std::vector<ImportExtraction> selections{{2, directory.path / "save"}, {0, directory.path / "data"}};
    ImportProgress progress;
    archive->Extract(selections, &progress);
    std::string expected;
    for (int i = 0; i < 257; ++i)
        expected.append("\xE8\x01\0\0\0\xE9\x02\0\0\0", 10);
    CHECK(Read(directory.path / "data") == expected);
    CHECK(Read(directory.path / "save") == "non-ASCII name\n");
    CHECK(progress.copied_bytes == 2585);
    const ImportExtraction apk{1, directory.path / "apk"};
    archive->Extract(std::span(&apk, 1), nullptr);
    CHECK(Read(apk.destination) == "fixture APK bytes");
}

TEST_CASE("archive 7z refuses encrypted entries and encrypted headers") {
    const auto fixtures = std::filesystem::path(OGPLAY_SOURCE_DIR) / "tests/fixtures/archive_import";
    for (const auto name : {"encrypted.7z", "encrypted-header.7z"})
        CHECK_THROWS_WITH_AS(static_cast<void>(OpenArchive(fixtures / name)), "暂不支持加密的压缩包，请先解压。",
                             LibraryError);
}
