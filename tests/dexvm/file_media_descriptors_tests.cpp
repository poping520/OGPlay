#include "boot_dex.h"
// java.io.File through the shared VFS (SBX-5, ADR-0020). Two things matter
// here: a Java save and a native read see one filesystem, and File.mkdirs
// reports the truth. The old handler returned true unconditionally without
// creating anything, so the mkdirs case below fails if that ever returns.

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "ogplay/core/capability_ledger.h"
#include "ogplay/core/logger.h"
#include "ogplay/loader/apk.h"
#include "ogplay/audio/encoded_music.h"
#include "ogplay/audio/java_sound_pool_mixer.h"
#include "ogplay/audio/pcm_mix.h"
#include "ogplay/runtime/dexvm/class_linker.h"
#include "ogplay/runtime/dexvm/intrinsic_builder.h"
#include "ogplay/runtime/dexvm/interpreter.h"
#include "ogplay/runtime/dexvm/vm_monitors.h"
#include "ogplay/runtime/dexvm/io_runtime.h"
#include "ogplay/runtime/dexvm/object_model.h"
#include "ogplay/runtime/dexvm/vm_threads.h"
#include "ogplay/runtime/integration/dexvm_android.h"
#include "ogplay/runtime/integration/dexvm_io_vfs.h"
#include "ogplay/runtime/vfs/sandbox_store.h"
#include "ogplay/runtime/vfs/vfs.h"

#include "file_vfs_fixture.h"

using namespace ogplay::test::file_vfs;
using namespace ogplay::runtime;
using namespace ogplay::runtime::dexvm;

TEST_CASE("MediaPlayer decodes the selected second Ogg descriptor range") {
    const TemporaryRoot root("media-range");
    const auto fixture_path = std::filesystem::path{OGPLAY_SOURCE_DIR} /
                              "tests/fixtures/audio/short-vorbis.ogg";
    std::ifstream fixture(fixture_path, std::ios::binary);
    REQUIRE(fixture.good());
    const std::vector<char> chars{std::istreambuf_iterator<char>(fixture), {}};
    std::vector<std::byte> ogg(chars.size());
    for (std::size_t index = 0; index < chars.size(); ++index) {
        ogg[index] = static_cast<std::byte>(chars[index]);
    }
    std::vector<std::byte> joined = ogg;
    joined.insert(joined.end(), ogg.begin(), ogg.end());
    {
        std::ofstream output(root.path / "joined.ogg", std::ios::binary);
        output.write(reinterpret_cast<const char*>(joined.data()),
                     static_cast<std::streamsize>(joined.size()));
        REQUIRE(output.good());
    }

    ogplay::audio::EncodedMusicMixer music;
    FileVm vm;
    VmThreadRuntime threads(vm.interpreter);
    vm.context->threads = &threads;
    vm.context->encoded_music = &music;
    vm.vfs.MountHostDirectory("/sdcard", root.path);
    const auto file = vm.NewFile("/sdcard/joined.ogg");
    const auto pfd_class =
        vm.linker.ResolveDescriptor("Landroid/os/ParcelFileDescriptor;");
    const auto open = vm.linker.FindDirectMethod(
        pfd_class, "open",
        "(Ljava/io/File;I)Landroid/os/ParcelFileDescriptor;");
    REQUIRE(open.has_value());
    const auto opened = vm.interpreter.Call(
        *open, std::vector<VmValue>{VmValue::Ref(file),
                                    VmValue::Int(0x10000000)});
    REQUIRE_MESSAGE(!opened.exception.IsValid(), opened.exception_message);
    const auto fd = vm.CallOn(opened.value.ref, "getFileDescriptor",
                              "()Ljava/io/FileDescriptor;").ref;
    const auto player = vm.interpreter.NewIntrinsicInstance(
        "Landroid/media/MediaPlayer;");
    vm.CallOn(player, "<init>", "()V");
    vm.CallOn(player, "setDataSource", "(Ljava/io/FileDescriptor;JJ)V",
              {VmValue::Ref(fd), VmValue::Long(
                   static_cast<std::int64_t>(ogg.size())),
               VmValue::Long(static_cast<std::int64_t>(ogg.size()))});
    vm.CallOn(player, "prepare", "()V");
    vm.CallOn(player, "start", "()V");
    const auto& source = vm.context->media_players.at(player.Value()).source;
    CHECK(source.kind == ogplay::audio::EncodedAudioSource::Kind::vfs_path);
    CHECK(source.name == "/proc/self/fd/" + std::to_string(vm.CallOn(fd, "getInt$", "()I").AsInt()));
    CHECK(source.offset == ogg.size());
    CHECK(source.length == ogg.size());
    std::vector<std::int16_t> pcm(2048U * 2U);
    std::vector<std::int64_t> accumulator(pcm.size());
    music.MixIntoAccumulator(accumulator, 48000U);
    ogplay::audio::SaturateStereoPcm16(accumulator, pcm);
    CHECK(std::ranges::any_of(pcm,
        [](const std::int16_t sample) { return sample != 0; }));

    vm.CallOn(player, "stop", "()V");
    vm.CallOn(player, "reset", "()V");
    vm.CallOn(player, "setDataSource", "(Ljava/io/FileDescriptor;)V",
              {VmValue::Ref(fd)});
    const auto& full_source = vm.context->media_players.at(player.Value()).source;
    CHECK(full_source.offset == 0U);
    CHECK(full_source.length == joined.size());
    vm.CallOn(player, "prepare", "()V");
}

