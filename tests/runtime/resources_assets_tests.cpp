#include "resources_test_support.h"
#include "frontend/game/run_game_vfs.h"
#include "ogplay/core/json.h"
#include <doctest/doctest.h>
#include <algorithm>
#include <set>
using namespace ogplay::test::resources;

TEST_CASE("resources core assets stream stored and deflated entries with bounded seek and FD rules") {
    res::AssetManager am(Framework());
    std::array<std::byte, 17> buffer{};
    auto count = res::Asset::globalCount();
    for (auto method : {0, 8}) {
        const auto& entries = Framework()->archive().entries;
        auto e = std::find_if(entries.begin(), entries.end(), [&](const ogplay::loader::ApkEntry& e) {
            return e.compression_method == method && e.uncompressed_size > 100 && e.name != "resources.arsc";
        });
        REQUIRE(e != entries.end());
        auto expected = ogplay::loader::ReadApkEntry(Framework()->source().bytes, Framework()->archive(), e->name);
        auto asset = am.openNonAsset(e->name, 1);
        REQUIRE(asset);
        CHECK(res::Asset::globalCount() == count + 1);
        CHECK(asset->cookie() == 1);
        CHECK(asset->length() == expected.size());
        CHECK(asset->read(buffer) == buffer.size());
        CHECK(std::equal(buffer.begin(), buffer.end(), expected.begin()));
        CHECK(asset->read(buffer) == buffer.size());
        CHECK(std::equal(buffer.begin(), buffer.end(), expected.begin() + 17));
        CHECK(asset->seek(3, 0) == 3);
        CHECK(asset->read(buffer) == buffer.size());
        CHECK(std::equal(buffer.begin(), buffer.end(), expected.begin() + 3));
        CHECK(asset->seek(-5, 2) == expected.size() - 5);
        CHECK(asset->remaining() == 5);
        CHECK(asset->read(buffer) == 5);
        CHECK(asset->read(buffer) == 0);
        CHECK_THROWS_AS(static_cast<void>(asset->seek(1, 2)), res::ResourceError);
        CHECK_THROWS_AS(static_cast<void>(asset->seek(-1, 0)), res::ResourceError);
        CHECK_THROWS_AS(static_cast<void>(asset->seek(INT64_MAX, 1)), res::ResourceError);
        if (!method) {
            auto fd = asset->openFd();
            CHECK(fd.length == expected.size());
            CHECK(std::equal(expected.begin(), expected.end(),
                             fd.source.bytes.begin() + static_cast<std::ptrdiff_t>(fd.offset)));
            CHECK(asset->storedBuffer().data() == fd.source.bytes.data() + fd.offset);
        } else {
            CHECK_THROWS_AS(static_cast<void>(asset->openFd()), res::ResourceError);
            CHECK_THROWS_AS(static_cast<void>(asset->storedBuffer()), res::ResourceError);
        }
    }
    CHECK(res::Asset::globalCount() == count);
}

TEST_CASE("resources core assets merge cookie directories and search newest first") {
    auto f = Framework();
    auto arsc = ogplay::loader::ReadApkEntry(f->source().bytes, f->archive(), "resources.arsc");
    std::vector<std::pair<std::string, std::vector<std::byte>>> files = {{"resources.arsc", arsc},
                                                                         {"assets/shared.txt", {std::byte{'f'}}},
                                                                         {"assets/base.txt", {std::byte{'z'}}},
                                                                         {"assets/sub/file", {std::byte{'x'}}}};
    auto framework = std::make_shared<res::AssetPath>(OwnedSource(Zip(files), "framework"));
    res::AssetManager am(framework);
    auto source = AppApk();
    auto up = std::make_shared<bool>(true);
    source.isUpToDate = [up] { return *up; };
    CHECK(am.addAssetPath(source) == 2);
    CHECK(am.addAssetPath(source) == 2);
    CHECK(am.getCookieName(1) == "framework");
    CHECK(am.getCookieName(2) == "fixture.apk");
    CHECK(am.list("") == std::vector<std::string>{"app.txt", "base.txt", "shared.txt", "sub"});
    CHECK(am.list("sub") == std::vector<std::string>{"file"});
    std::array<std::byte, 1> b;
    auto app = am.open("shared.txt");
    REQUIRE(app);
    CHECK(app->read(b) == 1);
    CHECK(b[0] == std::byte{'a'});
    auto base = am.openNonAsset("assets/shared.txt", 1);
    REQUIRE(base);
    CHECK(base->read(b) == 1);
    CHECK(b[0] == std::byte{'f'});
    CHECK_FALSE(am.open("../resources.arsc"));
    CHECK_FALSE(am.openNonAsset("assets/missing"));
    CHECK_FALSE(am.openNonAsset("assets/base.txt", 3));
    CHECK(am.isUpToDate());
    *up = false;
    CHECK_FALSE(am.isUpToDate());
    CHECK_THROWS_AS(static_cast<void>(am.getCookieName(0)), res::ResourceError);
}

