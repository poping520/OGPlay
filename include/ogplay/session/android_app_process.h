#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "ogplay/loader/apk_manifest.h"
#include "ogplay/loader/apk_native.h"
#include "ogplay/runtime/bionic/bionic_module_set.h"
#include "ogplay/runtime/integration/android_guest_call_session.h"
#include "ogplay/runtime/integration/dexvm_android.h"
#include "ogplay/runtime/integration/dexvm_bridge.h"
#include "ogplay/runtime/integration/native_library_loader.h"
#include "ogplay/session/dex_activity_lifecycle.h"

namespace ogplay::session {

enum class AndroidAppProcessState : std::uint8_t {
    created,
    package_ready,
    native_process_ready,
    dex_vm_ready,
    application_started,
    activity_resumed,
    stopped,
};

struct AndroidAppProcessHost final {
    std::function<void()> flush_persistent_state;
    std::function<void()> before_process_stop;
};

struct AndroidAppProcessRequest final {
    loader::AndroidManifestFacts manifest;
    std::vector<loader::ApkNativeLibrary> native_libraries;
    std::span<const runtime::BionicModuleSource> system_libraries;
    std::vector<std::uint8_t> dex_bytes;
    std::shared_ptr<runtime::DexVmAndroidContext> context;
    std::optional<std::string> launcher_override;
    std::uint32_t api_level{19};
    std::uint32_t surface_width{800};
    std::uint32_t surface_height{480};
    std::uint64_t maximum_ticks_per_call{UINT64_C(200000000)};
    std::uint32_t supersample_factor{1};
    gles::AngleBackend backend;
    runtime::VirtualFileSystem* filesystem{};
    std::function<void(std::string_view)> progress;
    runtime::AndroidBoundaryOptions boundary_options;
    audio::JavaSoundPoolMixer::EncodedResourceLoader sound_resource_loader;
    runtime::A32GuestCallSliceObserver guest_call_slice_observer;
    runtime::AndroidGuestPlatformConfig platform;
    runtime::DexVmBridgeConfig dexvm;
    core::CapabilityLedger* ledger{};
    core::Logger* logger{};
    std::function<void(runtime::dexvm::Interpreter&)> configure_dex_vm;
    AndroidAppProcessHost host;
    runtime::GuestProcFacts proc_facts{};
};

class AndroidAppProcessError final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Owns the complete generic APK startup chain. Create prepares the rootless
// native shell, dynamic loader and DexVM; Java execution remains explicit.
class AndroidAppProcess final {
public:
    [[nodiscard]] static std::unique_ptr<AndroidAppProcess> Create(
        AndroidAppProcessRequest request);
    ~AndroidAppProcess();
    AndroidAppProcess(const AndroidAppProcess&) = delete;
    AndroidAppProcess& operator=(const AndroidAppProcess&) = delete;

    void StartApplication();
    [[nodiscard]] LifecycleFrameState StartLauncherActivity();
    [[nodiscard]] LifecycleFrameState Stop();

    [[nodiscard]] AndroidAppProcessState State() const noexcept;
    [[nodiscard]] std::optional<loader::AndroidArmAbi> SelectedAbi() const noexcept;
    [[nodiscard]] runtime::AndroidGuestProcess& NativeProcess() noexcept;
    [[nodiscard]] runtime::DexVmGuestBridge& DexVm() noexcept;
    [[nodiscard]] DexActivityLifecycle& ActivityLifecycle() noexcept;
    [[nodiscard]] runtime::NativeLibraryLoader* NativeLibraries() noexcept;
    [[nodiscard]] const std::shared_ptr<runtime::DexVmAndroidContext>& Context()
        const noexcept;

private:
    class Impl;
    explicit AndroidAppProcess(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> impl_;
};

}  // namespace ogplay::session