TEST_CASE("MediaPlayer FD lease survives close and path replacement") {
    FileVm vm;
    vm.vfs.CreateDirectory("/sdcard");
    const auto fixture_path = std::filesystem::path{OGPLAY_SOURCE_DIR} /
                              "tests/fixtures/audio/short-vorbis.ogg";
    std::ifstream fixture(fixture_path, std::ios::binary);
    REQUIRE(fixture.good());
    const std::vector<char> chars{std::istreambuf_iterator<char>(fixture), {}};
    std::vector<std::byte> ogg(chars.size());
    for (std::size_t index = 0; index < chars.size(); ++index) {
        ogg[index] = static_cast<std::byte>(chars[index]);
    }
    vm.vfs.PutFile("/sdcard/lease.ogg", ogg, true);
    ogplay::audio::EncodedMusicMixer music;
    vm.context->encoded_music = &music;
    VmThreadRuntime threads(vm.interpreter);
    vm.context->threads = &threads;
    const auto stream = vm.interpreter.NewIntrinsicInstance(
        "Ljava/io/FileInputStream;");
    vm.CallOn(stream, "<init>", "(Ljava/lang/String;)V",
              {VmValue::Ref(vm.interpreter.NewStringUtf8("/sdcard/lease.ogg"))});
    const auto fd = vm.CallOn(stream, "getFD",
                              "()Ljava/io/FileDescriptor;").ref;
    const auto player = vm.interpreter.NewIntrinsicInstance(
        "Landroid/media/MediaPlayer;");
    vm.CallOn(player, "<init>", "()V");
    vm.CallOn(player, "setDataSource", "(Ljava/io/FileDescriptor;)V",
              {VmValue::Ref(fd)});
    REQUIRE(vm.context->encoded_audio_leases.size() == 1U);
    vm.CallOn(stream, "close", "()V");
    vm.vfs.RemoveFile("/sdcard/lease.ogg");
    std::vector<std::byte> other(ogg.size(), std::byte{0});
    vm.vfs.PutFile("/sdcard/lease.ogg", other, true);
    vm.CallOn(player, "prepare", "()V");
    vm.CallOn(player, "start", "()V");
    std::vector<std::int16_t> pcm(512U * 2U);
    std::vector<std::int64_t> accumulator(pcm.size());
    music.MixIntoAccumulator(accumulator, 48000U);
    ogplay::audio::SaturateStereoPcm16(accumulator, pcm);
    CHECK(std::ranges::any_of(pcm, [](const std::int16_t sample) {
        return sample != 0;
    }));
}

