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
    for (const auto* path : paths) files.push_back({*NormalizeArchivePath(path), 1});
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
        CHECK(GuestOf(plan, files, "Android/obb/com.a.b/main.72.com.a.b.obb") == "/sdcard/Android/obb/com.a.b/main.72.com.a.b.obb");
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
}
