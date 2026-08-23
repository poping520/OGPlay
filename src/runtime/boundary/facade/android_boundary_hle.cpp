#include "ogplay/runtime/boundary/android_boundary_hle.h"

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <exception>
#include <limits>
#include <mutex>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

#include "ogplay/gles/angle_frame.h"
#include "ogplay/gles/gles_dispatch.h"
#include "ogplay/gles/generated/gles1_catalog.h"
#include "ogplay/gles/generated/gles1_extensions_catalog.h"
#include "ogplay/gles/generated/gles2_catalog.h"
#include "ogplay/gles/guest_transfer.h"
#include "ogplay/gles/supersample.h"
#include "runtime/boundary/services/graphics_dispatch.h"
#include "runtime/boundary/services/android_boundary_services.h"
#include "runtime/boundary/services/graphics_boundary_context.h"
#include "runtime/boundary/services/frame_service.h"
#include "runtime/boundary/core/a32_call_frame.h"
#include "runtime/boundary/services/guest_gl_context.h"
#include "ogplay/runtime/bionic/guest_symbol_override_metadata.h"
#include "runtime/bionic/libc_override_module.h"
#include "runtime/boundary/core/boundary_symbols.h"
#include "runtime/boundary/core/boundary_fault.h"
#include "runtime/boundary/core/boundary_fast_router.h"
#include "runtime/boundary/core/boundary_thunk_arena.h"
#include "runtime/boundary/modules/android/android_exports.h"
#include "runtime/boundary/modules/android/android_module.h"
#include "runtime/boundary/modules/egl/egl_exports.h"
#include "runtime/boundary/modules/egl/egl_module.h"
#include "runtime/boundary/modules/gles1/gles1_completion.h"
#include "runtime/boundary/modules/gles1/gles1_dispatch.h"
#include "runtime/boundary/modules/gles1/gles1_module.h"
#include "runtime/boundary/modules/gles1/gles1_draw.h"
#include "runtime/boundary/modules/gles1/gles1_fixed.h"
#include "runtime/boundary/modules/gles1/gles1_query.h"
#include "runtime/boundary/modules/gles2/gles2_module.h"
#include "runtime/boundary/modules/log/log_exports.h"
#include "runtime/boundary/modules/log/log_module.h"
#include "runtime/boundary/modules/module_catalog.h"
#include "runtime/boundary/modules/opensles/opensles_module.h"
#include "runtime/boundary/modules/opensles/opensles_abi.h"
#include "runtime/boundary/modules/opensles/opensles_exports.h"

