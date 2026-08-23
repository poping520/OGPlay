#include "ogplay/session/android_app_process.h"

#include <algorithm>
#include <utility>

#include "ogplay/runtime/bionic/bionic_profile.h"

namespace ogplay::session {
namespace {

[[noreturn]] void Fail(const std::string& message) {
    throw AndroidAppProcessError(message);
}

[[nodiscard]] std::string Descriptor(std::string name) {
    if (name.empty()) Fail("Android app process class name is empty");
    std::replace(name.begin(), name.end(), '.', '/');
    return "L" + name + ";";
}

struct SystemModulePlan final {
    runtime::BionicModuleSet modules;
    std::vector<loader::Elf32ModuleInput> inputs;
};

[[nodiscard]] SystemModulePlan BuildSystemModules(
    const std::uint32_t api_level,
    const std::span<const runtime::BionicModuleSource> sources) {
    const auto libc = std::find_if(
        sources.begin(), sources.end(), [](const auto& source) {
            return source.name == "libc.so";
        });
    if (libc == sources.end()) {
        Fail("Android app process requires the API 19 libc.so source");
    }
    std::vector<runtime::BionicModuleSource> dependencies;
    dependencies.reserve(sources.size() - 1U);
    for (const auto& source : sources) {
        if (source.name != "libc.so") dependencies.push_back(source);
    }
    auto modules = runtime::BuildBionicModuleSet(
        runtime::SelectBionicProfile(api_level), "libc.so", libc->image,
        dependencies);
    auto inputs = modules.Inputs();
    return {std::move(modules), std::move(inputs)};
}

}  // namespace

class AndroidAppProcess::Impl final {
public:
    explicit Impl(AndroidAppProcessRequest request)
        : manifest(std::move(request.manifest)),
          inventory(std::move(request.native_libraries)),
          context(std::move(request.context)),
          host(std::move(request.host)) {
        if (request.api_level != 19 || request.dex_bytes.empty() ||
            !context || request.filesystem == nullptr ||
            request.surface_width == 0 || request.surface_height == 0 ||
            request.maximum_ticks_per_call == 0 || request.ledger == nullptr) {
            Fail("Android app process request is incomplete");
        }
        state = AndroidAppProcessState::package_ready;
        const auto launcher = loader::ResolveLauncherComponent(manifest);
        launcher_descriptor = Descriptor(
            request.launcher_override.value_or(launcher.activity_class));
        application_descriptor = Descriptor(manifest.application_class);

        auto system = BuildSystemModules(request.api_level,
                                         request.system_libraries);
        request.boundary_options.logger = request.logger;
        auto native_process = runtime::AndroidGuestProcess::Start(
            {request.api_level, system.inputs, request.backend,
             request.surface_width, request.surface_height,
             request.maximum_ticks_per_call, request.supersample_factor,
             request.filesystem, std::move(request.progress), std::nullopt,
             request.boundary_options,
             std::move(request.sound_resource_loader),
             std::move(request.guest_call_slice_observer),
             std::move(request.platform)});
        session = runtime::AndroidGuestCallSession::AdoptProcess(
            std::move(native_process));
        state = AndroidAppProcessState::native_process_ready;

        if (!inventory.Empty()) {
            selected_abi = loader::ResolveApkProcessAbi(inventory);
            selected = std::make_unique<loader::ApkSelectedNativeLibraries>(
                inventory, *selected_abi);
            native_libraries =
                std::make_unique<runtime::NativeLibraryLoader>(
                    session->Process(), *selected,
                    runtime::SelectBionicProfile(request.api_level),
                    request.system_libraries, request.logger);
        }
        context->session = session.get();
        context->native_libraries = native_libraries.get();
        context->package_name = manifest.package;
        context->package_version_code = manifest.version_code;
        context->package_version_name = manifest.version_name.value_or("");
        context->target_sdk_version = manifest.target_sdk.value_or(0U);
        context->application_class_name = manifest.application_class;
        context->application_label = manifest.application_label;
        context->application_icon = manifest.application_icon.value_or(0U);
        context->application_meta_data.clear();
        for (const auto& item : manifest.application_meta_data) {
            context->application_meta_data.emplace(item.name, item.value);
        }
        context->requested_permissions = manifest.requested_permissions;
        context->granted_permissions.insert(manifest.requested_permissions.begin(),
                                            manifest.requested_permissions.end());
        context->system_features.insert("android.hardware.touchscreen");
        context->system_features.insert(
            request.surface_width >= request.surface_height
                ? "android.hardware.screen.landscape"
                : "android.hardware.screen.portrait");
        context->surface_width = request.surface_width;
        context->surface_height = request.surface_height;
        context->api_level = static_cast<std::int32_t>(request.api_level);
        context->vfs = request.filesystem;

        const auto catalog = runtime::AndroidIntrinsicCatalog(context);
        bridge = std::make_unique<runtime::DexVmGuestBridge>(
            *session, std::move(request.dex_bytes), catalog, context,
            *request.ledger, request.logger, request.dexvm);
        context->threads = &bridge->Threads();
        if (request.configure_dex_vm) {
            request.configure_dex_vm(bridge->Vm());
        }

        DexActivityLifecycleBindings bindings;
        bindings.bridge = bridge.get();
        bindings.context = context;
        bindings.launcher_descriptor = launcher_descriptor;
        bindings.application_descriptor = application_descriptor;
        bindings.open_surface = [this] { session->OpenManagedSurface(); };
        bindings.present_surface = [this] {
            session->PresentManagedSurface();
        };
        bindings.publish_video_frame = [this](std::vector<std::uint8_t> rgba8) {
            session->PublishSoftwareFrame(std::move(rgba8));
        };
        bindings.interrupt_guest_waits = [this] {
            static_cast<void>(session->InterruptBlockingWaits());
        };
        bindings.finalize_guest = [this] {
            if (host.before_process_stop) host.before_process_stop();
            session->Stop();
        };
        bindings.close_surface = [this] { session->CloseManagedSurface(); };
        bindings.flush_persistent_state =
            std::move(host.flush_persistent_state);
        bindings.release_surface_currency = [this] {
            session->ReleaseManagedSurfaceFromCallingThread();
        };
        lifecycle =
            std::make_unique<DexActivityLifecycle>(std::move(bindings));
        state = AndroidAppProcessState::dex_vm_ready;
    }