TEST_CASE("MediaPlayer resolves a JNI-built FileDescriptor through its guest VFS descriptor") {
    FileVm vm;
    vm.vfs.CreateDirectory("/sdcard");
    const auto fixture_path = std::filesystem::path{OGPLAY_SOURCE_DIR} /
                              "tests/fixtures/audio/short-vorbis.ogg";
    std::ifstream fixture(fixture_path, std::ios::binary);
    REQUIRE(fixture.good());
    const std::vector<char> chars{std::istreambuf_iterator<char>(fixture), {}};
    // Like an APK asset: payload at a non-zero offset inside a larger file.
    std::vector<std::byte> pack(13U, std::byte{0x5a});
    for (const auto value : chars) pack.push_back(static_cast<std::byte>(value));
    const auto payload = static_cast<std::int64_t>(chars.size());
    pack.insert(pack.end(), 7U, std::byte{0x5a});
    vm.vfs.PutFile("/sdcard/pack.bin", pack, true);
    ogplay::audio::EncodedMusicMixer music;
    vm.context->encoded_music = &music;
    VmThreadRuntime threads(vm.interpreter);
    vm.context->threads = &threads;

    // JNI idiom: NewObject(FileDescriptor) then SetIntField("descriptor").
    const auto jni_fd = [&](const std::int32_t value) {
        const auto fd = vm.interpreter.NewIntrinsicInstance("Ljava/io/FileDescriptor;");
        vm.CallOn(fd, "<init>", "()V");
        vm.CallOn(fd, "setInt$", "(I)V", {VmValue::Int(value)});
        return fd;
    };
    const auto new_player = [&] {
        const auto player = vm.interpreter.NewIntrinsicInstance("Landroid/media/MediaPlayer;");
        vm.CallOn(player, "<init>", "()V");
        return player;
    };
    const auto set_source = [&](VmObjectRef player, VmObjectRef fd, std::int64_t offset,
                                std::int64_t length) {
        return vm.CallOnOutcome(player, "setDataSource", "(Ljava/io/FileDescriptor;JJ)V",
            {VmValue::Ref(fd), VmValue::Long(offset), VmValue::Long(length)});
    };
    const auto exception_class = [&](const VmCallOutcome& outcome) {
        REQUIRE(outcome.exception.IsValid());
        return vm.linker.Class(vm.model.ObjectClass(outcome.exception)).descriptor;
    };

    const auto native = vm.vfs.Open("/sdcard/pack.bin", {.read = true});
    const auto fd = jni_fd(native);
    const auto player = new_player();
    const auto roots = vm.interpreter.ProtectReferences(std::array{fd, player});
    REQUIRE_FALSE(set_source(player, fd, 13, payload).exception.IsValid());
    const auto& source = vm.context->media_players.at(player.Value()).source;
    CHECK(source.name == "/proc/self/fd/" + std::to_string(native));
    CHECK(source.offset == 13U);
    CHECK(source.length == static_cast<std::uint64_t>(payload));
    CHECK(source.lease != 0U);
    // The lease is a dup: closing and replacing the file keeps the window.
    vm.vfs.Close(native);
    vm.vfs.RemoveFile("/sdcard/pack.bin");
    vm.CallOn(player, "prepare", "()V");
    vm.CallOn(player, "start", "()V");
    std::vector<std::int16_t> pcm(512U * 2U);
    std::vector<std::int64_t> accumulator(pcm.size());
    music.MixIntoAccumulator(accumulator, 48000U);
    ogplay::audio::SaturateStereoPcm16(accumulator, pcm);
    CHECK(std::ranges::any_of(pcm, [](const std::int16_t sample) { return sample != 0; }));

    // MediaPlayerService clips length to the file and rejects bad windows.
    vm.vfs.PutFile("/sdcard/pack.bin", pack, true);
    const auto again = vm.vfs.Open("/sdcard/pack.bin", {.read = true});
    const auto clipped = new_player();
    const auto clip_roots = vm.interpreter.ProtectReferences(std::array{clipped});
    REQUIRE_FALSE(set_source(clipped, jni_fd(again), 13, 0x7ffffffffffffffLL).exception.IsValid());
    CHECK(vm.context->media_players.at(clipped.Value()).source.length == pack.size() - 13U);
    CHECK(exception_class(set_source(new_player(), jni_fd(again),
        static_cast<std::int64_t>(pack.size()), 1)) == "Ljava/io/IOException;");
    CHECK(exception_class(set_source(new_player(), jni_fd(-1), 0, 1)) == "Ljava/io/IOException;");
    vm.vfs.Close(again);
    CHECK(exception_class(set_source(new_player(), jni_fd(again), 0, 1)) == "Ljava/io/IOException;");
    const auto directory = vm.vfs.OpenDirectory("/sdcard");
    CHECK(exception_class(set_source(new_player(), jni_fd(directory), 0, 1)) == "Ljava/io/IOException;");
    vm.vfs.Close(directory);
}