namespace ogplay::runtime {
namespace {
constexpr std::size_t kMaximumShaderSourceCount = 1024;
constexpr std::size_t kMaximumShaderSourceBytes = 4U * 1024U * 1024U;

}  // namespace

class AndroidBoundaryHle::Impl final {
public:
    Impl(memory::AddressSpace& address_space, const gles::AngleBackend backend,
         const std::uint32_t width, const std::uint32_t height,
         const std::uint32_t supersample_factor,
         const AndroidBoundaryOptions options)
        : address_space_(address_space), backend_(backend),
          layout_(gles::MakeSupersampleLayout(width, height, supersample_factor)),
          symbols_(detail::BuildAndroidBoundarySymbols()),
          descriptors_(detail::BuildAndroidBoundaryDescriptors(symbols_)),
          provider_(symbols_),
          thunk_arena_(address_space_), fast_router_(thunk_arena_),
          frame_service_(layout_, descriptors_),
          gles_dispatch_(address_space, gl_context_),
          gles1_state_(gl_context_.Shared()),
          call_services_{address_space_, fault_store_, this,
                         &ServiceRecordGpuCall},
          android_services_{address_space_},
          graphics_context_{
              backend_, layout_, gl_context_, gles_dispatch_,
              angle_frame_, gl_owner_,
              managed_surface_, frame_service_,
              this, &ServiceRequireFrame, &ServiceInitializeGuestGlDefaults,
              &ServiceReleaseManagedSurface, &ServicePublishFrame,
              &ServiceResetGuestGraphics,
              &ServiceWrite32, &ServiceWriteRequired32,
              &ServiceReadCString, &ServiceReadShaderSources},
          egl_context_{graphics_context_, symbols_},
          android_module_(call_services_, android_services_),
          egl_module_(call_services_, egl_context_),
          gles1_module_(call_services_, graphics_context_, gles1_state_,
                        gles1_legacy_state_, gles1_draw_state_,
                        gles1_dispatch_, gles1_extensions_dispatch_),
          gles2_module_(call_services_, graphics_context_),
          log_context_{address_space_, options.logger, this,
                       &ServiceRecordFastFault, options.guest_file_owner,
                       options.read_guest_file},
          log_module_(log_context_),
          open_sles_module_(call_services_, open_sles_mixer_,
                            options.open_sles_callbacks),
          libc_override_module_(call_services_) {
        detail::BindAndroidBoundaryGles1Core(
            gles1_dispatch_, gles1_state_, address_space_, layout_.factor,
            [this](const std::string_view operation) -> gles::AngleFrame& {
                return RequireFrame(operation);
            });
        detail::BindAndroidBoundaryGles1Textures(
            gles1_dispatch_, gles1_state_, address_space_,
            [this](const std::string_view operation) -> gles::AngleFrame& {
                return RequireFrame(operation);
            });
        detail::BindAndroidBoundaryGles1Draw(
            gles1_dispatch_, gles1_extensions_dispatch_, gles1_draw_state_, gles1_state_,
            gles1_legacy_state_, address_space_,
            [this](const std::string_view operation) -> gles::AngleFrame& {
                return RequireFrame(operation);
            });
        detail::BindAndroidBoundaryGles1Queries(
            gles1_dispatch_, gles1_query_strings_,
            [this](const std::uint32_t parameter) {
                return RequireFrame("glGetString").GetString(parameter);
            });
        detail::BindAndroidBoundaryGles1Legacy(
            gles1_dispatch_, gles1_legacy_state_, gles1_state_,
            gles1_draw_state_, address_space_,
            [this](const std::string_view operation) -> gles::AngleFrame& {
                return RequireFrame(operation);
            });
        detail::BindAndroidBoundaryGles1Completion(
            gles1_dispatch_, gles1_state_, gles1_legacy_state_, address_space_,
            [this](const std::string_view operation) -> gles::AngleFrame& {
                return RequireFrame(operation);
            });
        gles1_dispatch_.Seal();
        gles1_extensions_dispatch_.Seal();
        gles1_state_.Fixed().SetMaterialSingleFaceQuirk(
            options.allow_gles1_material_single_face);
        SealBindings();
    }
    void MapThunks() {
        thunk_arena_.Map(descriptors_.size());
        const auto* open_sles = AndroidBoundaryCatalog(AndroidApi::api19)
                                    .FindModule("libOpenSLES.so");
        if (open_sles == nullptr) {
            throw std::logic_error("OpenSL ES module metadata is missing");
        }
        MapOpenSlesStaticAbi(address_space_, *open_sles);
        open_sles_module_.MapGuestObjectArena();
    }

