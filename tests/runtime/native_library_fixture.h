#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "ogplay/core/capability_ledger.h"
#include "ogplay/core/logger.h"
#include "ogplay/loader/elf.h"
#include "ogplay/runtime/integration/android_guest_call_session.h"
#include "ogplay/runtime/integration/dexvm_android.h"
#include "ogplay/runtime/integration/dexvm_bridge.h"
#include "ogplay/runtime/integration/native_library_loader.h"
#include "ogplay/runtime/vfs/vfs.h"
#include "ogplay/session/android_app_process.h"

namespace ogplay::test::native_library {

struct AppElfOptions final {
    std::string soname;
    std::string dependency;
    std::optional<std::uint32_t> jni_version;
    bool constructor{true};
    bool call_aps5_callback{};
};

void Put16(std::vector<std::byte>& bytes, const std::size_t offset,
           const std::uint16_t value);

void Put32(std::vector<std::byte>& bytes, const std::size_t offset,
           const std::uint32_t value);

[[nodiscard]] std::vector<std::byte> LibcElf();
[[nodiscard]] std::vector<std::byte> NativeActivityElf();

[[nodiscard]] std::uint32_t EnvironmentThunk(const std::string_view name);

[[nodiscard]] std::vector<std::byte> AppElf(const AppElfOptions& options);

[[nodiscard]] ogplay::loader::ApkNativeLibrary Library(
    std::string soname, std::vector<std::byte> image);

[[nodiscard]] std::vector<std::uint8_t> ReadDexFixture(
    const std::string& name);

[[nodiscard]] std::vector<std::byte> ReadPayloadBytes(
    const std::string& relative);

[[nodiscard]] ogplay::loader::AndroidManifestFacts AppManifest(
    const std::string& activity, const bool has_launcher = true);

struct FixtureProcess final {
    std::vector<std::byte> libc{LibcElf()};
    ogplay::loader::Elf32ModuleInput libc_input{
        "libc.so", libc, ogplay::memory::GuestAddress{0x10000000U}};
    ogplay::runtime::VirtualFileSystem filesystem;
    std::unique_ptr<ogplay::runtime::AndroidGuestProcess> process{
        ogplay::runtime::AndroidGuestProcess::Start(
            {19, std::span{&libc_input, 1}, {}, 64, 36,
             UINT64_C(100000), 1, &filesystem, {}})};
};

struct ApplicationProcess final {
    std::vector<std::byte> libc{LibcElf()};
    std::vector<std::byte> native_a{
        AppElf({"liba.so", "libc.so", ogplay::runtime::kJniVersion1_6})};
    ogplay::runtime::VirtualFileSystem filesystem;
    std::unique_ptr<ogplay::runtime::AndroidGuestCallSession> session;
    std::unique_ptr<ogplay::loader::ApkNativeLibraryInventory> inventory;
    std::unique_ptr<ogplay::loader::ApkSelectedNativeLibraries> selected;
    std::unique_ptr<ogplay::runtime::NativeLibraryLoader> libraries;
    std::shared_ptr<ogplay::runtime::DexVmAndroidContext> context;
    ogplay::core::CapabilityLedger ledger;
    std::unique_ptr<ogplay::runtime::DexVmGuestBridge> bridge;
    std::size_t globals_before_bridge{};
    ogplay::core::Logger logger;

    explicit ApplicationProcess(
        ogplay::runtime::dexvm::InterpreterBackend backend =
            ogplay::runtime::dexvm::InterpreterBackend::switch_dispatch,
        const std::optional<std::string_view> working_directory = std::nullopt,
        const std::string& dex_fixture = "application.dex",
        const ogplay::runtime::dexvm::JavaObjectModelConfig heap = {},
        std::vector<std::uint8_t> skeleton = {});

    ~ApplicationProcess();

    [[nodiscard]] std::int32_t CallStaticInt(
        const std::string& owner, const std::string& name);
};

struct OrchestratedApp final {
    std::vector<std::byte> libc{LibcElf()};
    std::vector<std::byte> native_a{
        AppElf({"liba.so", "libc.so", ogplay::runtime::kJniVersion1_6})};
    ogplay::runtime::VirtualFileSystem filesystem;
    std::shared_ptr<ogplay::runtime::DexVmAndroidContext> context{
        std::make_shared<ogplay::runtime::DexVmAndroidContext>()};
    ogplay::core::CapabilityLedger ledger;
    ogplay::core::Logger logger;
    std::unique_ptr<ogplay::session::AndroidAppProcess> app;

    explicit OrchestratedApp(const std::string& activity,
                             const bool has_launcher = true,
                             const bool with_native = true,
                             const std::string& launcher_alias = {},
                             const std::vector<ogplay::loader::AndroidManifestServiceComponent>& services = {},
                             const bool application_enabled = true,
                             const ogplay::runtime::dexvm::InterpreterBackend
                                 interpreter_backend =
                                     ogplay::runtime::dexvm::InterpreterBackend::switch_dispatch,
                             const std::string& application_class = "android.app.Application",
                             std::function<void()> pump_host_events = {},
                             ogplay::runtime::ui::UiMetrics viewport = {64, 36},
                             std::vector<std::string> gl_textures = {});

    [[nodiscard]] std::int32_t CallStaticInt(
        const std::string& owner, const std::string& name);
};

} // namespace ogplay::test::native_library