TEST_CASE("DVM-338 ROM adoptFd AFD close retains the captured media lease") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        for (const bool register_first : {false, true}) {
            CAPTURE(backend);
            CAPTURE(register_first);
            FileVm vm(nullptr, true, {.backend = backend});
            std::ifstream fixture(std::filesystem::path{OGPLAY_SOURCE_DIR} / "tests/fixtures/audio/short-vorbis.ogg",
                                  std::ios::binary);
            REQUIRE(fixture.good());
            const std::vector<char> chars{std::istreambuf_iterator<char>(fixture), {}};
            std::vector<std::byte> pack(13, std::byte{0x5a});
            for (const auto c : chars)
                pack.push_back(static_cast<std::byte>(c));
            pack.insert(pack.end(), 7, std::byte{0x5a});
            vm.vfs.PutFile("/pack", pack, true);
            const auto native = vm.vfs.Open("/pack", {.read = true});
            CHECK(vm.vfs.Seek(native, 3, VfsSeekWhence::begin) == 3);
            ogplay::audio::EncodedMusicMixer music;
            vm.context->encoded_music = &music;
            VmThreadRuntime threads(vm.interpreter);
            vm.context->threads = &threads;
            const auto type = vm.linker.ResolveDescriptor("Landroid/os/ParcelFileDescriptor;");
            const auto adopt = vm.linker.FindDirectMethod(type, "adoptFd", "(I)Landroid/os/ParcelFileDescriptor;");
            REQUIRE(adopt.has_value());
            const auto adopted = vm.interpreter.Call(*adopt, std::array{VmValue::Int(native)});
            REQUIRE_MESSAGE(!adopted.exception.IsValid(), adopted.exception_message);
            const auto pfd = adopted.value.ref;
            const auto pfd_root = vm.interpreter.ProtectReferences(std::array{pfd});
            CHECK(vm.CallOn(pfd, "getFd", "()I").AsInt() == native);
            const auto afd = vm.interpreter.NewIntrinsicInstance("Landroid/content/res/AssetFileDescriptor;");
            const auto afd_root = vm.interpreter.ProtectReferences(std::array{afd});
            vm.CallOn(afd, "<init>", "(Landroid/os/ParcelFileDescriptor;JJ)V",
                      {VmValue::Ref(pfd), VmValue::Long(13), VmValue::Long(static_cast<std::int64_t>(chars.size()))});
            const auto fd = vm.CallOn(afd, "getFileDescriptor", "()Ljava/io/FileDescriptor;").ref;
            if (register_first) {
                const auto posix = vm.interpreter.NewIntrinsicInstance("Llibcore/io/Posix;");
                vm.CallOn(posix, "fstat", "(Ljava/io/FileDescriptor;)Llibcore/io/StructStat;", {VmValue::Ref(fd)});
            }
            const auto player = vm.interpreter.NewIntrinsicInstance("Landroid/media/MediaPlayer;");
            const auto player_root = vm.interpreter.ProtectReferences(std::array{player});
            vm.CallOn(player, "<init>", "()V");
            vm.CallOn(player, "setDataSource", "(Ljava/io/FileDescriptor;JJ)V",
                      {VmValue::Ref(fd), VmValue::Long(13), VmValue::Long(static_cast<std::int64_t>(chars.size()))});
            CHECK(vm.context->media_players.at(player.Value()).source.lease != 0);
            CHECK(vm.vfs.Seek(native, 0, VfsSeekWhence::current) == 3);
            vm.CallOn(afd, "close", "()V");
            CHECK(vm.CallOn(fd, "getInt$", "()I").AsInt() == -1);
            const auto assert_closed = [&] {
                try {
                    vm.vfs.Close(native);
                    FAIL("guest fd remained open");
                } catch (const VfsError& error) {
                    CHECK(error.ErrorNumber() == 9);
                }
            };
            assert_closed();
            vm.CallOn(afd, "close", "()V");
            assert_closed();
            vm.CallOn(player, "prepare", "()V");
            vm.CallOn(player, "start", "()V");
            std::vector<std::int64_t> accumulator(1024);
            music.MixIntoAccumulator(accumulator, 48000);
            CHECK(std::ranges::any_of(accumulator, [](auto sample) { return sample != 0; }));
            CHECK(vm.ledger.Unimplemented().empty());
            vm.CallOn(player, "release", "()V");
        }
    }
}