    [[nodiscard]] cpu::HostCallHook FastHostCallHook() noexcept {
        return fast_router_.Hook();
    }
    void OpenManagedSurface() {
        if (angle_frame_.has_value()) {
            throw std::logic_error(
                "Android boundary already has a current ANGLE frame");
        }
        angle_frame_.emplace(gles::AngleFrame::CreatePbuffer(
            backend_, layout_.render_width, layout_.render_height));
        gl_owner_ = std::this_thread::get_id();
        InitializeGuestGlDefaults();
        managed_surface_ = true;
        frame_service_.SetRenderTargetReady(true);
    }
    void BindManagedSurfaceOnCallingThread() {
        static_cast<void>(RequireFrame("bind managed surface"));
    }
    void ReleaseManagedSurfaceFromCallingThread() {
        if (!angle_frame_.has_value()) {
            throw std::runtime_error(
                "release managed surface has no current ANGLE frame");
        }
        if (!gl_owner_.has_value()) {
            return;
        }
        if (*gl_owner_ != std::this_thread::get_id()) {
            throw std::runtime_error(
                "release managed surface violates GL context thread affinity");
        }
        angle_frame_->ReleaseCurrent();
        gl_owner_.reset();
    }
    [[nodiscard]] bool ManagedSurfaceIsOpen() const noexcept {
        return managed_surface_ && angle_frame_.has_value();
    }
    [[nodiscard]] std::string ManagedGlString(const std::uint32_t parameter) {
        return RequireFrame("managed glGetString").GetString(parameter);
    }
    void PresentManagedSurface() {
        if (!managed_surface_ || !angle_frame_.has_value()) {
            throw std::logic_error(
                "Android boundary managed surface is not open");
        }
        PublishFrame();
    }
    void CloseManagedSurface() {
        if (!managed_surface_ || !angle_frame_.has_value()) {
            throw std::logic_error(
                "Android boundary managed surface is not open");
        }
        gl_owner_.reset();
        angle_frame_.reset();
        managed_surface_ = false;
        ResetGuestGraphics();
        frame_service_.SetRenderTargetReady(false);
    }
    [[nodiscard]] bool Handle(cpu::Cpu& cpu, const cpu::RunResult& stopped) {
        if (stopped.reason == cpu::RunStopReason::host_call_fault &&
            stopped.immediate == 2U) {
            const auto state = cpu.GetState();
            auto pending = fault_store_.Take(state.ThreadId(), stopped.pc);
            if (!pending) return false;
            std::rethrow_exception(pending);
        }
        if (!thunk_arena_.IsMapped() ||
            stopped.reason != cpu::RunStopReason::supervisor_call ||
            stopped.immediate != 2) return false;
        const auto* descriptor = detail::DecodeAndroidBoundaryThunk(
            stopped.pc.Value(), descriptors_);
        if (descriptor == nullptr) return false;
        const auto descriptor_index =
            static_cast<std::size_t>(descriptor - descriptors_.data());
        const auto& binding = fast_router_.Entry(descriptor_index);
        auto state = cpu.GetState();
        const A32CallFrame call(address_space_, state,
                                descriptor->parameter_count);
        const auto arguments = call.RegisterArguments();
        std::uint32_t result{};
        try {
            result = binding.slow(binding.self, call);
        } catch (const gles::GuestTransferError& error) {
            throw gles::GuestTransferError(
                "Android boundary guest transfer failed in " +
                std::string(descriptor->library) + "!" +
                std::string(descriptor->name) +
                ": " + error.what() +
                "; r0=" + std::to_string(arguments[0]) +
                " r1=" + std::to_string(arguments[1]) +
                " r2=" + std::to_string(arguments[2]) +
                " r3=" + std::to_string(arguments[3]) +
                " sp=" + std::to_string(
                    state.Register(cpu::CoreRegister::sp)) +
                " lr=" + std::to_string(
                    state.Register(cpu::CoreRegister::lr)) +
                " thread=" + std::to_string(state.ThreadId()));
        }
        RecordGpuCall(descriptor_index, arguments, binding.gpu);
        state.SetRegister(cpu::CoreRegister::r0, result);
        cpu.SetState(state);
        return true;
    }
    void NotifyFileWrite() {
        android_module_.NotifyFileWrite();
    }
    void PushInput(const AndroidBoundaryInput& input) {
        android_module_.PushInput(input);
    }
    [[nodiscard]] std::optional<AndroidBoundaryFrame> TakeLatestFrame() {
        return frame_service_.TakeLatestFrame();
    }
    // Host-decoded frames (e.g. video playback) enter the same store and
    // sequence as GL presents; the layout contract stays logical-sized.
    void PublishSoftwareFrame(std::vector<std::uint8_t> rgba8) {
        frame_service_.PublishSoftwareFrame(std::move(rgba8));
    }
    void RecycleFrame(AndroidBoundaryFrame&& frame) {
        frame_service_.RecycleFrame(std::move(frame));
    }
    std::vector<audio::OpenSlesConsumedBuffer> MixOpenSlesPcm16(
        const std::span<std::int16_t> output,
        const std::uint32_t output_rate) {
        return open_sles_module_.MixAdditiveStereoPcm16(output, output_rate);
    }
    [[nodiscard]] const BionicHleSymbolProvider& Symbols() const noexcept {
        return provider_;
    }
    [[nodiscard]] core::GpuStats Stats() const {
        return frame_service_.Stats();
    }
    [[nodiscard]] std::vector<core::GpuRenderTarget> RenderTargets() const {
        return frame_service_.RenderTargets();
    }
    [[nodiscard]] core::GpuCapabilities Capabilities() const {
        return {{}, {}, std::string(gles::AngleBackendName(backend_))};
    }
    [[nodiscard]] std::vector<core::GpuTraceEntry> Trace(
        const std::string_view filter, const std::size_t limit) const {
        return frame_service_.Trace(filter, limit);
    }
private:
    static void ServiceRecordFastFault(
        void* owner, const cpu::A32HostCallContext& context) noexcept {
        static_cast<Impl*>(owner)->fault_store_.RecordCurrent(context);
    }
    static void ServiceRecordGpuCall(
        void* owner, const std::size_t slot,
        const std::array<std::uint32_t, 4>& arguments, const bool gpu) {
        static_cast<Impl*>(owner)->RecordGpuCall(slot, arguments, gpu);
    }
    static gles::AngleFrame& ServiceRequireFrame(
        void* owner, const std::string_view operation) {
        return static_cast<Impl*>(owner)->RequireFrame(operation);
    }
    static void ServiceInitializeGuestGlDefaults(void* owner) {
        static_cast<Impl*>(owner)->InitializeGuestGlDefaults();
    }
    static void ServiceReleaseManagedSurface(void* owner) {
        static_cast<Impl*>(owner)->ReleaseManagedSurfaceFromCallingThread();
    }
    static void ServicePublishFrame(void* owner) {
        static_cast<Impl*>(owner)->PublishFrame();
    }
    static void ServiceResetGuestGraphics(void* owner) {
        static_cast<Impl*>(owner)->ResetGuestGraphics();
    }
    static void ServiceWrite32(void* owner, const std::uint32_t address,
                               const std::uint32_t value,
                               const std::uint64_t thread_id) {
        static_cast<Impl*>(owner)->Write32(address, value, thread_id);
    }
    static void ServiceWriteRequired32(
        void* owner, const std::uint32_t address, const std::uint32_t value,
        const std::uint64_t thread_id,
        const std::string_view operation) {
        static_cast<Impl*>(owner)->WriteRequired32(
            address, value, thread_id, operation);
    }
    static std::string ServiceReadCString(
        void* owner, const std::uint32_t address,
        const std::size_t maximum_bytes, const std::uint64_t thread_id,
        const std::string_view operation) {
        return static_cast<Impl*>(owner)->ReadCString(
            address, maximum_bytes, thread_id, operation);
    }
    static std::vector<std::string> ServiceReadShaderSources(
        void* owner, const std::array<std::uint32_t, 4>& arguments,
        const std::uint64_t thread_id) {
        return static_cast<Impl*>(owner)->ReadShaderSources(
            arguments, thread_id);
    }

