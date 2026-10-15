#include "ogplay/runtime/integration/bitmap_pixels.h"
#include "runtime/boundary/core/boundary_symbols.h"
#include "ogplay/runtime/dexvm/big_int_runtime.h"
#include <set>
#include "ogplay/runtime/integration/native_activity_runtime.h"
#include "ogplay/runtime/dexvm/nio_runtime.h"
#include "runtime/integration/dexvm_android/shared.h"
#include "../fixtures/bitmap_decode_data.h"
#include <chrono>
#include <atomic>
#include "ogplay/runtime/vfs/sandbox_store.h"
#include "../dexvm/boot_dex.h"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <future>
#include "ogplay/runtime/dexvm/vm_monitors.h"
#include <thread>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

#include <doctest/doctest.h>
#include "ogplay/runtime/dexvm/reflection.h"

#include "ogplay/core/capability_ledger.h"
#include "ogplay/core/encoding.h"
#include "ogplay/core/logger.h"
#include "ogplay/loader/elf.h"
#include "ogplay/runtime/dexvm/access_flags.h"
#include "ogplay/runtime/dexvm/class_loader_facade.h"
#include "ogplay/runtime/integration/dexvm_android.h"
#include "ogplay/runtime/integration/dexvm_bridge.h"
#include "ogplay/runtime/integration/native_library_loader.h"
#include "ogplay/runtime/integration/apk_assets.h"
#include "ogplay/runtime/jni/jni.h"
#include "ogplay/runtime/jni/jni_java_vm.h"
#include "ogplay/runtime/jni/jni_field_store.h"
#include "ogplay/runtime/jni/jni_native_registry.h"
#include "ogplay/runtime/jni/jni_object_array.h"
#include "ogplay/runtime/jni_guest/jni_guest_abi.h"
#include "ogplay/runtime/jni_guest/jni_guest_static_calls.h"
#include "ogplay/runtime/jni_guest/jni_guest_bindings.h"
#include "ogplay/cpu/interpreter.h"
#include "ogplay/memory/bus.h"
#include "ogplay/session/android_app_process.h"
#include "ogplay/session/dex_activity_lifecycle.h"
#include "ogplay/video/fake_video_player.h"

#include "native_library_fixture.h"

using namespace ogplay::test::native_library;