TEST_CASE("resources core framework mapping is initialized once and shared across sessions") {
    auto framework = Framework();
    int called = 0;
    auto again = res::sharedFramework([&]() -> res::Source {
        ++called;
        throw std::runtime_error("provider must not run twice");
    });
    CHECK(called == 0);
    CHECK(again == framework);
    res::AssetManager a(framework), b(framework);
    CHECK(&a.resources().strings(1) == &b.resources().strings(1));
    auto c = Config();
    c.sdkVersion = 12;
    a.resources().setParameters(c);
    CHECK(b.resources().parameters().sdkVersion == 0);
    a.setConfiguration(Config(), "zh_CN");
    CHECK(a.resources().parameters().localeName() == "zh_CN");
    a.setConfiguration(Config(1280, 720, 320));
    CHECK(a.resources().parameters().localeName() == "zh_CN");
    a.setConfiguration(Config(), "");
    CHECK(a.resources().parameters().localeName().empty());
}

TEST_CASE("resources core payload archives are visible through read only guest VFS") {
    ogplay::runtime::VirtualFileSystem vfs;
    ogplay::frontend::MountApi19SystemPayload(vfs, std::filesystem::path(OGPLAY_SOURCE_DIR) / "data/android/19");
    for (auto name : {"bootdex.jar", "framework-res.apk"}) {
        auto path = std::string("/system/framework/") + name;
        auto info = vfs.Stat(path);
        CHECK_FALSE(info.writable);
        CHECK(info.size > 0);
        CHECK_THROWS_AS(static_cast<void>(vfs.Open(path, {.write = true})), ogplay::runtime::VfsError);
        auto fd = vfs.Open(path, {.read = true});
        std::array<std::byte, 4> magic{};
        CHECK(vfs.Read(fd, magic) == 4);
        CHECK(magic[0] == std::byte{'P'});
        CHECK(magic[1] == std::byte{'K'});
        vfs.Close(fd);
    }
}