    template <typename Module, auto Method, std::size_t ParameterCount,
              bool Gpu>
    void BindExport(const BoundaryModuleDescriptor& module_descriptor,
                    const std::string_view name, Module& module) {
        const auto found = std::find_if(
            module_descriptor.exports.begin(), module_descriptor.exports.end(),
            [&](const auto& export_) { return export_.name == name; });
        if (found == module_descriptor.exports.end() ||
            found->parameter_count != ParameterCount) {
            throw std::logic_error("concrete boundary export metadata mismatch");
        }
        const auto slot = BoundaryThunkArena::SlotForAddress(
            found->address.Value() & ~UINT32_C(1));
        if (slot >= fast_router_.Entries().size() ||
            fast_router_.Entry(slot).invoke != nullptr) {
            throw std::logic_error("boundary hot slot is invalid or already bound");
        }
        fast_router_.Entry(slot) = {
            &InvokeBoundaryFast<Module, Method, ParameterCount, Gpu>, &module,
            &InvokeBoundarySlow<Module, Method>, Gpu};
    }

    template <typename Module, auto Method, std::size_t ParameterCount,
              bool Gpu>
    void BindOverride(const std::string_view library,
                      const std::string_view name, Module& module) {
        const auto found = std::find_if(
            descriptors_.begin(), descriptors_.end(), [&](const auto& export_) {
                return export_.library == library && export_.name == name;
            });
        if (found == descriptors_.end() ||
            found->parameter_count != ParameterCount) {
            throw std::logic_error("guest symbol override metadata mismatch");
        }
        const auto slot = static_cast<std::size_t>(
            std::distance(descriptors_.begin(), found));
        if (fast_router_.Entry(slot).invoke != nullptr) {
            throw std::logic_error("guest symbol override slot is already bound");
        }
        fast_router_.Entry(slot) = {
            &InvokeBoundaryFast<Module, Method, ParameterCount, Gpu>, &module,
            &InvokeBoundarySlow<Module, Method>, Gpu};
    }