namespace {

// Real ARM constructors record their order in guest memory and optionally
// reopen a library through the production libdl thunk.
std::vector<std::byte> NativeDlopenElf(const AppElfOptions& options,
    const ogplay::memory::GuestAddress sequence, const std::string_view reopen = {}) {
    auto bytes = AppElf(options);
    const std::array code{
        0xe92d4010U, 0xe59f4030U, 0xe08f4004U, 0xe59f302cU,
        0xe5932000U, 0xe2822001U, 0xe5832000U, 0xe5842000U,
        0xe28f0068U, 0xe3a01001U, 0xe59f3014U, 0xe12fff33U,
        0xe5840004U, 0xe8bd8010U,
    };
    for (std::size_t i = 0; i < code.size(); ++i) Put32(bytes, 0x1010U + i * 4U, code[i]);
    if (reopen.empty()) Put32(bytes, 0x1030U, 0xe8bd8010U);
    Put32(bytes, 0x104cU, 0x10250U - 0x20020U);
    Put32(bytes, 0x1050U, sequence.Value());
    const auto symbols = ogplay::runtime::detail::BuildAndroidBoundarySymbols();
    const ogplay::runtime::BionicHleSymbolProvider provider(symbols);
    Put32(bytes, 0x1054U, provider.Lookup("libdl.so", "dlopen")->Value());
    REQUIRE(reopen.size() < 0x60U);
    for (std::size_t i = 0; i < reopen.size(); ++i) bytes[0x10a0U + i] = static_cast<std::byte>(reopen[i]);
    if (options.jni_version) {
        Put32(bytes, 0x1000U, 0xea000016U); // OnLoad branches to its counter body.
        const std::array on_load{0xe59f3014U, 0xe08f3003U, 0xe5932000U,
            0xe2822001U, 0xe5832000U, 0xe59f0004U, 0xe12fff1eU};
        for (std::size_t i = 0; i < on_load.size(); ++i) Put32(bytes, 0x1060U + i * 4U, on_load[i]);
        Put32(bytes, 0x107cU, 0x10258U - 0x2006cU);
        Put32(bytes, 0x1080U, *options.jni_version);
    }
    return bytes;
}

std::uint32_t NativeDlCall(ogplay::runtime::AndroidGuestProcess& process,
    const char* operation, const std::array<std::uint32_t, 4> arguments,
    const std::uint64_t thread = 1U) {
    const auto symbols = ogplay::runtime::detail::BuildAndroidBoundarySymbols();
    const ogplay::runtime::BionicHleSymbolProvider provider(symbols);
    ogplay::runtime::A32GuestCallFrame frame{*provider.Lookup("libdl.so", operation), arguments, {}};
    frame.thread_id = thread;
    return process.Invoke(frame).return_value;
}

std::uint32_t NativeOpen(ogplay::runtime::AndroidGuestProcess& process,
    const std::string_view path, const std::uint64_t thread = 1U) {
    const auto access = process.GuestMemoryAccess();
    const auto scratch = access.allocate(static_cast<std::uint32_t>(path.size() + 1U));
    std::vector<std::byte> bytes(path.size() + 1U);
    for (std::size_t i = 0; i < path.size(); ++i) bytes[i] = static_cast<std::byte>(path[i]);
    access.write(scratch, bytes);
    try {
        const auto handle = NativeDlCall(process, "dlopen", {scratch.Value(), 1U, 0U, 0U}, thread);
        access.release(scratch, static_cast<std::uint32_t>(bytes.size()));
        return handle;
    } catch (...) {
        access.release(scratch, static_cast<std::uint32_t>(bytes.size()));
        throw;
    }
}

std::uint32_t ReadNativeWord(ogplay::runtime::AndroidGuestProcess& process,
    const ogplay::memory::GuestAddress address) {
    std::array<std::byte, 4> bytes{};
    process.GuestMemoryAccess().read(address, bytes);
    std::uint32_t result{};
    for (std::size_t i = 0; i < 4; ++i) result |= std::to_integer<std::uint32_t>(bytes[i]) << (i * 8U);
    return result;
}

ogplay::memory::GuestAddress NativeState(ogplay::runtime::AndroidGuestProcess& process,
    const std::string_view library) {
    const auto load = process.LoadApplicationModules(library, {});
    return process.FindModuleExport(load.root_module_index, "JNI_OnLoad").Subtract(0x20000U).Add(0x10250U);
}

} // namespace


TEST_CASE("APS-10 native dlopen shares mappings constructors and Java initialization") {
    using namespace ogplay;
    FixtureProcess fixture;
    const auto sequence = fixture.process->GuestMemoryAccess().allocate(4U);
    loader::ApkNativeLibraryInventory inventory{{
        Library("liba.so", NativeDlopenElf({"liba.so", "libb.so", runtime::kJniVersion1_6}, sequence)),
        Library("libb.so", NativeDlopenElf({"libb.so", "", runtime::kJniVersion1_4}, sequence, "/data/app-lib/liba.so")),
    }};
    const auto selected = loader::SelectApkNativeLibraries(inventory, loader::AndroidArmAbi::armeabi_v7a);
    runtime::NativeLibraryLoader java(*fixture.process, selected);
    const auto handle = NativeOpen(*fixture.process, "/data/app-lib/liba.so");
    REQUIRE(handle != 0U);
    CHECK(fixture.process->ApplicationModuleCount() == 2U);
    CHECK(java.Records().empty());
    CHECK(ReadNativeWord(*fixture.process, NativeState(*fixture.process, "liba.so").Add(8U)) == 0U);
    CHECK(ReadNativeWord(*fixture.process, NativeState(*fixture.process, "libb.so").Add(8U)) == 0U);
    CHECK(ReadNativeWord(*fixture.process, sequence) == 2U);
    CHECK(ReadNativeWord(*fixture.process, NativeState(*fixture.process, "libb.so")) == 1U);
    CHECK(ReadNativeWord(*fixture.process, NativeState(*fixture.process, "liba.so")) == 2U);
    CHECK(ReadNativeWord(*fixture.process, NativeState(*fixture.process, "libb.so").Add(4U)) == handle);
    CHECK(NativeOpen(*fixture.process, "liba.so") == handle);
    CHECK(ReadNativeWord(*fixture.process, sequence) == 2U);
    // The self-open in B and the explicit reopen each hold a reference.
    CHECK(NativeDlCall(*fixture.process, "dlclose", {handle, 0, 0, 0}) == 0U);
    CHECK(NativeDlCall(*fixture.process, "dlclose", {handle, 0, 0, 0}) == 0U);
    const auto java_load = java.LoadLibrary("a", 7U);
    CHECK(java_load.initialized_modules.empty());
    CHECK(java_load.jni_version == runtime::kJniVersion1_6);
    CHECK(ReadNativeWord(*fixture.process, NativeState(*fixture.process, "liba.so").Add(8U)) == 1U);
    CHECK(java.Records().at(0).jni_on_load_calls == 1U);
    CHECK(java.LoadLibrary("a", 7U).already_loaded);
    CHECK(java.Records().at(0).jni_on_load_calls == 1U);
    CHECK_THROWS_AS(static_cast<void>(java.LoadLibrary("a", 8U)), runtime::NativeLibraryLoadError);
    CHECK(NativeOpen(*fixture.process, "/data/app-lib/libb.so") != 0U);
    CHECK(java.Records().size() == 1U);
    fixture.process->GuestMemoryAccess().release(sequence, 4U);
}

