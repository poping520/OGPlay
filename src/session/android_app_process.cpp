#include "ogplay/session/android_app_process.h"

#include <algorithm>
#include <unordered_map>
#include <utility>

#include "ogplay/runtime/bionic/bionic_profile.h"
#include "ogplay/runtime/debug/stall_diagnostics.h"
#include "ogplay/runtime/dexvm/vm_monitors.h"

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

std::string_view ThreadStatusName(const runtime::dexvm::VmThreadStatus status) {
    using S = runtime::dexvm::VmThreadStatus;
    switch (status) {
        case S::created: return "created";
        case S::running: return "running";
        case S::finished: return "finished";
        case S::stopped: return "stopped";
        case S::failed: return "failed";
    }
    return "unknown";
}

std::string_view WaitStateName(const runtime::dexvm::VmThreadWaitState state) {
    using S = runtime::dexvm::VmThreadWaitState;
    switch (state) {
        case S::none: return "none";
        case S::sleeping: return "sleeping";
        case S::joining: return "joining";
        case S::monitor: return "monitor";
    }
    return "unknown";
}

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
          host(std::move(request.host)),
          diagnostics(request.diagnostics) {
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
             std::move(request.platform), request.proc_facts,
             request.diagnostics});
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
        context->pcm_playback = &session->PcmPlayback();
        context->encoded_audio_playback = &session->SoundPoolMixer();
        context->native_libraries = native_libraries.get();
        context->package_name = manifest.package;
        context->package_resource_path =
            "/data/app/" + manifest.package + "-1.apk";
        if (!context->apk_bytes.empty()) {
            request.filesystem->PutFile(context->package_resource_path,
                                        context->apk_bytes, false);
        }
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
        bindings.diagnostics = request.diagnostics;
        lifecycle =
            std::make_unique<DexActivityLifecycle>(std::move(bindings));
        BindDiagnostics();
        state = AndroidAppProcessState::dex_vm_ready;
    }

    ~Impl() {
        if (!diagnostics) return;
        diagnostics->SetNativeMethodResolver({});
        diagnostics->SetDexVmProvider({});
        diagnostics->SetMonitorProvider({});
        diagnostics->SetPacerProvider({});
        diagnostics->SetGlesProvider({});
    }

    void BindDiagnostics() {
        if (!diagnostics) return;
        diagnostics->SetNativeMethodResolver([this](const std::uint64_t id) {
            try {
                const auto& method = bridge->Linker().Method(
                    runtime::dexvm::VmMethodId(static_cast<std::uint32_t>(id)));
                const auto& owner = bridge->Linker().Class(method.owner);
                return owner.descriptor + "." + method.name + method.descriptor;
            } catch (...) {
                return std::string{};
            }
        });
        diagnostics->SetDexVmProvider([this]()
            -> std::optional<runtime::debug::DiagnosticDexVmSnapshot> {
            const auto trace = bridge->Vm().TryTrace(128U);
            const auto stacks = bridge->Vm().TryStackSnapshot();
            const auto threads = bridge->Threads().TrySnapshot();
            if (!trace || !stacks || !threads) return std::nullopt;
            runtime::debug::DiagnosticDexVmSnapshot result;
            result.events.reserve(trace->size());
            for (const auto& event : *trace) {
                result.events.push_back(
                    {event.sequence, event.context_token, event.tick,
                     event.dex_pc,
                     std::string(runtime::dexvm::DexVmTraceKindName(event.kind)),
                     event.class_descriptor + "." + event.method_name +
                         event.method_descriptor});
            }
            std::unordered_map<std::uint64_t,
                               const runtime::dexvm::DexVmThreadStack*>
                stack_by_context;
            for (const auto& stack : *stacks) {
                stack_by_context.emplace(stack.context_token, &stack);
            }
            result.threads.reserve(threads->size());
            for (const auto& thread : *threads) {
                runtime::debug::DiagnosticJavaThread diagnostic{
                    thread.id, thread.context_token, thread.name,
                    std::string(ThreadStatusName(thread.status)),
                    std::string(WaitStateName(thread.wait_state))};
                if (const auto found = stack_by_context.find(
                        thread.context_token);
                    found != stack_by_context.end()) {
                    diagnostic.ticks = found->second->ticks;
                    diagnostic.pending_exception =
                        found->second->pending_exception;
                    diagnostic.frames.reserve(found->second->frames.size());
                    for (const auto& frame : found->second->frames) {
                        diagnostic.frames.push_back(
                            {frame.class_descriptor + "." +
                                 frame.method_name + frame.method_descriptor,
                             frame.dex_pc});
                    }
                }
                result.threads.push_back(std::move(diagnostic));
            }
            return result;
        });
        diagnostics->SetMonitorProvider([this]()
            -> std::optional<std::vector<runtime::debug::DiagnosticMonitor>> {
            const auto monitors = bridge->Vm().Monitors().TrySnapshotAll();
            if (!monitors) return std::nullopt;
            std::vector<runtime::debug::DiagnosticMonitor> result;
            result.reserve(monitors->size());
            for (const auto& monitor : *monitors) {
                result.push_back({monitor.object, monitor.owner,
                                  monitor.recursion, monitor.entry_waiters,
                                  monitor.notify_wait_set});
            }
            return result;
        });
        diagnostics->SetPacerProvider([this]()
            -> std::optional<runtime::debug::DiagnosticPacer> {
            const auto pacer = runtime::TryEglSwapPacerSnapshot(*context);
            if (!pacer) return std::nullopt;
            return runtime::debug::DiagnosticPacer{
                pacer->attached, pacer->driver_blocked, pacer->shutdown,
                pacer->surface_retired, pacer->generation};
        });
        diagnostics->SetGlesProvider([this]()
            -> std::optional<std::vector<runtime::debug::DiagnosticGlesEvent>> {
            const auto trace = session->Process().TryGlesTrace(128U);
            if (!trace) return std::nullopt;
            std::vector<runtime::debug::DiagnosticGlesEvent> result;
            result.reserve(trace->size());
            for (const auto& event : *trace) {
                const auto argument = [&event](const char* name) {
                    const auto found = event.arguments.find(name);
                    return found == event.arguments.end()
                        ? 0U
                        : static_cast<std::uint32_t>(std::stoul(found->second));
                };
                result.push_back({event.call, argument("r0"), argument("r1"),
                                  argument("r2"), argument("r3"), event.error});
            }
            return result;
        });
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
    std::shared_ptr<runtime::debug::DiagnosticState> diagnostics;
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