    template <std::size_t... Index>
    void BindGles1Core(const BoundaryModuleDescriptor& descriptor,
                       std::index_sequence<Index...>) {
        (BindExport<Gles1Module,
                    &Gles1Module::template Invoke<gles::GlesApi::gles1,
                                                  static_cast<gles::GlesThunkId>(Index)>,
                    gles::generated::gles1::kFunctions[Index].parameter_count,
                    true>(descriptor,
                          gles::generated::gles1::kFunctions[Index].name,
                          gles1_module_), ...);
    }

    template <std::size_t... Index>
    void BindGles1Extensions(const BoundaryModuleDescriptor& descriptor,
                             std::index_sequence<Index...>) {
        (BindExport<Gles1Module,
                    &Gles1Module::template Invoke<gles::GlesApi::gles1_extensions,
                                                  static_cast<gles::GlesThunkId>(Index)>,
                    gles::generated::gles1_extensions::kFunctions[Index].parameter_count,
                    true>(descriptor,
                          gles::generated::gles1_extensions::kFunctions[Index].name,
                          gles1_module_), ...);
    }

    template <std::size_t... Index>
    void BindGles2(const BoundaryModuleDescriptor& descriptor,
                   std::index_sequence<Index...>) {
        (BindExport<Gles2Module,
                    &Gles2Module::template Invoke<static_cast<gles::GlesThunkId>(Index)>,
                    gles::generated::gles2::kFunctions[Index].parameter_count,
                    true>(descriptor,
                          gles::generated::gles2::kFunctions[Index].name,
                          gles2_module_), ...);
    }

    void SealBindings() {
        fast_router_.Resize(descriptors_.size());
        const auto& catalog = AndroidBoundaryCatalog(AndroidApi::api19);
        const auto require = [&](const std::string_view soname)
            -> const BoundaryModuleDescriptor& {
            const auto* descriptor = catalog.FindModule(soname);
            if (descriptor == nullptr) {
                throw std::logic_error("required concrete boundary module is missing");
            }
            return *descriptor;
        };
        const auto& android = require("libandroid.so");
#define OGPLAY_BIND_ANDROID(name, id, count, method)                            \
        BindExport<AndroidModule, &AndroidModule::method, count,                \
                   false>(android, name, android_module_);
        OGPLAY_ANDROID_BOUNDARY_EXPORTS(OGPLAY_BIND_ANDROID)
#undef OGPLAY_BIND_ANDROID
        const auto& egl = require("libEGL.so");
#define OGPLAY_BIND_EGL(name, id, count, method)                                \
        BindExport<EglModule, &EglModule::method, count, true>(                 \
            egl, name, egl_module_);
        OGPLAY_EGL_BOUNDARY_EXPORTS(OGPLAY_BIND_EGL)
#undef OGPLAY_BIND_EGL
        const auto& gles1 = require("libGLESv1_CM.so");
        BindGles1Core(gles1, std::make_index_sequence<
                                  gles::generated::gles1::kFunctions.size()>{});
        BindGles1Extensions(
            gles1, std::make_index_sequence<
                       gles::generated::gles1_extensions::kFunctions.size()>{});
#define OGPLAY_BIND_GLES1_BOUNDS(name, id, count, method)                     \
        BindExport<Gles1Module, &Gles1Module::method, count, true>(           \
            gles1, name, gles1_module_);
        OGPLAY_GLES1_BOUNDS_EXPORTS(OGPLAY_BIND_GLES1_BOUNDS)
#undef OGPLAY_BIND_GLES1_BOUNDS
        const auto& gles2 = require("libGLESv2.so");
        BindGles2(gles2, std::make_index_sequence<
                              gles::generated::gles2::kFunctions.size()>{});
        const auto& log = require("liblog.so");
#define OGPLAY_BIND_LOG(name, id, count, method)                                \
        BindExport<LogModule, &LogModule::method, count, false>(                \
            log, name, log_module_);
        OGPLAY_LOG_BOUNDARY_EXPORTS(OGPLAY_BIND_LOG)
#undef OGPLAY_BIND_LOG
        const auto& open_sles = require("libOpenSLES.so");
#define OGPLAY_BIND_OPENSLES(name, id, count, kind, method)                    \
        BindExport<OpenSlesModule, &OpenSlesModule::method, count, false>(     \
            open_sles, name, open_sles_module_);
        OGPLAY_OPENSLES_BOUNDARY_EXPORTS(OGPLAY_BIND_OPENSLES)
#undef OGPLAY_BIND_OPENSLES

#define OGPLAY_BIND_OVERRIDE(library, symbol, id, count, method)               \
        BindOverride<LibcOverrideModule, &LibcOverrideModule::method, count,   \
                     false>(library, symbol, libc_override_module_);
        OGPLAY_GUEST_SYMBOL_OVERRIDE_EXPORTS(OGPLAY_BIND_OVERRIDE)
#undef OGPLAY_BIND_OVERRIDE
        const auto hot = fast_router_.Entries();
        if (std::any_of(hot.begin(), hot.end(),
                        [](const auto& entry) {
                            return entry.invoke == nullptr || entry.slow == nullptr;
                        })) {
            throw std::logic_error("boundary seal left an export unbound");
        }
    }