TEST_CASE("resources core API19 system payload exposes read-only guest data and shared fonts") {
    using namespace ogplay;
    const auto root = std::filesystem::path(OGPLAY_SOURCE_DIR) / "data/android/19";
    const auto manifest = ReadSource(root / "manifest.json");
    core::JsonParseError error;
    const auto document = core::JsonDocument::ParseStrict(
        {reinterpret_cast<const char*>(manifest.bytes.data()), manifest.bytes.size()}, error);
    REQUIRE(document);

    runtime::VirtualFileSystem vfs;
    const auto font_source = frontend::MountApi19SystemPayload(vfs, root);
    CHECK(vfs.IoStatistics().backing_read_bytes == 0);
    const auto expect_read_only = [](auto action) {
        try {
            action();
            FAIL("expected guest EROFS");
        } catch (const runtime::VfsError& failure) {
            CHECK(failure.ErrorNumber() == 30);  // API19 EROFS
        }
    };
    const auto check_file = [&](const std::filesystem::path& relative, const std::string& guest) {
        INFO(guest);
        const auto expected = ReadSource(root / relative);
        const auto info = vfs.Stat(guest);
        CHECK_FALSE(info.is_directory);
        CHECK_FALSE(info.writable);
        CHECK(info.size == expected.bytes.size());
        const auto fd = vfs.Open(guest, {.read = true, .require_positioned_read = true});
        std::array<std::byte, 32> bytes{};
        REQUIRE(expected.bytes.size() >= bytes.size());
        CHECK(vfs.Read(fd, bytes) == bytes.size());
        CHECK(std::ranges::equal(bytes, expected.bytes.first(bytes.size())));
        vfs.Close(fd);
        expect_read_only([&] { static_cast<void>(vfs.Open(guest, {.write = true})); });
        expect_read_only([&] { vfs.RemoveFile(guest); });
        expect_read_only([&] { vfs.Rename(guest, guest + ".bak"); });
    };

    check_file("framework/bootdex.jar", "/system/framework/bootdex.jar");
    check_file("framework/framework-res.apk", "/system/framework/framework-res.apk");
    check_file("icu/icudt51l.dat", "/system/usr/icu/icudt51l.dat");
    check_file("zoneinfo/tzdata", "/system/usr/share/zoneinfo/tzdata");

    std::set<std::string> fonts;
    const auto manifest_root = document->Root();
    for (const auto list :
         {*manifest_root.Member("system_fonts"), *manifest_root.Member("ui_fallback_font")->Member("fonts")}) {
        for (std::size_t i = 0; i < list.Size(); ++i)
            fonts.emplace(*list.Element(i)->Member("path")->String());
    }
    REQUIRE(fonts.contains("fonts/DroidSans.ttf"));
    REQUIRE(fonts.contains("fonts/DroidSansFallback.ttf"));
    std::set<std::string> guest_fonts;
    for (const auto& relative : fonts) {
        const auto name = std::filesystem::path(relative).filename().string();
        const auto guest = "/system/fonts/" + name;
        check_file(relative, guest);
        CHECK(std::ranges::equal(font_source(name), ReadSource(root / relative).bytes));
        guest_fonts.insert(guest);
    }

    const auto check_directory = [&](const std::string& path, const std::set<std::string>& expected) {
        INFO(path);
        const auto info = vfs.Stat(path);
        CHECK(info.is_directory);
        CHECK_FALSE(info.writable);
        const auto fd = vfs.OpenDirectory(path);
        CHECK(vfs.DescriptorInfo(fd).is_directory);
        std::set<std::string> names;
        for (const auto& entry : vfs.ReadDirectory(fd, expected.size() + 1)) {
            CHECK_FALSE(entry.is_directory);
            names.insert(path + "/" + entry.name);
        }
        CHECK(names == expected);
        CHECK(vfs.ReadDirectory(fd, 1).empty());
        vfs.Close(fd);
        expect_read_only([&] { static_cast<void>(vfs.Open(path + "/new-file", {.write = true, .create = true})); });
        expect_read_only([&] { vfs.CreateDirectory(path + "/new-dir"); });
        expect_read_only([&] { vfs.RemoveDirectory(path); });
    };
    check_directory("/system/framework", {"/system/framework/bootdex.jar", "/system/framework/framework-res.apk"});
    check_directory("/system/fonts", guest_fonts);
    check_directory("/system/usr/icu", {"/system/usr/icu/icudt51l.dat"});
    check_directory("/system/usr/share/zoneinfo", {"/system/usr/share/zoneinfo/tzdata"});
    CHECK(vfs.IoStatistics().backing_read_bytes == (fonts.size() + 4) * 32);
    CHECK(vfs.IoStatistics().full_materialized_bytes == 0);
}

TEST_CASE("resources core opens the shipped application APK with its independent string pool") {
    res::AssetManager am(Framework());
    const auto path = std::filesystem::path(OGPLAY_SOURCE_DIR) / "samples/sweetshop/out/ogplay-sweetshop.apk";
    CHECK(am.addAssetPath(ReadSource(path)) == 2);
    am.setConfiguration(Config(), "en");
    const auto& table = am.resources();
    const auto id = table.identifierForName("org.ogplay.sweetshop:string/app_name");
    CHECK(id == 0x7f020000);
    auto value = table.getResource(id);
    REQUIRE(value);
    CHECK(value->cookie == 2);
    CHECK(value->value.type == 3);
    CHECK(table.strings(2).stringAt(value->value.data) == "OGPlay Sweet Shop");
}