    loader::AndroidManifestFacts manifest;
    loader::ApkNativeLibraryInventory inventory;
    std::optional<loader::AndroidArmAbi> selected_abi;
    std::unique_ptr<loader::ApkSelectedNativeLibraries> selected;
    std::unique_ptr<runtime::AndroidGuestCallSession> session;
    std::unique_ptr<runtime::NativeLibraryLoader> native_libraries;
    std::shared_ptr<runtime::DexVmAndroidContext> context;
    std::unique_ptr<runtime::DexVmGuestBridge> bridge;
    std::unique_ptr<DexActivityLifecycle> lifecycle;
    AndroidAppProcessHost host;
    std::string application_descriptor;
    std::string launcher_descriptor;
    AndroidAppProcessState state{AndroidAppProcessState::created};
};

std::unique_ptr<AndroidAppProcess> AndroidAppProcess::Create(
    AndroidAppProcessRequest request) {
    try {
        return std::unique_ptr<AndroidAppProcess>(
            new AndroidAppProcess(
                std::make_unique<Impl>(std::move(request))));
    } catch (const AndroidAppProcessError&) {
        throw;
    } catch (const std::exception& error) {
        throw AndroidAppProcessError(
            "Android app process prepare failed: " +
            std::string(error.what()));
    }
}

AndroidAppProcess::AndroidAppProcess(std::unique_ptr<Impl> impl) noexcept
    : impl_(std::move(impl)) {}

AndroidAppProcess::~AndroidAppProcess() {
    if (!impl_ || impl_->state == AndroidAppProcessState::stopped) return;
    try {
        static_cast<void>(Stop());
    } catch (...) {
    }
}

void AndroidAppProcess::StartApplication() {
    if (impl_->state != AndroidAppProcessState::dex_vm_ready) {
        Fail("Application startup requires DexVmReady state");
    }
    static_cast<void>(StartDexApplication(
        *impl_->bridge, impl_->context, impl_->application_descriptor));
    impl_->state = AndroidAppProcessState::application_started;
}

LifecycleFrameState AndroidAppProcess::StartLauncherActivity() {
    if (impl_->state != AndroidAppProcessState::application_started) {
        Fail("launcher startup requires ApplicationStarted state");
    }
    const auto result = impl_->lifecycle->Start();
    impl_->state = AndroidAppProcessState::activity_resumed;
    return result;
}

LifecycleFrameState AndroidAppProcess::Stop() {
    if (impl_->state == AndroidAppProcessState::stopped) {
        return impl_->lifecycle->State();
    }
    const auto result = impl_->lifecycle->Stop();
    impl_->state = AndroidAppProcessState::stopped;
    return result;
}

AndroidAppProcessState AndroidAppProcess::State() const noexcept {
    return impl_->state;
}

std::optional<loader::AndroidArmAbi> AndroidAppProcess::SelectedAbi() const
    noexcept {
    return impl_->selected_abi;
}

runtime::AndroidGuestProcess& AndroidAppProcess::NativeProcess() noexcept {
    return impl_->session->Process();
}

runtime::DexVmGuestBridge& AndroidAppProcess::DexVm() noexcept {
    return *impl_->bridge;
}

DexActivityLifecycle& AndroidAppProcess::ActivityLifecycle() noexcept {
    return *impl_->lifecycle;
}

runtime::NativeLibraryLoader* AndroidAppProcess::NativeLibraries() noexcept {
    return impl_->native_libraries.get();
}

const std::shared_ptr<runtime::DexVmAndroidContext>&
AndroidAppProcess::Context() const noexcept {
    return impl_->context;
}

}  // namespace ogplay::session