    void InitializeGuestGlDefaults() {
        constexpr auto kDither = UINT32_C(0x0BD0);
        const auto maximum_dimension = static_cast<std::uint32_t>(
            (std::numeric_limits<std::int32_t>::max)());
        if (layout_.logical_width > maximum_dimension ||
            layout_.logical_height > maximum_dimension) {
            throw std::overflow_error(
                "guest GL logical drawable dimensions exceed GLsizei");
        }

        const auto logical_width =
            static_cast<std::int32_t>(layout_.logical_width);
        const auto logical_height =
            static_cast<std::int32_t>(layout_.logical_height);
        const std::array<std::int32_t, 4> logical{
            0, 0, logical_width, logical_height};

        auto& frame = RequireFrame("initialize GL defaults");
        const auto render_width = detail::ScaleAndroidBoundaryViewportComponent(
            logical_width, layout_.factor);
        const auto render_height = detail::ScaleAndroidBoundaryViewportComponent(
            logical_height, layout_.factor);
        frame.Viewport(0, 0, render_width, render_height);
        frame.Scissor(0, 0, render_width, render_height);
        frame.SetCapability(kDither, true);

        auto& shared = gl_context_.Shared();
        shared.SetViewport(logical);
        shared.SetScissor(logical);
        shared.SetCapability(kDither, true);
    }
    void Write32(const std::uint32_t address, const std::uint32_t value,
                 const std::uint64_t thread_id) {
        if (address == 0) return;
        std::array<std::byte, 4> bytes{};
        for (std::size_t index = 0; index < bytes.size(); ++index) {
            bytes[index] = static_cast<std::byte>(value >> (index * 8U));
        }
        address_space_.Write(memory::GuestAddress{address}, bytes, thread_id);
    }
    void WriteRequired32(const std::uint32_t address, const std::uint32_t value,
                         const std::uint64_t thread_id,
                         const std::string_view operation) {
        if (address == 0) {
            throw std::invalid_argument(std::string(operation) +
                                        " requires a guest output pointer");
        }
        Write32(address, value, thread_id);
    }
    std::uint32_t Read32(const std::uint32_t address,
                         const std::uint64_t thread_id,
                         const std::string_view operation) const {
        if (address == 0) {
            throw std::invalid_argument(std::string(operation) +
                                        " requires a guest input pointer");
        }
        std::array<std::byte, 4> bytes{};
        address_space_.Read(memory::GuestAddress{address}, bytes, thread_id);
        std::uint32_t value{};
        for (std::size_t index = 0; index < bytes.size(); ++index) {
            value |= static_cast<std::uint32_t>(
                         std::to_integer<std::uint8_t>(bytes[index]))
                     << (index * 8U);
        }
        return value;
    }
    std::string ReadString(const std::uint32_t address,
                           const std::size_t byte_count,
                           const std::uint64_t thread_id,
                           const std::string_view operation) const {
        if (address == 0) {
            throw std::invalid_argument(std::string(operation) +
                                        " requires a guest string pointer");
        }
        std::vector<std::byte> bytes(byte_count);
        if (!bytes.empty()) {
            address_space_.Read(memory::GuestAddress{address}, bytes, thread_id);
        }
        std::string result;
        result.reserve(bytes.size());
        for (const auto value : bytes) {
            result.push_back(static_cast<char>(std::to_integer<unsigned char>(value)));
        }
        return result;
    }
    std::string ReadCString(const std::uint32_t address,
                            const std::size_t maximum_bytes,
                            const std::uint64_t thread_id,
                            const std::string_view operation) const {
        if (address == 0) {
            throw std::invalid_argument(std::string(operation) +
                                        " requires a guest string pointer");
        }
        std::size_t length{};
        try {
            length = address_space_.CStringLength(
                memory::GuestAddress{address}, maximum_bytes, thread_id);
        } catch (const std::length_error&) {
            throw std::length_error(std::string(operation) +
                                    " guest string exceeds its byte limit");
        }
        std::string result(length, '\0');
        address_space_.Read(memory::GuestAddress{address},
                            std::as_writable_bytes(std::span(result)),
                            thread_id);
        return result;
    }
    std::vector<std::string> ReadShaderSources(
        const std::array<std::uint32_t, 4>& args,
        const std::uint64_t thread_id) const {
        const auto signed_count = std::bit_cast<std::int32_t>(args[1]);
        if (signed_count < 0 ||
            static_cast<std::size_t>(signed_count) > kMaximumShaderSourceCount) {
            throw std::invalid_argument("glShaderSource count is outside the supported range");
        }
        const auto count = static_cast<std::size_t>(signed_count);
        if (count == 0) return {};
        if (args[2] == 0) {
            throw std::invalid_argument("glShaderSource requires a guest string array");
        }
        std::vector<std::string> result;
        result.reserve(count);
        std::size_t total_bytes{};
        for (std::size_t index = 0; index < count; ++index) {
            const auto source_address = Read32(
                memory::GuestAddress{args[2]}.Add(index * 4U).Value(),
                thread_id, "glShaderSource");
            const auto length = args[3] == 0 ? -1 : std::bit_cast<std::int32_t>(Read32(
                memory::GuestAddress{args[3]}.Add(index * 4U).Value(),
                thread_id, "glShaderSource"));
            const auto remaining = kMaximumShaderSourceBytes - total_bytes;
            if (length >= 0 && static_cast<std::size_t>(length) > remaining) {
                throw std::length_error("glShaderSource exceeds its total byte limit");
            }
            auto source = length < 0
                              ? ReadCString(source_address, remaining, thread_id,
                                            "glShaderSource")
                              : ReadString(source_address,
                                           static_cast<std::size_t>(length),
                                           thread_id, "glShaderSource");
            if (source.size() > remaining) {
                throw std::length_error("glShaderSource exceeds its total byte limit");
            }
            total_bytes += source.size();
            result.push_back(std::move(source));
        }
        return result;
    }
    gles::AngleFrame& RequireFrame(const std::string_view operation) {
        if (!angle_frame_.has_value()) {
            throw std::runtime_error(std::string(operation) + " has no current ANGLE frame");
        }
        const auto caller = std::this_thread::get_id();
        if (!gl_owner_.has_value()) {
            angle_frame_->BindCurrentOnCallingThread();
            gl_owner_ = caller;
        } else if (*gl_owner_ != caller) {
            throw std::runtime_error(
                std::string(operation) +
                " violates GL context thread affinity");
        }
        return *angle_frame_;
    }
    void PublishFrame() {
        frame_service_.PublishAngleFrame(RequireFrame("present"));
    }
    void ResetGuestGraphics() {
        gles_dispatch_.Reset();
        gles1_state_.Reset();
        gles1_legacy_state_.Reset();
        gles1_draw_state_.Reset();
    }
    void RecordGpuCall(const std::size_t descriptor_index,
                       const std::array<std::uint32_t, 4>& args,
                       const bool gpu) {
        frame_service_.RecordGpuCall(descriptor_index, args, gpu);
    }
    memory::AddressSpace& address_space_;
    gles::AngleBackend backend_;
    gles::SupersampleLayout layout_;
    std::vector<BionicHleSymbol> symbols_;
    std::vector<detail::HleThunkDescriptor> descriptors_;
    BionicHleSymbolProvider provider_;
    BoundaryThunkArena thunk_arena_;
    BoundaryFastRouter fast_router_;
    FrameService frame_service_;
    GuestGlContext gl_context_;
    AndroidBoundaryGles gles_dispatch_;
    detail::AndroidBoundaryGles1State gles1_state_;
    detail::AndroidBoundaryGles1DrawState gles1_draw_state_;
    detail::AndroidBoundaryGles1QueryStrings gles1_query_strings_{address_space_};
    detail::AndroidBoundaryGles1LegacyState gles1_legacy_state_;
    gles::GlesDispatchTable gles1_dispatch_{gles::GlesApi::gles1};
    gles::GlesDispatchTable gles1_extensions_dispatch_{
        gles::GlesApi::gles1_extensions};
    BoundaryFaultStore fault_store_;
    std::optional<gles::AngleFrame> angle_frame_;
    std::optional<std::thread::id> gl_owner_;
    bool managed_surface_{};
    BoundaryCallServices call_services_;
    AndroidBoundaryServices android_services_;
    GraphicsBoundaryContext graphics_context_;
    EglBoundaryContext egl_context_;
    AndroidModule android_module_;
    EglModule egl_module_;
    Gles1Module gles1_module_;
    Gles2Module gles2_module_;
    LogBoundaryContext log_context_;
    LogModule log_module_;
    audio::OpenSlesPcmMixer open_sles_mixer_;
    OpenSlesModule open_sles_module_;
    LibcOverrideModule libc_override_module_;
};
AndroidBoundaryHle::AndroidBoundaryHle(memory::AddressSpace& address_space,
                                       const gles::AngleBackend backend,
                                       const std::uint32_t width,
                                       const std::uint32_t height,
                                       const std::uint32_t supersample_factor,
                                       const AndroidBoundaryOptions options)
    : impl_(std::make_unique<Impl>(address_space, backend, width, height,
                                   supersample_factor, options)) {}
AndroidBoundaryHle::~AndroidBoundaryHle() = default;
void AndroidBoundaryHle::MapThunks() { impl_->MapThunks(); }
void AndroidBoundaryHle::OpenManagedSurface() {
    impl_->OpenManagedSurface();
}
void AndroidBoundaryHle::BindManagedSurfaceOnCallingThread() {
    impl_->BindManagedSurfaceOnCallingThread();
}
void AndroidBoundaryHle::ReleaseManagedSurfaceFromCallingThread() {
    impl_->ReleaseManagedSurfaceFromCallingThread();
}
bool AndroidBoundaryHle::ManagedSurfaceIsOpen() const noexcept {
    return impl_->ManagedSurfaceIsOpen();
}
std::string AndroidBoundaryHle::ManagedGlString(
    const std::uint32_t parameter) {
    return impl_->ManagedGlString(parameter);
}
void AndroidBoundaryHle::PresentManagedSurface() {
    impl_->PresentManagedSurface();
}
void AndroidBoundaryHle::CloseManagedSurface() {
    impl_->CloseManagedSurface();
}
const BionicHleSymbolProvider& AndroidBoundaryHle::Symbols() const noexcept {
    return impl_->Symbols();
}
cpu::HostCallHook AndroidBoundaryHle::FastHostCallHook() noexcept {
    return impl_->FastHostCallHook();
}
bool AndroidBoundaryHle::Handle(cpu::Cpu& cpu, const cpu::RunResult& stopped) {
    return impl_->Handle(cpu, stopped);
}
void AndroidBoundaryHle::NotifyFileWrite() { impl_->NotifyFileWrite(); }
void AndroidBoundaryHle::PushInput(const AndroidBoundaryInput& input) { impl_->PushInput(input); }
std::optional<AndroidBoundaryFrame> AndroidBoundaryHle::TakeLatestFrame() {
    return impl_->TakeLatestFrame();
}
void AndroidBoundaryHle::PublishSoftwareFrame(std::vector<std::uint8_t> rgba8) {
    impl_->PublishSoftwareFrame(std::move(rgba8));
}
void AndroidBoundaryHle::RecycleFrame(AndroidBoundaryFrame&& frame) {
    impl_->RecycleFrame(std::move(frame));
}
std::vector<audio::OpenSlesConsumedBuffer>
AndroidBoundaryHle::MixOpenSlesPcm16(
    const std::span<std::int16_t> output, const std::uint32_t output_rate) {
    return impl_->MixOpenSlesPcm16(output, output_rate);
}
core::GpuStats AndroidBoundaryHle::Stats() const { return impl_->Stats(); }
std::vector<core::GpuRenderTarget> AndroidBoundaryHle::RenderTargets() const {
    return impl_->RenderTargets();
}
core::GpuCapabilities AndroidBoundaryHle::Capabilities() const {
    return impl_->Capabilities();
}
std::vector<core::GpuTraceEntry> AndroidBoundaryHle::Trace(
    const std::string_view filter, const std::size_t limit) const {
    return impl_->Trace(filter, limit);
}

}  // namespace ogplay::runtime