TEST_CASE("APS-10 native dlopen preserves paths sources and recoverable link errors") {
    using namespace ogplay;
    FixtureProcess fixture;
    loader::ApkNativeLibraryInventory inventory{{
        Library("libbad.so", std::vector<std::byte>(4U)),
        Library("libmissing.so", AppElf({"libmissing.so", "libabsent.so", std::nullopt})),
        Library("liba.so", AppElf({"liba.so", "", runtime::kJniVersion1_6})),
    }};
    const auto selected = loader::SelectApkNativeLibraries(inventory, loader::AndroidArmAbi::armeabi_v7a);
    const auto system = AppElf({"libsys.so", "", std::nullopt});
    const runtime::BionicModuleSource systems[]{{"libsys.so", system}};
    {
        runtime::NativeLibraryLoader java(*fixture.process, selected, runtime::SelectBionicProfile(19), systems);
        CHECK(NativeOpen(*fixture.process, "/wrong/liba.so") == 0U);
        CHECK_FALSE(fixture.process->HasLoadedModule("liba.so"));
    }
    // Native sources outlive the Java registry object and own their bytes.
    CHECK(NativeOpen(*fixture.process, "liba.so") != 0U);
    CHECK(NativeOpen(*fixture.process, "/system/lib/libsys.so") != 0U);
    for (const auto name : {"libbad.so", "libmissing.so", "libabsent.so"}) {
        CAPTURE(name);
        CHECK(NativeOpen(*fixture.process, name) == 0U);
        const auto error = NativeDlCall(*fixture.process, "dlerror", {});
        REQUIRE(error != 0U);
        CHECK(ReadNativeWord(*fixture.process, memory::GuestAddress{error}) != 0U);
        CHECK(NativeDlCall(*fixture.process, "dlerror", {}) == 0U);
    }
    const auto vfs = AppElf({"libvfs.so", "", std::nullopt});
    fixture.filesystem.PutFile("/data/files/libvfs.so", vfs, false);
    CHECK(NativeOpen(*fixture.process, "/data/files/libvfs.so") != 0U);
    CHECK_FALSE(fixture.process->HasLoadedModule("libmissing.so"));
    CHECK_FALSE(fixture.process->HasLoadedModule("libbad.so"));
}

TEST_CASE("APS-10 native constructor guest exit propagates past dlerror") {
    using namespace ogplay;
    FixtureProcess fixture;
    auto image = AppElf({"libexit.so", "", std::nullopt});
    Put32(image, 0x1010U, 0xe3a07001U); // SYS_exit
    Put32(image, 0x1014U, 0xe3a00011U);
    Put32(image, 0x1018U, 0xef000000U);
    loader::ApkNativeLibraryInventory inventory{{Library("libexit.so", std::move(image))}};
    const auto selected = loader::SelectApkNativeLibraries(inventory, loader::AndroidArmAbi::armeabi_v7a);
    runtime::NativeLibraryLoader java(*fixture.process, selected);
    CHECK_THROWS_AS(static_cast<void>(NativeOpen(*fixture.process, "libexit.so")), runtime::A32GuestCallExit);
    CHECK(java.Records().empty());
}

