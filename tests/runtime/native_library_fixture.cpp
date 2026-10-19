#include "native_library_fixture.h"

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

namespace ogplay::test::native_library {

[[nodiscard]] std::vector<std::byte> NativeActivityElf() {
    auto bytes = AppElf({"liba.so", "libc.so", 0U, false});
    constexpr char strings[] = "\0liba.so\0libc.so\0ANativeActivity_onCreate\0free\0";
    for (std::size_t i=0; i<sizeof(strings); ++i) bytes[0x160+i] = static_cast<std::byte>(strings[i]);
    Put32(bytes, 0x10c, sizeof(strings));
    Put32(bytes, 0x194, 3); // two chained exports
    Put32(bytes, 0x1a0, 2);
    Put32(bytes, 0x1d0, 42); Put32(bytes, 0x1d4, 0x20114); Put32(bytes, 0x1d8, 12);
    bytes[0x1dc] = std::byte{0x12}; Put16(bytes, 0x1de, 1);
    Put32(bytes, 0x1c0, 17);
    constexpr std::array code{
        0xe92d4010U, 0xe1a04000U, 0xe28410c0U, 0xe584101cU, 0xe5943000U, 0xe28f203cU,
        0xe3a01000U, 0xe7832101U, 0xe2822008U, 0xe2811001U, 0xe3510010U, 0x1afffffaU,
        0xe3a01000U, 0xe5831008U, 0xe5940008U, 0xe594100cU, 0xe1a02001U, 0xe5903000U,
        0xe5933060U, 0xe12fff33U, 0xe584006cU, 0xe8bd8010U, 0xe3a02000U, 0xea00001dU,
        0xe3a02001U, 0xea00001bU, 0xe3a02002U, 0xea000019U, 0xe3a02003U, 0xea000017U,
        0xe3a02004U, 0xea000015U, 0xe3a02005U, 0xea000013U, 0xe3a02006U, 0xea000011U,
        0xe3a02007U, 0xea00000fU, 0xe3a02008U, 0xea00000dU, 0xe3a02009U, 0xea00000bU,
        0xe3a0200aU, 0xea000009U, 0xe3a0200bU, 0xea000007U, 0xe3a0200cU, 0xea000005U,
        0xe3a0200dU, 0xea000003U, 0xe3a0200eU, 0xea000001U, 0xe3a0200fU, 0xeaffffffU,
        0xe590301cU, 0xe593c000U, 0xe28cc001U, 0xe583c000U, 0xe5832004U, 0xe5831008U,
        0xe12fff1eU,
    };
    for (std::size_t i=0; i<code.size(); ++i) Put32(bytes, 0x1000+i*4, code[i]);
    bytes.resize(0x1120);
    Put32(bytes, 100, 0x120); Put32(bytes, 104, 0x120);
    constexpr std::array state_code{0xe590301cU, 0xe593000cU, 0xe5933010U, 0xe5813000U, 0xe12fff1eU,
                                     0xe3a01000U, 0xe5801000U, 0xe12fff1eU};
    for (std::size_t i=0; i<state_code.size(); ++i) Put32(bytes, 0x1100+i*4, state_code[i]);
    return bytes;
}

void Put16(std::vector<std::byte>& bytes, const std::size_t offset,
           const std::uint16_t value) {
    bytes[offset] = static_cast<std::byte>(value & 0xffU);
    bytes[offset + 1] = static_cast<std::byte>(value >> 8U);
}

void Put32(std::vector<std::byte>& bytes, const std::size_t offset,
           const std::uint32_t value) {
    for (std::size_t index = 0; index < 4; ++index) {
        bytes[offset + index] = static_cast<std::byte>(
            (value >> static_cast<unsigned>(index * 8U)) & 0xffU);
    }
}

void PutDynamic(std::vector<std::byte>& bytes, std::size_t& offset,
                const std::int32_t tag, const std::uint32_t value) {
    Put32(bytes, offset, static_cast<std::uint32_t>(tag));
    Put32(bytes, offset + 4U, value);
    offset += 8U;
}

[[nodiscard]] std::vector<std::byte> LibcElf() {
    std::vector<std::byte> bytes(0x300, std::byte{});
    bytes[0] = std::byte{0x7f};
    bytes[1] = std::byte{'E'};
    bytes[2] = std::byte{'L'};
    bytes[3] = std::byte{'F'};
    bytes[4] = std::byte{1};
    bytes[5] = std::byte{1};
    bytes[6] = std::byte{1};
    Put16(bytes, 16, 3);
    Put16(bytes, 18, 40);
    Put32(bytes, 20, 1);
    Put32(bytes, 28, 52);
    Put32(bytes, 36, 0x05000400U);
    Put16(bytes, 40, 52);
    Put16(bytes, 42, 32);
    Put16(bytes, 44, 2);
    Put32(bytes, 52, ogplay::loader::kElfProgramLoad);
    Put32(bytes, 60, 0x10000U);
    Put32(bytes, 68, 0x300);
    Put32(bytes, 72, 0x300);
    Put32(bytes, 76, 6);
    Put32(bytes, 80, 0x1000);
    Put32(bytes, 84, ogplay::loader::kElfProgramDynamic);
    Put32(bytes, 88, 0x100);
    Put32(bytes, 92, 0x10100U);
    Put32(bytes, 100, 56);
    Put32(bytes, 104, 56);
    Put32(bytes, 108, 6);
    Put32(bytes, 112, 4);
    std::size_t dynamic = 0x100;
    PutDynamic(bytes, dynamic, ogplay::loader::kElfDynamicStringTable,
               0x10160U);
    PutDynamic(bytes, dynamic, ogplay::loader::kElfDynamicStringTableSize,
               34);
    PutDynamic(bytes, dynamic, ogplay::loader::kElfDynamicSoname, 1);
    PutDynamic(bytes, dynamic, ogplay::loader::kElfDynamicHash, 0x10190U);
    PutDynamic(bytes, dynamic, ogplay::loader::kElfDynamicSymbolTable,
               0x101b0U);
    PutDynamic(bytes, dynamic, ogplay::loader::kElfDynamicSymbolEntrySize,
               16);
    const char strings[] = "\0libc.so\0__system_property_area__\0";
    for (std::size_t index = 0; index < sizeof(strings); ++index) {
        bytes[0x160 + index] = static_cast<std::byte>(strings[index]);
    }
    Put32(bytes, 0x190, 1);
    Put32(bytes, 0x194, 2);
    Put32(bytes, 0x198, 1);
    Put32(bytes, 0x19c, 0);
    Put32(bytes, 0x1c0, 9);
    Put32(bytes, 0x1c4, 0x10200U);
    Put32(bytes, 0x1c8, 4);
    bytes[0x1cc] = std::byte{0x11};
    Put16(bytes, 0x1ce, 1);
    return bytes;
}

[[nodiscard]] std::uint32_t EnvironmentThunk(const std::string_view name) {
    const auto slot = ogplay::runtime::FindJniSlot(name);
    if (!slot.has_value()) throw std::runtime_error("missing JNI slot");
    return ogplay::runtime::kJniThunkBegin +
           static_cast<std::uint32_t>(slot->Value()) * 4U + 1U;
}

[[nodiscard]] std::uint32_t JavaVmThunk(const std::string_view name) {
    const auto slot = ogplay::runtime::FindJniInvokeSlot(name);
    if (!slot.has_value()) throw std::runtime_error("missing JavaVM slot");
    return ogplay::runtime::kJniInvokeThunkBegin +
           static_cast<std::uint32_t>(slot->Value()) * 4U + 1U;
}

void WriteAps5OnLoad(std::vector<std::byte>& bytes,
                     const std::uint32_t version) {
    constexpr std::size_t kCode = 0x1000U;
    const std::array code{
        0xe92d40f0U, 0xe24dd00cU, 0xe1a04000U, 0xe1a00004U,
        0xe28d1000U, 0xe59f2064U, 0xe59fc064U, 0xe12fff3cU,
        0xe59d4000U, 0xe1a00004U, 0xe28f1068U, 0xe59fc054U,
        0xe12fff3cU, 0xe1a05000U, 0xe1a00004U, 0xe1a01005U,
        0xe28f2060U, 0xe28f3068U, 0xe59fc03cU, 0xe12fff3cU,
        0xe1a06000U, 0xe1a00004U, 0xe1a01005U, 0xe1a02006U,
        0xe59fc028U, 0xe12fff3cU, 0xe59f0010U, 0xe28dd00cU,
        0xe8bd80f0U,
    };
    for (std::size_t index = 0; index < code.size(); ++index) {
        Put32(bytes, kCode + index * 4U, code[index]);
    }
    Put32(bytes, kCode + 0x80U, version);
    Put32(bytes, kCode + 0x84U, JavaVmThunk("GetEnv"));
    Put32(bytes, kCode + 0x88U, EnvironmentThunk("FindClass"));
    Put32(bytes, kCode + 0x8cU, EnvironmentThunk("GetStaticMethodID"));
    Put32(bytes, kCode + 0x90U,
          EnvironmentThunk("CallStaticVoidMethod"));
    const auto put_string = [&bytes](const std::size_t offset,
                                     const std::string_view value) {
        for (std::size_t index = 0; index < value.size(); ++index) {
            bytes[offset + index] = static_cast<std::byte>(value[index]);
        }
        bytes[offset + value.size()] = std::byte{};
    };
    put_string(kCode + 0x98U, "fixture/Aps5");
    put_string(kCode + 0xa8U, "callback");
    put_string(kCode + 0xb4U, "()V");
}

[[nodiscard]] std::vector<std::byte> AppElf(const AppElfOptions& options) {
    std::vector<std::byte> bytes(0x1100, std::byte{});
    bytes[0] = std::byte{0x7f};
    bytes[1] = std::byte{'E'};
    bytes[2] = std::byte{'L'};
    bytes[3] = std::byte{'F'};
    bytes[4] = std::byte{1};
    bytes[5] = std::byte{1};
    bytes[6] = std::byte{1};
    Put16(bytes, 16, 3);
    Put16(bytes, 18, 40);
    Put32(bytes, 20, 1);
    Put32(bytes, 28, 52);
    Put32(bytes, 36, 0x05000400U);
    Put16(bytes, 40, 52);
    Put16(bytes, 42, 32);
    Put16(bytes, 44, 3);
    Put32(bytes, 52, ogplay::loader::kElfProgramLoad);
    Put32(bytes, 60, 0x10000U);
    Put32(bytes, 68, 0x300);
    Put32(bytes, 72, 0x300);
    Put32(bytes, 76, 6);
    Put32(bytes, 80, 0x1000);
    Put32(bytes, 84, ogplay::loader::kElfProgramLoad);
    Put32(bytes, 88, 0x1000);
    Put32(bytes, 92, 0x20000U);
    Put32(bytes, 100, 0x100);
    Put32(bytes, 104, 0x100);
    Put32(bytes, 108, 5);
    Put32(bytes, 112, 0x1000);
    Put32(bytes, 116, ogplay::loader::kElfProgramDynamic);
    Put32(bytes, 120, 0x100);
    Put32(bytes, 124, 0x10100U);
    Put32(bytes, 132, 0x80);
    Put32(bytes, 136, 0x80);
    Put32(bytes, 140, 6);
    Put32(bytes, 144, 4);

    std::string strings(1, '\0');
    const auto soname_offset = static_cast<std::uint32_t>(strings.size());
    strings += options.soname;
    strings.push_back('\0');
    std::optional<std::uint32_t> dependency_offset;
    if (!options.dependency.empty()) {
        dependency_offset = static_cast<std::uint32_t>(strings.size());
        strings += options.dependency;
        strings.push_back('\0');
    }
    std::optional<std::uint32_t> jni_name_offset;
    if (options.jni_version.has_value()) {
        jni_name_offset = static_cast<std::uint32_t>(strings.size());
        strings += "JNI_OnLoad";
        strings.push_back('\0');
    }
    if (strings.size() > 0x30U) {
        throw std::runtime_error("application ELF string table is too large");
    }
    for (std::size_t index = 0; index < strings.size(); ++index) {
        bytes[0x160 + index] = static_cast<std::byte>(strings[index]);
    }

    std::size_t dynamic = 0x100;
    PutDynamic(bytes, dynamic, ogplay::loader::kElfDynamicStringTable,
               0x10160U);
    PutDynamic(bytes, dynamic, ogplay::loader::kElfDynamicStringTableSize,
               static_cast<std::uint32_t>(strings.size()));
    PutDynamic(bytes, dynamic, ogplay::loader::kElfDynamicSoname,
               soname_offset);
    if (dependency_offset.has_value()) {
        PutDynamic(bytes, dynamic, ogplay::loader::kElfDynamicNeeded,
                   *dependency_offset);
    }
    PutDynamic(bytes, dynamic, ogplay::loader::kElfDynamicHash, 0x10190U);
    PutDynamic(bytes, dynamic, ogplay::loader::kElfDynamicSymbolTable,
               0x101b0U);
    PutDynamic(bytes, dynamic, ogplay::loader::kElfDynamicSymbolEntrySize,
               16);
    if (options.constructor) {
        PutDynamic(bytes, dynamic, ogplay::loader::kElfDynamicInit,
                   options.call_aps5_callback ? 0x200f0U : 0x20010U);
    }
    Put32(bytes, 0x190, 1);
    Put32(bytes, 0x194, options.jni_version.has_value() ? 2U : 1U);
    if (options.jni_version.has_value()) {
        Put32(bytes, 0x198, 1);
        Put32(bytes, 0x19c, 0);
        Put32(bytes, 0x1c0, *jni_name_offset);
        Put32(bytes, 0x1c4, 0x20000U);
        Put32(bytes, 0x1c8, 8);
        bytes[0x1cc] = std::byte{0x12};
        Put16(bytes, 0x1ce, 1);
        if (options.call_aps5_callback) {
            WriteAps5OnLoad(bytes, *options.jni_version);
        } else {
            Put32(bytes, 0x1000, 0xe59f0000U);
            Put32(bytes, 0x1004, 0xe12fff1eU);
            Put32(bytes, 0x1008, *options.jni_version);
        }
    }
    Put32(bytes, options.call_aps5_callback ? 0x10f0U : 0x1010U,
          0xe12fff1eU);
    return bytes;
}

[[nodiscard]] ogplay::loader::ApkNativeLibrary Library(
    std::string soname, std::vector<std::byte> image) {
    const auto logical = soname.substr(3U, soname.size() - 6U);
    return {"lib/armeabi-v7a/" + soname, soname,
            ogplay::loader::AndroidArmAbi::armeabi_v7a, "fixture",
            std::move(image), soname, logical};
}

[[nodiscard]] std::vector<std::uint8_t> ReadDexFixture(
    const std::string& name) {
    const std::string path =
        std::string(OGPLAY_DEXVM_FIXTURE_DIR) + "/" + name;
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("missing DEX fixture: " + path);
    return {std::istreambuf_iterator<char>(stream),
            std::istreambuf_iterator<char>()};
}

[[nodiscard]] std::vector<std::byte> ReadPayloadBytes(
    const std::string& relative) {
    std::ifstream stream(std::string(OGPLAY_SOURCE_DIR) +
                             "/data/android/19/" + relative,
                         std::ios::binary);
    if (!stream) throw std::runtime_error("missing payload file: " + relative);
    const std::vector<char> raw{std::istreambuf_iterator<char>(stream), {}};
    std::vector<std::byte> result(raw.size());
    std::transform(raw.begin(), raw.end(), result.begin(),
                   [](char value) { return static_cast<std::byte>(value); });
    return result;
}

[[nodiscard]] ogplay::loader::AndroidManifestFacts AppManifest(
    const std::string& activity, const bool has_launcher) {
    ogplay::loader::AndroidManifestFacts manifest;
    manifest.package = "fixture";
    manifest.version_code = 1;
    manifest.application_class = "android.app.Application";
    if (has_launcher) {
        manifest.activity_components.push_back({
            ogplay::loader::AndroidManifestComponentKind::activity,
            activity, std::nullopt, true,
            {{{"android.intent.action.MAIN"},
              {"android.intent.category.LAUNCHER"}}}});
    }
    return manifest;
}

ApplicationProcess::ApplicationProcess(
    ogplay::runtime::dexvm::InterpreterBackend backend,
    const std::optional<std::string_view> working_directory,
    const std::string& dex_fixture,
    const ogplay::runtime::dexvm::JavaObjectModelConfig heap) {
    if (working_directory.has_value()) {
        std::string directory;
        for (const auto& component : std::filesystem::path(*working_directory)) {
            if (component == "/") continue;
            directory += "/" + component.generic_string();
            filesystem.CreateDirectory(directory);
        }
        filesystem.SetWorkingDirectory(*working_directory);
    }
    const std::array inputs{
        ogplay::loader::Elf32ModuleInput{
            "liba.so", native_a,
            ogplay::memory::GuestAddress{0x20000000U}},
        ogplay::loader::Elf32ModuleInput{
            "libc.so", libc,
            ogplay::memory::GuestAddress{0x10000000U}},
    };
    session = ogplay::runtime::AndroidGuestCallSession::Start(
        {19, "liba.so", inputs, {}, 64, 36,
         UINT64_C(200000), 1, &filesystem, {}});
    inventory =
        std::make_unique<ogplay::loader::ApkNativeLibraryInventory>(
            std::vector<ogplay::loader::ApkNativeLibrary>{
                Library("liba.so", native_a)});
    selected =
        std::make_unique<ogplay::loader::ApkSelectedNativeLibraries>(
            ogplay::loader::SelectApkNativeLibraries(
                *inventory,
                ogplay::loader::AndroidArmAbi::armeabi_v7a));
    libraries = std::make_unique<ogplay::runtime::NativeLibraryLoader>(
        session->Process(), *selected);
    context =
        std::make_shared<ogplay::runtime::DexVmAndroidContext>();
    context->session = session.get();
    context->native_libraries = libraries.get();
    if (working_directory.has_value()) context->vfs = &filesystem;
    auto catalog = ogplay::runtime::AndroidIntrinsicCatalog(context);
    globals_before_bridge = session->Environment().GlobalReferenceCount();
    bridge = std::make_unique<ogplay::runtime::DexVmGuestBridge>(
        *session, ReadDexFixture(dex_fixture), catalog, context,
        ledger, &logger, ogplay::runtime::DexVmBridgeConfig{
            .interpreter = {.backend = backend}, .heap = heap}, ogplay::test::ReadBootDex());
    context->threads = &bridge->Threads();
}

ApplicationProcess::~ApplicationProcess() {
    bridge.reset();
    if (session && session->Running()) session->Stop();
}

std::int32_t ApplicationProcess::CallStaticInt(
    const std::string& owner, const std::string& name) {
    const auto java_class = bridge->Linker().FindClass(owner);
    if (!java_class.has_value()) {
        throw std::runtime_error("fixture class is not linked");
    }
    const auto method = bridge->Linker().FindDirectMethod(
        *java_class, name, "()I");
    if (!method.has_value()) {
        throw std::runtime_error("fixture method is not linked");
    }
    const auto outcome = bridge->Vm().Call(*method, {});
    if (outcome.exception.IsValid()) {
        throw std::runtime_error(outcome.exception_message);
    }
    return outcome.value.AsInt();
}

OrchestratedApp::OrchestratedApp(const std::string& activity,
                         const bool has_launcher,
                         const bool with_native,
                         const std::string& launcher_alias,
                         const std::vector<ogplay::loader::AndroidManifestServiceComponent>& services,
                         const bool application_enabled,
                         const ogplay::runtime::dexvm::InterpreterBackend
                             interpreter_backend,
                         const std::string& application_class,
                         std::function<void()> pump_host_events,
                         ogplay::runtime::ui::UiMetrics viewport,
                         std::vector<std::string> gl_textures) {
    if (activity == "android.app.NativeActivity" || activity == "fixture.NativeTakeoverActivity") native_a = NativeActivityElf();
    context->apk_bytes = {
        std::byte{0x50}, std::byte{0x4b}, std::byte{0x03}, std::byte{0x04}};
    const ogplay::runtime::BionicModuleSource system{
        "libc.so", libc};
    std::vector<ogplay::loader::ApkNativeLibrary> libraries;
    if (with_native) libraries.push_back(Library("liba.so", native_a));
    ogplay::session::AndroidAppProcessRequest request;
    request.manifest = AppManifest(activity, has_launcher);
    request.manifest.supported_gl_textures = std::move(gl_textures);
    // Handoff fixtures must declare their destination, like a sealed APK.
    if (activity == "fixture.WindowFocusActivity" ||
        activity == "fixture.TaskRootLauncherActivity") {
        const auto target = activity == "fixture.WindowFocusActivity"
            ? "fixture.WindowFocusSecondActivity" : "fixture.TaskRootChildActivity";
        request.manifest.activity_components.push_back({
            ogplay::loader::AndroidManifestComponentKind::activity,
            target, std::nullopt, true, {}});
    }
    if (activity == "fixture.ResultCaller" || activity == "fixture.ResultOnStartCaller") {
        request.manifest.activity_components.push_back({
            ogplay::loader::AndroidManifestComponentKind::activity,
            "fixture.ResultChild", std::nullopt, true, {}});
    }
    if (activity == "fixture.ResultOnStartCaller") {
        request.manifest.activity_components.push_back({
            ogplay::loader::AndroidManifestComponentKind::activity,
            "fixture.ResultImmediateChild", std::nullopt, true, {}});
    }
    if (activity == "android.app.NativeActivity" || activity == "fixture.NativeTakeoverActivity") {
        request.manifest.activity_components.front().meta_data.push_back({"android.app.lib_name", std::string("a")});
        request.manifest.application_meta_data.push_back({"android.app.lib_name", std::string("wrong_application_library")});
    }
    request.manifest.application_class = application_class;
    request.manifest.service_components = services;
    request.manifest.application_enabled = application_enabled;
    if (!launcher_alias.empty()) {
        auto filters = request.manifest.activity_components.front().intent_filters;
        request.manifest.activity_components.front().intent_filters.clear();
        request.manifest.activity_components.push_back(
            {ogplay::loader::AndroidManifestComponentKind::activity_alias,
             launcher_alias, activity, true, std::move(filters)});
    }
    request.native_libraries = std::move(libraries);
    request.system_libraries = std::span{&system, 1};
    request.dex_bytes = ReadDexFixture("application.dex");
    request.boot_dex_bytes = ogplay::test::ReadBootDex();
    request.context = context;
    request.surface_width = static_cast<std::uint32_t>(viewport.width);
    request.surface_height = static_cast<std::uint32_t>(viewport.height);
    request.maximum_ticks_per_call = UINT64_C(200000);
#if defined(_WIN32)
    request.backend = {ogplay::gles::AngleRenderer::d3d11,
                       ogplay::gles::AngleDevice::hardware};
#elif defined(__APPLE__)
    request.backend = {ogplay::gles::AngleRenderer::metal,
                       ogplay::gles::AngleDevice::hardware};
#else
    request.backend = {ogplay::gles::AngleRenderer::vulkan,
                       ogplay::gles::AngleDevice::hardware};
#endif
    request.filesystem = &filesystem;
    request.platform.android_id = "0123456789abcdef";
    request.ledger = &ledger;
    request.logger = &logger;
    request.dexvm.interpreter.backend = interpreter_backend;
    request.host.pump_host_events = std::move(pump_host_events);
    app = ogplay::session::AndroidAppProcess::Create(
        std::move(request));
}

std::int32_t OrchestratedApp::CallStaticInt(
    const std::string& owner, const std::string& name) {
    auto& bridge = app->DexVm();
    const auto java_class = bridge.Linker().FindClass(owner);
    if (!java_class.has_value()) {
        throw std::runtime_error("fixture class is not linked");
    }
    const auto method = bridge.Linker().FindDirectMethod(
        *java_class, name, "()I");
    if (!method.has_value()) {
        throw std::runtime_error("fixture method is not linked");
    }
    const auto outcome = bridge.Vm().Call(*method, {});
    if (outcome.exception.IsValid()) {
        throw std::runtime_error(outcome.exception_message);
    }
    return outcome.value.AsInt();
}

} // namespace ogplay::test::native_library
