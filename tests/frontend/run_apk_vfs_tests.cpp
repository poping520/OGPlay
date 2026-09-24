#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <span>
#include <stop_token>
#include <string_view>
#include <vector>

#include "ogplay/audio/encoded_audio_stream.h"
#include "../../src/frontend/cli/run_apk_vfs.h"

namespace {

class TemporaryDirectory final {
public:
    TemporaryDirectory() {
        static std::atomic_uint64_t next{};
        path = std::filesystem::temp_directory_path() /
               ("ogplay-run-apk-vfs-" +
                std::to_string(next.fetch_add(1U, std::memory_order_relaxed)));
        std::filesystem::remove_all(path);
    }
    ~TemporaryDirectory() {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
    std::filesystem::path path;
};

void Append16(std::vector<std::byte>& out, const std::uint16_t value) {
    out.push_back(static_cast<std::byte>(value));
    out.push_back(static_cast<std::byte>(value >> 8U));
}
void Append32(std::vector<std::byte>& out, const std::uint32_t value) {
    Append16(out, static_cast<std::uint16_t>(value));
    Append16(out, static_cast<std::uint16_t>(value >> 16U));
}
void AppendText(std::vector<std::byte>& out, const std::string_view text) {
    for (const auto value : text) out.push_back(static_cast<std::byte>(value));
}
std::uint32_t Crc32(const std::span<const std::byte> bytes) {
    std::uint32_t crc = 0xffffffffU;
    for (const auto byte : bytes) {
        crc ^= std::to_integer<std::uint8_t>(byte);
        for (unsigned bit = 0; bit < 8; ++bit) {
            const auto mask = 0U - (crc & 1U);
            crc = (crc >> 1U) ^ (0xedb88320U & mask);
        }
    }
    return ~crc;
}
std::vector<std::byte> FixedZip() {
    constexpr std::string_view name = "music.bin";
    constexpr std::string_view text = "hello deflated apk entry";
    std::vector<std::byte> payload;
    AppendText(payload, text);
    const std::vector<std::byte> compressed{
        std::byte{0xcb}, std::byte{0x48}, std::byte{0xcd}, std::byte{0xc9},
        std::byte{0xc9}, std::byte{0x57}, std::byte{0x48}, std::byte{0x49},
        std::byte{0x4d}, std::byte{0xcb}, std::byte{0x49}, std::byte{0x2c},
        std::byte{0x49}, std::byte{0x4d}, std::byte{0x51}, std::byte{0x48},
        std::byte{0x2c}, std::byte{0xc8}, std::byte{0x56}, std::byte{0x48},
        std::byte{0xcd}, std::byte{0x2b}, std::byte{0x29}, std::byte{0xaa},
        std::byte{0x04}, std::byte{0x00}};
    const auto crc = Crc32(payload);
    std::vector<std::byte> bytes;
    Append32(bytes, 0x04034b50); Append16(bytes, 20); Append16(bytes, 0);
    Append16(bytes, 8); Append16(bytes, 0); Append16(bytes, 0); Append32(bytes, crc);
    Append32(bytes, static_cast<std::uint32_t>(compressed.size()));
    Append32(bytes, static_cast<std::uint32_t>(payload.size()));
    Append16(bytes, static_cast<std::uint16_t>(name.size())); Append16(bytes, 0);
    AppendText(bytes, name); bytes.insert(bytes.end(), compressed.begin(), compressed.end());
    const auto central = static_cast<std::uint32_t>(bytes.size());
    Append32(bytes, 0x02014b50); Append16(bytes, 20); Append16(bytes, 20);
    Append16(bytes, 0); Append16(bytes, 8); Append16(bytes, 0); Append16(bytes, 0);
    Append32(bytes, crc); Append32(bytes, static_cast<std::uint32_t>(compressed.size()));
    Append32(bytes, static_cast<std::uint32_t>(payload.size()));
    Append16(bytes, static_cast<std::uint16_t>(name.size()));
    Append16(bytes, 0); Append16(bytes, 0); Append16(bytes, 0); Append16(bytes, 0);
    Append32(bytes, 0); Append32(bytes, 0); AppendText(bytes, name);
    const auto central_size = static_cast<std::uint32_t>(bytes.size()) - central;
    Append32(bytes, 0x06054b50); Append16(bytes, 0); Append16(bytes, 0);
    Append16(bytes, 1); Append16(bytes, 1); Append32(bytes, central_size);
    Append32(bytes, central); Append16(bytes, 0);
    return bytes;
}

std::vector<std::byte> DeflateStored(
    const std::span<const std::byte> payload) {
    std::vector<std::byte> encoded;
    std::size_t offset{};
    do {
        const auto count = std::min<std::size_t>(65535U,
                                                 payload.size() - offset);
        const bool final = offset + count == payload.size();
        encoded.push_back(final ? std::byte{1} : std::byte{0});
        Append16(encoded, static_cast<std::uint16_t>(count));
        Append16(encoded, static_cast<std::uint16_t>(~count));
        encoded.insert(encoded.end(), payload.begin() +
                            static_cast<std::ptrdiff_t>(offset),
                       payload.begin() +
                            static_cast<std::ptrdiff_t>(offset + count));
        offset += count;
    } while (offset < payload.size());
    return encoded;
}

std::vector<std::byte> SingleEntryZip(
    const std::string_view name, const std::span<const std::byte> payload,
    const std::uint16_t method) {
    const auto body = method == 0U
                          ? std::vector<std::byte>(payload.begin(), payload.end())
                          : DeflateStored(payload);
    const auto crc = Crc32(payload);
    std::vector<std::byte> bytes;
    Append32(bytes, 0x04034b50); Append16(bytes, 20); Append16(bytes, 0);
    Append16(bytes, method); Append16(bytes, 0); Append16(bytes, 0);
    Append32(bytes, crc); Append32(bytes, static_cast<std::uint32_t>(body.size()));
    Append32(bytes, static_cast<std::uint32_t>(payload.size()));
    Append16(bytes, static_cast<std::uint16_t>(name.size())); Append16(bytes, 0);
    AppendText(bytes, name); bytes.insert(bytes.end(), body.begin(), body.end());
    const auto central = static_cast<std::uint32_t>(bytes.size());
    Append32(bytes, 0x02014b50); Append16(bytes, 20); Append16(bytes, 20);
    Append16(bytes, 0); Append16(bytes, method); Append16(bytes, 0); Append16(bytes, 0);
    Append32(bytes, crc); Append32(bytes, static_cast<std::uint32_t>(body.size()));
    Append32(bytes, static_cast<std::uint32_t>(payload.size()));
    Append16(bytes, static_cast<std::uint16_t>(name.size()));
    Append16(bytes, 0); Append16(bytes, 0); Append16(bytes, 0); Append16(bytes, 0);
    Append32(bytes, 0); Append32(bytes, 0); AppendText(bytes, name);
    const auto central_size = static_cast<std::uint32_t>(bytes.size()) - central;
    Append32(bytes, 0x06054b50); Append16(bytes, 0); Append16(bytes, 0);
    Append16(bytes, 1); Append16(bytes, 1); Append32(bytes, central_size);
    Append32(bytes, central); Append16(bytes, 0);
    return bytes;
}

std::vector<std::byte> ReadAudioFixture() {
    const auto path = std::filesystem::path{OGPLAY_SOURCE_DIR} /
                      "tests/fixtures/audio/short-vorbis.ogg";
    std::ifstream input(path, std::ios::binary);
    REQUIRE(input.good());
    const std::vector<char> chars{std::istreambuf_iterator<char>(input), {}};
    std::vector<std::byte> bytes(chars.size());
    std::transform(chars.begin(), chars.end(), bytes.begin(),
                   [](const char value) { return static_cast<std::byte>(value); });
    return bytes;
}

class LeaseAudioSource final : public ogplay::audio::EncodedAudioDataSource {
public:
    explicit LeaseAudioSource(
        std::shared_ptr<const ogplay::runtime::VfsReadLease> lease)
        : lease_(std::move(lease)) {}
    [[nodiscard]] std::uint64_t Size() const noexcept override {
        return lease_->Size();
    }
    [[nodiscard]] std::size_t ReadAt(
        const std::uint64_t offset, const std::span<std::byte> destination,
        const std::stop_token stop) const override {
        if (stop.stop_requested()) return 0U;
        return lease_->ReadAt(offset, destination);
    }
private:
    std::shared_ptr<const ogplay::runtime::VfsReadLease> lease_;
};

ogplay::session::TitleProfile ArchiveProfile(
    const ogplay::session::ProfileSource source) {
    ogplay::session::TitleProfile profile;
    profile.data = ogplay::session::ProfileData{
        .mounts = {{"/archive", source, true}}};
    return profile;
}

}  // namespace

TEST_CASE("direct run-apk allocates and reuses an unambiguous installation") {
    TemporaryDirectory tree;
    const auto root = tree.path / "sandbox";
    {
        auto sandbox = ogplay::frontend::OpenSandbox(
            {.directory = root}, "org.example.game", 1U);
        CHECK(sandbox.installation_id == "org.example.game");
        CHECK(std::filesystem::is_directory(root / "org.example.game"));
    }
    {
        auto sandbox = ogplay::frontend::OpenSandbox(
            {.directory = root}, "org.example.game", 2U);
        CHECK(sandbox.installation_id == "org.example.game");
    }
}

TEST_CASE("direct run-apk reuses a unique suffixed installation") {
    TemporaryDirectory tree;
    const auto root = tree.path / "sandbox";
    std::filesystem::create_directories(root);
    auto created = ogplay::runtime::SandboxStore::Create(
        root, "org.example.game-2", "org.example.game");
    created.reset();
    auto sandbox = ogplay::frontend::OpenSandbox(
        {.directory = root}, "org.example.game", 1U);
    CHECK(sandbox.installation_id == "org.example.game-2");
}

TEST_CASE("direct run-apk requires disambiguation for multiple installations") {
    TemporaryDirectory tree;
    const auto root = tree.path / "sandbox";
    std::filesystem::create_directories(root);
    auto first = ogplay::runtime::SandboxStore::Create(
        root, "org.example.game", "org.example.game");
    auto second = ogplay::runtime::SandboxStore::Create(
        root, "org.example.game-2", "org.example.game");
    first.reset();
    second.reset();
    CHECK_THROWS_WITH(
        static_cast<void>(ogplay::frontend::OpenSandbox(
            {.directory = root}, "org.example.game", 1U)),
        "multiple save sandbox installations exist for package "
        "org.example.game; pass --installation-id <id>");
}

TEST_CASE("run-apk mounts an external directory without a Profile") {
    TemporaryDirectory tree;
    std::filesystem::create_directories(tree.path);
    {
        std::ofstream file(tree.path / "data.bin", std::ios::binary);
        file << "external-data";
    }
    ogplay::session::TitleProfile generic;
    generic.identity.package = "org.example.game";
    ogplay::runtime::VirtualFileSystem vfs;
    ogplay::frontend::MountExternalDirectory(generic, tree.path, vfs);
    const auto descriptor = vfs.Open("/storage/emulated/0/data.bin", {.read = true});
    std::array<std::byte, 8> output{};
    CHECK(vfs.Read(descriptor, output) == output.size());
    CHECK(std::string(reinterpret_cast<const char*>(output.data()), output.size()) ==
          "external");
    CHECK(vfs.DescriptorInfo(descriptor).source ==
          ogplay::runtime::VfsSource::external);
    vfs.Close(descriptor);

    ogplay::runtime::VirtualFileSystem explicit_vfs;
    ogplay::frontend::MountExternalDirectory(
        generic, tree.path, explicit_vfs,
        std::string{"/sdcard/Android/data/org.example.game/files"});
    CHECK_NOTHROW(static_cast<void>(explicit_vfs.Stat(
        "/sdcard/Android/data/org.example.game/files/data.bin")));
    CHECK_THROWS(static_cast<void>(explicit_vfs.Stat("/sdcard/data.bin")));
    ogplay::runtime::VirtualFileSystem invalid_vfs;
    CHECK_THROWS(ogplay::frontend::MountExternalDirectory(
        generic, tree.path, invalid_vfs, std::string{"/data/data/org.example.game"}));

    generic.data = ogplay::session::ProfileData{
        .mounts = {{"/sdcard/game", ogplay::session::ProfileSource::external, true}}};
    ogplay::runtime::VirtualFileSystem profiled_vfs;
    ogplay::frontend::MountExternalDirectory(generic, tree.path, profiled_vfs);
    CHECK_NOTHROW(static_cast<void>(profiled_vfs.Stat("/sdcard/game/data.bin")));
    CHECK_THROWS(static_cast<void>(profiled_vfs.Stat("/sdcard/data.bin")));
    CHECK_THROWS(ogplay::frontend::MountExternalDirectory(
        generic, tree.path, profiled_vfs, std::string{"/sdcard/elsewhere"}));
}

TEST_CASE("run-apk mounts the original OBB file with bounded reads") {
    TemporaryDirectory tree;
    std::filesystem::create_directories(tree.path);
    const auto file = tree.path / "main.1.org.example.game.obb";
    {
        std::ofstream output(file, std::ios::binary);
        output << "original-obb-bytes";
    }
    ogplay::runtime::VirtualFileSystem vfs;
    vfs.MountHostFile(ogplay::runtime::VfsSource::obb,
                      "/sdcard/Android/obb/org.example.game", file);
    vfs.AddPathAlias("/storage/emulated/0", "/sdcard");
    const auto descriptor = vfs.Open(
        "/storage/emulated/0/Android/obb/org.example.game/main.1.org.example.game.obb",
        {.read = true});
    CHECK(vfs.DescriptorInfo(descriptor).source == ogplay::runtime::VfsSource::obb);
    CHECK(vfs.Seek(descriptor, 9, ogplay::runtime::VfsSeekWhence::begin) == 9U);
    std::array<std::byte, 3> output{};
    CHECK(vfs.Read(descriptor, output) == output.size());
    CHECK(std::string(reinterpret_cast<const char*>(output.data()), output.size()) ==
          "obb");
    CHECK(vfs.IoStatistics().full_materialized_bytes == 0U);
    vfs.Close(descriptor);
    CHECK_THROWS(vfs.MountHostFile(ogplay::runtime::VfsSource::obb,
                                  "/sdcard/Android/obb/org.example.game", file));
}

TEST_CASE("run-apk archive mounts enforce compressed block cache budget") {
    const auto bytes = FixedZip();
    const auto archive = ogplay::loader::ParseApkArchive(bytes);
    const auto owned =
        std::make_shared<const std::vector<std::byte>>(bytes);
    {
        ogplay::runtime::VirtualFileSystem vfs(
            {.resource_memory_budget_bytes = 1U});
        ogplay::frontend::MountApkArchive(
            ArchiveProfile(ogplay::session::ProfileSource::apk), owned, archive,
            vfs);
        const auto descriptor = vfs.Open("/archive/music.bin", {.read = true});
        std::array<std::byte, 5> output{};
        CHECK_THROWS_WITH(static_cast<void>(vfs.Read(descriptor, output)),
                          "VFS resource memory budget exhausted");
        vfs.Close(descriptor);
    }
    {
        ogplay::runtime::VirtualFileSystem vfs(
            {.resource_memory_budget_bytes = 25U});
        ogplay::frontend::MountObbArchive(
            ArchiveProfile(ogplay::session::ProfileSource::obb), owned, archive,
            vfs);
        const auto descriptor = vfs.Open("/archive/music.bin", {.read = true});
        std::array<std::byte, 5> output{};
        CHECK(vfs.Read(descriptor, output) == output.size());
        CHECK(std::string(reinterpret_cast<const char*>(output.data()), output.size()) ==
              "hello");
        CHECK(vfs.Seek(descriptor, 10,
                       ogplay::runtime::VfsSeekWhence::begin) == 10U);
        CHECK(vfs.Read(descriptor, output) == output.size());
        CHECK(vfs.Seek(descriptor, 0,
                       ogplay::runtime::VfsSeekWhence::begin) == 0U);
        CHECK(vfs.Read(descriptor, output) == output.size());
        CHECK(std::string(reinterpret_cast<const char*>(output.data()), output.size()) ==
              "hello");
        vfs.Close(descriptor);
        const std::array writable{std::byte{1}, std::byte{2}};
        vfs.PutFile("/writable", writable, true);
        const auto writable_descriptor =
            vfs.Open("/writable", {.read = true, .write = true});
        CHECK_THROWS_WITH(
            static_cast<void>(vfs.CaptureReadLease(writable_descriptor, 0, 2)),
            "VFS resource memory budget exhausted");
        CHECK(vfs.IoStatistics().resource_memory_bytes == 24U);
        vfs.Close(writable_descriptor);
    }
}

TEST_CASE("run-apk archive cache releases failed validation reservations") {
    auto bytes = FixedZip();
    const auto archive = ogplay::loader::ParseApkArchive(bytes);
    constexpr std::size_t local_header_bytes = 30U;
    constexpr std::size_t name_bytes = std::string_view("music.bin").size();
    bytes[local_header_bytes + name_bytes + 5U] ^= std::byte{0x40};
    const auto owned =
        std::make_shared<const std::vector<std::byte>>(bytes);
    ogplay::runtime::VirtualFileSystem vfs(
        {.resource_memory_budget_bytes = 64U * 1024U});
    ogplay::frontend::MountApkArchive(
        ArchiveProfile(ogplay::session::ProfileSource::apk), owned, archive,
        vfs);
    const auto descriptor = vfs.Open("/archive/music.bin", {.read = true});
    std::array<std::byte, 5> output{};
    CHECK_THROWS(static_cast<void>(vfs.Read(descriptor, output)));
    CHECK(vfs.IoStatistics().resource_memory_bytes == 0U);
    CHECK_THROWS(static_cast<void>(vfs.Read(descriptor, output)));
    CHECK(vfs.IoStatistics().resource_memory_bytes == 0U);
    vfs.Close(descriptor);
}

TEST_CASE("music stream consumes stored and Deflate archive VFS leases") {
    const auto payload = ReadAudioFixture();
    for (const auto method : {std::uint16_t{0}, std::uint16_t{8}}) {
        CAPTURE(method);
        const auto bytes = SingleEntryZip("music.ogg", payload, method);
        const auto owned =
            std::make_shared<const std::vector<std::byte>>(bytes);
        const auto archive = ogplay::loader::ParseApkArchive(*owned);
        ogplay::runtime::VirtualFileSystem vfs;
        ogplay::frontend::MountApkArchive(
            ArchiveProfile(ogplay::session::ProfileSource::apk), owned,
            archive, vfs);
        const auto descriptor = vfs.Open("/archive/music.ogg", {.read = true});
        auto lease = vfs.CaptureReadLease(descriptor, 0);
        vfs.Close(descriptor);
        auto source = std::make_shared<LeaseAudioSource>(std::move(lease));
        ogplay::audio::EncodedAudioStream stream(source);
        CHECK(stream.Frames() > 0U);
        CHECK_NOTHROW(static_cast<void>(stream.Sample(stream.Frames() - 1U)));
        CHECK(vfs.IoStatistics().full_materialized_bytes == 0U);
    }
}