TEST_CASE("APS-10 linker waits for another thread constructor completion") {
    using namespace ogplay;
    auto libc = LibcElf();
    const loader::Elf32ModuleInput input{"libc.so", libc, memory::GuestAddress{0x10000000U}};
    runtime::VirtualFileSystem filesystem;
    std::promise<void> constructor_entered, release_constructor, second_started;
    auto entered = constructor_entered.get_future();
    auto release = release_constructor.get_future().share();
    auto started = second_started.get_future();
    std::atomic<bool> block{false};
    runtime::AndroidGuestProcessRequest request;
    request.system_modules = std::span{&input, 1U};
    request.width = 64U; request.height = 36U;
    request.maximum_ticks_per_call = 100000U;
    request.filesystem = &filesystem;
    request.guest_call_slice_observer = [&](const std::uint64_t) {
        if (block.exchange(false)) { constructor_entered.set_value(); release.wait(); }
    };
    auto process = runtime::AndroidGuestProcess::Start(request);
    process->PrepareDexVmThread(2U, 0U);
    auto image = AppElf({"liba.so", "", runtime::kJniVersion1_6});
    Put32(image, 0x1010U, 0xe92d4080U); // push {r7, lr}
    Put32(image, 0x1014U, 0xe3a070e0U); // SYS_gettid
    Put32(image, 0x1018U, 0xef000000U);
    Put32(image, 0x101cU, 0xe8bd8080U); // pop {r7, pc}
    loader::ApkNativeLibraryInventory inventory{{Library("liba.so", std::move(image))}};
    const auto selected = loader::SelectApkNativeLibraries(inventory, loader::AndroidArmAbi::armeabi_v7a);
    runtime::NativeLibraryLoader java(*process, selected);
    block = true;
    auto first = std::async(std::launch::async, [&] { return java.LoadLibrary("a", 7U); });
    const auto ready = entered.wait_for(std::chrono::seconds(5));
    if (ready != std::future_status::ready) release_constructor.set_value();
    REQUIRE(ready == std::future_status::ready);
    auto second = std::async(std::launch::async, [&] {
        second_started.set_value();
        return NativeOpen(*process, "liba.so", 2U);
    });
    started.wait();
    CHECK(second.wait_for(std::chrono::milliseconds(50)) == std::future_status::timeout);
    release_constructor.set_value();
    REQUIRE(first.wait_for(std::chrono::seconds(5)) == std::future_status::ready);
    REQUIRE(second.wait_for(std::chrono::seconds(5)) == std::future_status::ready);
    CHECK(first.get().jni_version == runtime::kJniVersion1_6);
    CHECK(second.get() != 0U);
    CHECK(process->ApplicationModuleCount() == 1U);
    process->ReleaseDexVmThread(2U);
}

TEST_CASE("APS-10 native sources stay in selected ABI and retain constructor faults") {
    using namespace ogplay;
    FixtureProcess fixture;
    auto foreign = Library("libforeign.so", std::vector<std::byte>(4U));
    foreign.abi = loader::AndroidArmAbi::armeabi;
    foreign.entry_name = "lib/armeabi/libforeign.so";
    auto bad_ctor = AppElf({"libfault.so", "", std::nullopt});
    Put32(bad_ctor, 0x1010U, 0xe7f000f0U); // Undefined instruction.
    loader::ApkNativeLibraryInventory inventory{{std::move(foreign), Library("libfault.so", std::move(bad_ctor))}};
    const auto selected = loader::SelectApkNativeLibraries(inventory, loader::AndroidArmAbi::armeabi_v7a);
    runtime::NativeLibraryLoader java(*fixture.process, selected);
    CHECK(NativeOpen(*fixture.process, "libforeign.so") == 0U);
    CHECK_FALSE(fixture.process->HasLoadedModule("libforeign.so"));
    CHECK_THROWS(static_cast<void>(NativeOpen(*fixture.process, "libfault.so")));
    // Mapping survives guest side effects; the original ctor fault is rethrown,
    // never reported as a successfully initialized module on a later request.
    CHECK_THROWS(static_cast<void>(fixture.process->LoadApplicationModules("libfault.so", {})));
    CHECK(java.Records().empty());
}
