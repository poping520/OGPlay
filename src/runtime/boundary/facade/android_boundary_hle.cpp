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
#include "runtime/boundary/modules/gles2/gles2_dispatch.h"
#include "runtime/boundary/core/a32_call_frame.h"
#include "runtime/boundary/services/guest_gl_context.h"
#include "ogplay/runtime/bionic/guest_symbol_override_metadata.h"
#include "runtime/boundary/core/boundary_symbols.h"
#include "runtime/boundary/modules/android/android_exports.h"
#include "runtime/boundary/modules/egl/egl_exports.h"
#include "runtime/boundary/modules/gles1/gles1_dispatch.h"
#include "runtime/boundary/modules/gles1/gles1_draw.h"
#include "runtime/boundary/modules/gles1/gles1_fixed.h"
#include "runtime/boundary/modules/gles1/gles1_query.h"
#include "runtime/boundary/modules/log/log_exports.h"
#include "runtime/boundary/modules/log/log_module.h"
#include "runtime/boundary/modules/module_catalog.h"

namespace ogplay::runtime {
namespace {
constexpr std::uint32_t kThunkStride = 4;
constexpr std::uint32_t kFakeConfiguration = 0x6e003000U;
constexpr std::uint32_t kFakeLooper = 0x6e003100U;
constexpr std::uint32_t kFakeInputEvent = 0x6e003200U;
constexpr std::uint32_t kFakeDisplay = 1;
constexpr std::uint32_t kFakeConfig = 2;
constexpr std::uint32_t kFakeSurface = 3;
constexpr std::uint32_t kFakeContext = 4;
constexpr std::uint32_t kEglWidth = 0x3057;
constexpr std::uint32_t kEglHeight = 0x3056;
constexpr std::size_t kMaximumShaderSourceCount = 1024;
constexpr std::size_t kMaximumShaderSourceBytes = 4U * 1024U * 1024U;
constexpr std::size_t kMaximumGlesNameBytes = 4096;

std::uint32_t SignedResult(const std::int32_t value) noexcept {
    return std::bit_cast<std::uint32_t>(value);
}

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
          gles_dispatch_(address_space, gl_context_),
          gles1_state_(gl_context_.Shared()),
          call_services_{address_space_, this, &ServiceRecordFastFault,
                         &ServiceRecordGpuCall},
          android_services_{address_space_},
          graphics_context_{
              backend_, layout_, gl_context_, gles_dispatch_, gles1_state_,
              gles1_draw_state_, gles1_legacy_state_, gles1_dispatch_,
              gles1_extensions_dispatch_, angle_frame_, gl_owner_,
              managed_surface_, mutex_, gpu_stats_, gpu_render_target_ready_,
              this, &ServiceRequireFrame, &ServiceInitializeGuestGlDefaults,
              &ServiceReleaseManagedSurface, &ServicePublishFrame,
              &ServiceWrite32, &ServiceWriteRequired32,
              &ServiceReadCString, &ServiceReadShaderSources},
          android_module_(call_services_, android_services_),
          egl_module_(call_services_, graphics_context_),
          gles1_module_(call_services_, graphics_context_),
          gles2_module_(call_services_, graphics_context_),
          log_context_{address_space_, options.logger, this,
                       &ServiceRecordFastFault, options.guest_file_owner,
                       options.read_guest_file},
          log_module_(log_context_), libc_override_module_(call_services_) {
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
        gles1_dispatch_.Seal();
        gles1_extensions_dispatch_.Seal();
        gles1_state_.Fixed().SetMaterialSingleFaceQuirk(
            options.allow_gles1_material_single_face);
        SealBindings();
    }
    void MapThunks() {
        if (mapped_) throw std::logic_error("Android boundary thunks are already mapped");
        const auto page_size = address_space_.PageSize();
        const auto code_bytes = symbols_.size() * kThunkStride;
        const auto arena_bytes =
            ((code_bytes + page_size - 1U) / page_size) * page_size;
        if (arena_bytes == 0U ||
            arena_bytes > kBionicHleThunkEnd - kBionicHleThunkBegin) {
            throw std::length_error("Android boundary thunk arena exceeds its guest range");
        }
        address_space_.Map({memory::GuestAddress{kBionicHleThunkBegin}, arena_bytes},
                           memory::PageProtection::read | memory::PageProtection::write);
        std::vector<std::byte> code(arena_bytes, std::byte{});
        for (std::size_t index = 0; index < symbols_.size(); ++index) {
            const auto offset = index * kThunkStride;
            code[offset] = std::byte{0x02};
            code[offset + 1] = std::byte{0xdf};  // Thumb svc #2
            code[offset + 2] = std::byte{0x70};
            code[offset + 3] = std::byte{0x47};  // bx lr
        }
        address_space_.Write(memory::GuestAddress{kBionicHleThunkBegin}, code);
        address_space_.Protect({memory::GuestAddress{kBionicHleThunkBegin}, arena_bytes},
                               memory::PageProtection::read |
                                   memory::PageProtection::execute);
        thunk_bytes_ = arena_bytes;
        mapped_ = true;
    }

    [[nodiscard]] cpu::HostCallHook FastHostCallHook() noexcept {
        return {&TryFastHostCall, this};
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
        std::scoped_lock lock(mutex_);
        gpu_render_target_ready_ = true;
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
        gles_dispatch_.Reset();
        gles1_state_.Reset();
        gles1_legacy_state_.Reset();
        gles1_draw_state_.Reset();
        std::scoped_lock lock(mutex_);
        gpu_render_target_ready_ = false;
    }
    [[nodiscard]] bool Handle(cpu::Cpu& cpu, const cpu::RunResult& stopped) {
        if (stopped.reason == cpu::RunStopReason::host_call_fault &&
            stopped.immediate == 2U) {
            const auto state = cpu.GetState();
            std::exception_ptr pending;
            {
                std::scoped_lock lock(fast_fault_mutex_);
                const auto found = std::find_if(
                    fast_faults_.begin(), fast_faults_.end(),
                    [&](const auto& fault) {
                        return fault.thread_id == state.ThreadId() &&
                               fault.pc == stopped.pc;
                    });
                if (found != fast_faults_.end()) {
                    pending = std::move(found->exception);
                    fast_faults_.erase(found);
                }
            }
            if (!pending) return false;
            std::rethrow_exception(pending);
        }
        if (!mapped_ || stopped.reason != cpu::RunStopReason::supervisor_call ||
            stopped.immediate != 2) return false;
        const auto* descriptor = detail::DecodeAndroidBoundaryThunk(
            stopped.pc.Value(), descriptors_);
        if (descriptor == nullptr) return false;
        const auto descriptor_index =
            static_cast<std::size_t>(descriptor - descriptors_.data());
        const auto& binding = hot_.at(descriptor_index);
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
        std::scoped_lock lock(mutex_);
        auto result = std::move(latest_frame_);
        latest_frame_.reset();
        return result;
    }
    // Host-decoded frames (e.g. video playback) enter the same store and
    // sequence as GL presents; the layout contract stays logical-sized.
    void PublishSoftwareFrame(std::vector<std::uint8_t> rgba8) {
        const auto expected = static_cast<std::size_t>(
            layout_.logical_width) * layout_.logical_height * 4U;
        if (rgba8.size() != expected) {
            throw std::invalid_argument(
                "software frame does not match the logical surface layout");
        }
        AndroidBoundaryFrame frame{layout_.logical_width,
                                   layout_.logical_height, 0,
                                   std::move(rgba8)};
        {
            std::scoped_lock lock(mutex_);
            frame.sequence = ++frame_sequence_;
            latest_frame_ = std::move(frame);
        }
        ready_.notify_all();
    }
    void RecycleFrame(AndroidBoundaryFrame&& frame) {
        const auto expected_size = static_cast<std::size_t>(
            layout_.logical_width) * layout_.logical_height * 4U;
        if (frame.width != layout_.logical_width ||
            frame.height != layout_.logical_height ||
            frame.rgba8.size() != expected_size) {
            throw std::invalid_argument(
                "recycled Android boundary frame layout does not match");
        }
        if (layout_.factor != 1U) return;
        std::scoped_lock lock(mutex_);
        if (frame.rgba8.capacity() >= recycled_rgba8_.capacity()) {
            recycled_rgba8_ = std::move(frame.rgba8);
        }
    }
    [[nodiscard]] const BionicHleSymbolProvider& Symbols() const noexcept {
        return provider_;
    }
    [[nodiscard]] core::GpuStats Stats() const {
        std::scoped_lock lock(mutex_);
        return gpu_stats_;
    }
    [[nodiscard]] std::vector<core::GpuRenderTarget> RenderTargets() const {
        std::scoped_lock lock(mutex_);
        if (!gpu_render_target_ready_) return {};
        return {{0, layout_.render_width, layout_.render_height,
                 "RGBA8", {"color0"}, false}};
    }
    [[nodiscard]] core::GpuCapabilities Capabilities() const {
        return {{}, {}, std::string(gles::AngleBackendName(backend_))};
    }
    [[nodiscard]] std::vector<core::GpuTraceEntry> Trace(
        const std::string_view filter, const std::size_t limit) const {
        std::scoped_lock lock(trace_mutex_);
        std::vector<core::GpuTraceEntry> result;
        const auto available = std::min(gpu_trace_count_, gpu_trace_.size());
        result.reserve(std::min(limit, available));
        for (std::size_t offset = 0;
             offset < available && result.size() < limit; ++offset) {
            const auto index =
                (gpu_trace_write_ + gpu_trace_.size() - 1U - offset) %
                gpu_trace_.size();
            const auto& raw = gpu_trace_[index];
            const auto name = descriptors_[raw.descriptor_index].name;
            if (!filter.empty() && name.find(filter) == std::string_view::npos) {
                continue;
            }
            core::GpuTraceEntry entry;
            entry.call = name;
            for (std::size_t argument = 0; argument < raw.registers.size();
                 ++argument) {
                entry.arguments.emplace("r" + std::to_string(argument),
                                        std::to_string(raw.registers[argument]));
            }
            result.push_back(std::move(entry));
        }
        std::reverse(result.begin(), result.end());
        return result;
    }
private:
    struct BoundaryCallServices final {
        memory::AddressSpace& address_space;
        void* owner{};
        void (*record_fast_fault)(void*,
                                  const cpu::A32HostCallContext&) noexcept{};
        void (*record_gpu_call)(void*, std::size_t,
                                const std::array<std::uint32_t, 4>&, bool){};

        void RecordFastFault(
            const cpu::A32HostCallContext& context) const noexcept {
            record_fast_fault(owner, context);
        }
        void RecordGpuCall(
            const std::size_t slot,
            const std::array<std::uint32_t, 4>& arguments,
            const bool gpu) const {
            record_gpu_call(owner, slot, arguments, gpu);
        }
    };

    struct AndroidBoundaryServices final {
        memory::AddressSpace& address_space;

        void Write32(const std::uint32_t address, const std::uint32_t value,
                     const std::uint64_t thread_id) const {
            if (address == 0) return;
            std::array<std::byte, 4> bytes{};
            for (std::size_t index = 0; index < bytes.size(); ++index) {
                bytes[index] =
                    static_cast<std::byte>(value >> (index * 8U));
            }
            address_space.Write(
                memory::GuestAddress{address}, bytes, thread_id);
        }
    };

    struct GraphicsBoundaryContext final {
        gles::AngleBackend& backend;
        gles::SupersampleLayout& layout;
        GuestGlContext& gl_context;
        AndroidBoundaryGles& gles_dispatch;
        detail::AndroidBoundaryGles1State& gles1_state;
        detail::AndroidBoundaryGles1DrawState& gles1_draw_state;
        detail::AndroidBoundaryGles1LegacyState& gles1_legacy_state;
        gles::GlesDispatchTable& gles1_dispatch;
        gles::GlesDispatchTable& gles1_extensions_dispatch;
        std::optional<gles::AngleFrame>& angle_frame;
        std::optional<std::thread::id>& gl_owner;
        bool& managed_surface;
        std::mutex& mutex;
        core::GpuStats& gpu_stats;
        bool& gpu_render_target_ready;
        void* owner{};
        gles::AngleFrame& (*require_frame)(void*, std::string_view){};
        void (*initialize_defaults)(void*){};
        void (*release_surface)(void*){};
        void (*publish_frame)(void*){};
        void (*write32)(void*, std::uint32_t, std::uint32_t,
                        std::uint64_t){};
        void (*write_required32)(void*, std::uint32_t, std::uint32_t,
                                 std::uint64_t, std::string_view){};
        std::string (*read_cstring)(void*, std::uint32_t, std::size_t,
                                    std::uint64_t, std::string_view){};
        std::vector<std::string> (*read_shader_sources)(
            void*, const std::array<std::uint32_t, 4>&, std::uint64_t){};

        [[nodiscard]] gles::AngleFrame& RequireFrame(
            const std::string_view operation) const {
            return require_frame(owner, operation);
        }
        void InitializeGuestGlDefaults() const { initialize_defaults(owner); }
        void ReleaseManagedSurfaceFromCallingThread() const {
            release_surface(owner);
        }
        void PublishFrame() const { publish_frame(owner); }
        void Write32(const std::uint32_t address, const std::uint32_t value,
                     const std::uint64_t thread_id) const {
            write32(owner, address, value, thread_id);
        }
        void WriteRequired32(const std::uint32_t address,
                             const std::uint32_t value,
                             const std::uint64_t thread_id,
                             const std::string_view operation) const {
            write_required32(owner, address, value, thread_id, operation);
        }
        [[nodiscard]] std::string ReadCString(
            const std::uint32_t address, const std::size_t maximum_bytes,
            const std::uint64_t thread_id,
            const std::string_view operation) const {
            return read_cstring(
                owner, address, maximum_bytes, thread_id, operation);
        }
        [[nodiscard]] std::vector<std::string> ReadShaderSources(
            const std::array<std::uint32_t, 4>& arguments,
            const std::uint64_t thread_id) const {
            return read_shader_sources(owner, arguments, thread_id);
        }
    };

    struct AndroidModule final {
        AndroidModule(BoundaryCallServices& calls,
                      AndroidBoundaryServices& services) noexcept
            : calls_(calls), services_(services) {}
        [[nodiscard]] BoundaryCallServices& CallServices() noexcept {
            return calls_;
        }
        void NotifyFileWrite() {
            {
                std::scoped_lock lock(mutex_);
                ++pending_command_writes_;
            }
            ready_.notify_all();
        }
        void PushInput(const AndroidBoundaryInput& input) {
            {
                std::scoped_lock lock(mutex_);
                inputs_.push_back(input);
            }
            ready_.notify_all();
        }
#define OGPLAY_DECLARE_ANDROID(name, id, count, method)                         \
        std::uint32_t method(const A32CallFrame& call) {                        \
            return ExecuteExport<id>(call);                                    \
        }
        OGPLAY_ANDROID_BOUNDARY_EXPORTS(OGPLAY_DECLARE_ANDROID)
#undef OGPLAY_DECLARE_ANDROID
    private:
        std::uint32_t PollAll(const std::array<std::uint32_t, 4>& args,
                              const std::uint64_t thread_id) {
            const auto timeout = std::bit_cast<std::int32_t>(args[0]);
            std::unique_lock lock(mutex_);
            const auto has_source = [this] {
                return pending_command_writes_ != 0 || !inputs_.empty();
            };
            if (!has_source()) {
                if (timeout < 0) {
                    ready_.wait(lock, has_source);
                } else if (timeout > 0) {
                    ready_.wait_for(
                        lock, std::chrono::milliseconds(timeout), has_source);
                }
            }
            std::uint32_t ident{};
            std::uint32_t data{};
            if (pending_command_writes_ != 0) {
                --pending_command_writes_;
                ident = command_ident_;
                data = command_data_;
            } else if (!inputs_.empty()) {
                ident = input_ident_;
                data = input_data_;
            } else {
                return SignedResult(-1);
            }
            lock.unlock();
            services_.Write32(args[1], 0, thread_id);
            services_.Write32(args[2], 1, thread_id);
            services_.Write32(args[3], data, thread_id);
            return ident;
        }

        template <std::uint16_t FunctionId>
        std::uint32_t ExecuteExport(const A32CallFrame& call) {
            const auto args = call.RegisterArguments();
            const auto tid = call.ThreadId();
            if constexpr (FunctionId == 0U) return kFakeConfiguration;
            if constexpr (FunctionId == 1U || FunctionId == 2U ||
                          FunctionId == 9U || FunctionId == 11U ||
                          FunctionId == 19U) return 0;
            if constexpr (FunctionId == 3U || FunctionId == 4U) {
                const auto output = call.Pointer<std::byte>(1);
                if (!output.IsNull()) {
                    const std::array bytes{std::byte{'e'}, std::byte{'n'}};
                    services_.address_space.Write(output.Address(), bytes, tid);
                }
                return 0;
            }
            if constexpr (FunctionId == 5U) return kFakeLooper;
            if constexpr (FunctionId == 6U) {
                std::scoped_lock lock(mutex_);
                command_ident_ = args[2];
                command_data_ = call.Argument(5);
                return 1;
            }
            if constexpr (FunctionId == 7U) return PollAll(args, tid);
            if constexpr (FunctionId == 8U) {
                std::scoped_lock lock(mutex_);
                input_ident_ = args[2];
                input_data_ = call.Argument(4);
                return 0;
            }
            if constexpr (FunctionId == 10U) {
                std::scoped_lock lock(mutex_);
                if (inputs_.empty()) return SignedResult(-1);
                active_input_ = inputs_.front();
                inputs_.pop_front();
                services_.Write32(args[1], kFakeInputEvent, tid);
                return 0;
            }
            if constexpr (FunctionId == 12U) {
                std::scoped_lock lock(mutex_);
                active_input_.reset();
                return 0;
            }
            if constexpr (FunctionId == 13U) {
                std::scoped_lock lock(mutex_);
                return active_input_.has_value() &&
                               active_input_->type ==
                                   AndroidBoundaryInputType::key
                           ? 1U : 2U;
            }
            if constexpr (FunctionId == 14U) {
                std::scoped_lock lock(mutex_);
                return active_input_.has_value() && active_input_->pressed
                           ? 0U : 1U;
            }
            if constexpr (FunctionId == 15U) {
                std::scoped_lock lock(mutex_);
                return active_input_.has_value()
                           ? static_cast<std::uint32_t>(
                                 active_input_->code)
                           : 0U;
            }
            if constexpr (FunctionId == 16U) {
                std::scoped_lock lock(mutex_);
                if (!active_input_.has_value() ||
                    active_input_->type ==
                        AndroidBoundaryInputType::pointer_motion) return 2U;
                return active_input_->pressed ? 0U : 1U;
            }
            if constexpr (FunctionId == 17U || FunctionId == 18U) {
                std::scoped_lock lock(mutex_);
                const auto value = !active_input_.has_value()
                                       ? 0.0F
                                       : FunctionId == 17U
                                             ? active_input_->x
                                             : active_input_->y;
                return std::bit_cast<std::uint32_t>(value);
            }
            throw std::logic_error("unbound concrete libandroid export");
        }
        BoundaryCallServices& calls_;
        AndroidBoundaryServices& services_;
        std::mutex mutex_;
        std::condition_variable ready_;
        std::uint64_t pending_command_writes_{};
        std::uint32_t command_ident_{};
        std::uint32_t command_data_{};
        std::uint32_t input_ident_{};
        std::uint32_t input_data_{};
        std::deque<AndroidBoundaryInput> inputs_;
        std::optional<AndroidBoundaryInput> active_input_;
    };

    struct EglModule final {
        EglModule(BoundaryCallServices& calls,
                  GraphicsBoundaryContext& graphics) noexcept
            : calls_(calls), graphics_(graphics) {}
        [[nodiscard]] BoundaryCallServices& CallServices() noexcept {
            return calls_;
        }
#define OGPLAY_DECLARE_EGL(name, id, count, method)                             \
        std::uint32_t method(const A32CallFrame& call) {                        \
            return ExecuteExport<id>(call);                                    \
        }
        OGPLAY_EGL_BOUNDARY_EXPORTS(OGPLAY_DECLARE_EGL)
#undef OGPLAY_DECLARE_EGL
    private:
        template <std::uint16_t FunctionId>
        std::uint32_t ExecuteExport(const A32CallFrame& call) {
            const auto args = call.RegisterArguments();
            const auto tid = call.ThreadId();
            if constexpr (FunctionId == 0U) return kFakeDisplay;
            if constexpr (FunctionId == 1U) {
                graphics_.Write32(
                    call.Pointer<std::uint32_t>(1).Address().Value(), 1, tid);
                graphics_.Write32(
                    call.Pointer<std::uint32_t>(2).Address().Value(), 5, tid);
                return 1;
            }
            if constexpr (FunctionId == 2U) {
                graphics_.Write32(args[2], kFakeConfig, tid);
                graphics_.Write32(call.Argument(4), 1, tid);
                return 1;
            }
            if constexpr (FunctionId == 3U) {
                graphics_.Write32(args[3], 0, tid);
                return 1;
            }
            if constexpr (FunctionId == 4U) return kFakeSurface;
            if constexpr (FunctionId == 5U) return kFakeContext;
            if constexpr (FunctionId == 6U) {
                if (graphics_.managed_surface) {
                    throw std::runtime_error(
                        "guest EGL cannot replace a host-managed ANGLE surface");
                }
                if (args[3] != 0 && !graphics_.angle_frame.has_value()) {
                    graphics_.angle_frame.emplace(gles::AngleFrame::CreatePbuffer(
                        graphics_.backend, graphics_.layout.render_width,
                        graphics_.layout.render_height));
                    graphics_.gl_owner = std::this_thread::get_id();
                    graphics_.InitializeGuestGlDefaults();
                    std::scoped_lock lock(graphics_.mutex);
                    graphics_.gpu_render_target_ready = true;
                } else if (args[3] == 0 && graphics_.gl_owner.has_value()) {
                    graphics_.ReleaseManagedSurfaceFromCallingThread();
                }
                return 1;
            }
            if constexpr (FunctionId == 7U) {
                graphics_.Write32(
                    args[3], args[2] == kEglWidth
                                 ? graphics_.layout.logical_width
                                 : args[2] == kEglHeight
                                       ? graphics_.layout.logical_height : 0,
                    tid);
                return 1;
            }
            if constexpr (FunctionId == 8U) {
                if (!graphics_.angle_frame.has_value()) {
                    throw std::runtime_error(
                        "eglSwapBuffers has no current ANGLE frame");
                }
                graphics_.PublishFrame();
                return 1;
            }
            if constexpr (FunctionId == 9U || FunctionId == 10U) return 1;
            if constexpr (FunctionId == 11U) {
                if (graphics_.managed_surface) {
                    throw std::runtime_error(
                        "guest EGL cannot terminate a host-managed ANGLE surface");
                }
                graphics_.gl_owner.reset();
                graphics_.angle_frame.reset();
                graphics_.gles_dispatch.Reset();
                graphics_.gles1_state.Reset();
                graphics_.gles1_legacy_state.Reset();
                graphics_.gles1_draw_state.Reset();
                std::scoped_lock lock(graphics_.mutex);
                graphics_.gpu_render_target_ready = false;
                return 1;
            }
            throw std::logic_error("unbound concrete libEGL export");
        }
        BoundaryCallServices& calls_;
        GraphicsBoundaryContext& graphics_;
    };

    struct Gles1Module final {
        Gles1Module(BoundaryCallServices& calls,
                    GraphicsBoundaryContext& graphics) noexcept
            : calls_(calls), graphics_(graphics) {}
        [[nodiscard]] BoundaryCallServices& CallServices() noexcept {
            return calls_;
        }
        template <gles::GlesApi Api, gles::GlesThunkId Id>
        std::uint32_t Invoke(const A32CallFrame& call) {
            constexpr bool draw_call = Api == gles::GlesApi::gles1 &&
                                       (Id == 35U || Id == 36U);
            const auto symbol = gles::DescribeGlesFunction(Api, Id).name;
            if constexpr (draw_call) {
                if (graphics_.gl_context.SelectDrawRenderer(
                        graphics_.gles1_draw_state
                            .Array(detail::kGles1VertexArray, 0x84C0U).enabled,
                        graphics_.gles_dispatch.HasEnabledVertexAttribute()) ==
                    GuestGlRenderer::programmable) {
                    const auto result = graphics_.gles_dispatch.Dispatch(
                        Id == 35U ? 40U : 41U, call,
                        graphics_.angle_frame.has_value()
                            ? &*graphics_.angle_frame : nullptr);
                    if (!result.has_value()) {
                        throw std::logic_error(
                            "selected programmable draw has no GLES2 handler");
                    }
                    std::scoped_lock lock(graphics_.mutex);
                    ++graphics_.gpu_stats.draws;
                    ++graphics_.gpu_stats.draw_targets.front().draws;
                    return *result;
                }
            }
            auto& dispatch = Api == gles::GlesApi::gles1
                                 ? graphics_.gles1_dispatch
                                 : graphics_.gles1_extensions_dispatch;
            if constexpr (!draw_call) {
                return dispatch.Invoke(Id, call.Arguments(), call.ThreadId());
            }
            graphics_.gl_context.Native().BeginFixedDraw();
            try {
                const auto result = dispatch.Invoke(
                    Id, call.Arguments(), call.ThreadId());
                graphics_.gles_dispatch.RestoreNativeState(
                    graphics_.RequireFrame(symbol));
                graphics_.gl_context.Native().EndFixedDraw();
                return result;
            } catch (...) {
                try {
                    graphics_.gles_dispatch.RestoreNativeState(
                        graphics_.RequireFrame(symbol));
                    graphics_.gl_context.Native().EndFixedDraw();
                } catch (...) {
                    graphics_.gl_context.Native().Reset();
                }
                throw;
            }
        }
    private:
        BoundaryCallServices& calls_;
        GraphicsBoundaryContext& graphics_;
    };

    struct Gles2Module final {
        Gles2Module(BoundaryCallServices& calls,
                    GraphicsBoundaryContext& graphics) noexcept
            : calls_(calls), graphics_(graphics) {}
        [[nodiscard]] BoundaryCallServices& CallServices() noexcept {
            return calls_;
        }
        template <gles::GlesThunkId FunctionId>
        std::uint32_t Invoke(const A32CallFrame& call) {
            const auto args = call.RegisterArguments();
            const auto symbol = gles::DescribeGlesFunction(
                                    gles::GlesApi::gles2, FunctionId).name;
            if (const auto shader_program =
                    DispatchShaderProgram<FunctionId>(call);
                shader_program.has_value()) {
                return *shader_program;
            }
            if (const auto resources = graphics_.gles_dispatch.Dispatch(
                    FunctionId, call,
                    graphics_.angle_frame.has_value()
                        ? &*graphics_.angle_frame : nullptr);
                resources.has_value()) {
                if constexpr (FunctionId == 40U || FunctionId == 41U) {
                    std::scoped_lock lock(graphics_.mutex);
                    ++graphics_.gpu_stats.draws;
                    ++graphics_.gpu_stats.draw_targets.front().draws;
                }
                return *resources;
            }
            if constexpr (FunctionId == 141U || FunctionId == 96U) {
                const std::array logical{
                    std::bit_cast<std::int32_t>(args[0]),
                    std::bit_cast<std::int32_t>(args[1]),
                    std::bit_cast<std::int32_t>(args[2]),
                    std::bit_cast<std::int32_t>(args[3])};
                const auto x = detail::ScaleAndroidBoundaryViewportComponent(
                    std::bit_cast<std::int32_t>(args[0]),
                    graphics_.layout.factor);
                const auto y = detail::ScaleAndroidBoundaryViewportComponent(
                    std::bit_cast<std::int32_t>(args[1]),
                    graphics_.layout.factor);
                const auto width =
                    detail::ScaleAndroidBoundaryViewportComponent(
                        std::bit_cast<std::int32_t>(args[2]),
                        graphics_.layout.factor);
                const auto height =
                    detail::ScaleAndroidBoundaryViewportComponent(
                        std::bit_cast<std::int32_t>(args[3]),
                        graphics_.layout.factor);
                if constexpr (FunctionId == 141U) {
                    graphics_.RequireFrame(symbol).Viewport(x, y, width, height);
                    graphics_.gl_context.Shared().SetViewport(logical);
                } else {
                    graphics_.RequireFrame(symbol).Scissor(x, y, width, height);
                    graphics_.gl_context.Shared().SetScissor(logical);
                }
                return 0;
            }
            if constexpr (FunctionId == 16U) {
                const std::array color{std::bit_cast<float>(args[0]),
                                       std::bit_cast<float>(args[1]),
                                       std::bit_cast<float>(args[2]),
                                       std::bit_cast<float>(args[3])};
                graphics_.RequireFrame(symbol).ClearColor(
                    color[0], color[1], color[2], color[3]);
                graphics_.gl_context.Shared().SetClearColor(color);
                return 0;
            }
            if constexpr (FunctionId == 15U) {
                graphics_.RequireFrame(symbol).Clear(args[0]);
                std::scoped_lock lock(graphics_.mutex);
                ++graphics_.gpu_stats.clears;
                return 0;
            }
            throw std::runtime_error(
                "Android boundary HLE is not implemented: " +
                std::string(symbol));
        }
    private:
        template <gles::GlesThunkId FunctionId>
        std::optional<std::uint32_t> DispatchShaderProgram(
            const A32CallFrame& call) {
            const auto args = call.RegisterArguments();
            const auto symbol = gles::DescribeGlesFunction(
                                    gles::GlesApi::gles2, FunctionId).name;
            const auto tid = call.ThreadId();
            if constexpr (FunctionId == 26U) {
                return graphics_.RequireFrame(symbol).CreateShader(args[0]);
            }
            if constexpr (FunctionId == 98U) {
                graphics_.RequireFrame(symbol).ShaderSource(
                    args[0], graphics_.ReadShaderSources(args, tid));
                return 0;
            }
            if constexpr (FunctionId == 20U) {
                graphics_.RequireFrame(symbol).CompileShader(args[0]);
                std::scoped_lock lock(graphics_.mutex);
                ++graphics_.gpu_stats.shader_compiles;
                return 0;
            }
            if constexpr (FunctionId == 70U) {
                const auto value = graphics_.RequireFrame(symbol)
                                       .GetShaderParameter(args[0], args[1]);
                graphics_.WriteRequired32(
                    args[2], std::bit_cast<std::uint32_t>(value), tid, symbol);
                return 0;
            }
            if constexpr (FunctionId == 32U) {
                graphics_.RequireFrame(symbol).DeleteShader(args[0]);
                return 0;
            }
            if constexpr (FunctionId == 25U) {
                return graphics_.RequireFrame(symbol).CreateProgram();
            }
            if constexpr (FunctionId == 1U) {
                graphics_.RequireFrame(symbol).AttachShader(args[0], args[1]);
                return 0;
            }
            if constexpr (FunctionId == 89U) {
                graphics_.RequireFrame(symbol).LinkProgram(args[0]);
                std::scoped_lock lock(graphics_.mutex);
                ++graphics_.gpu_stats.program_links;
                return 0;
            }
            if constexpr (FunctionId == 65U) {
                const auto value = graphics_.RequireFrame(symbol)
                                       .GetProgramParameter(args[0], args[1]);
                graphics_.WriteRequired32(
                    args[2], std::bit_cast<std::uint32_t>(value), tid, symbol);
                return 0;
            }
            if constexpr (FunctionId == 57U) {
                return SignedResult(graphics_.RequireFrame(symbol)
                    .GetAttribLocation(args[0], graphics_.ReadCString(
                        args[1], kMaximumGlesNameBytes, tid, symbol)));
            }
            if constexpr (FunctionId == 74U) {
                return SignedResult(graphics_.RequireFrame(symbol)
                    .GetUniformLocation(args[0], graphics_.ReadCString(
                        args[1], kMaximumGlesNameBytes, tid, symbol)));
            }
            if constexpr (FunctionId == 130U) {
                graphics_.RequireFrame(symbol).UseProgram(args[0]);
                graphics_.gl_context.Shared().SetCurrentProgram(args[0]);
                return 0;
            }
            if constexpr (FunctionId == 30U) {
                graphics_.RequireFrame(symbol).DeleteProgram(args[0]);
                return 0;
            }
            return std::nullopt;
        }
        BoundaryCallServices& calls_;
        GraphicsBoundaryContext& graphics_;
    };

    // libOpenSLES.so currently satisfies module loading only. It deliberately
    // publishes no exports until behavior-backed handlers are implemented.
    struct OpenSlesModule final {};

    struct LibcOverrideModule final {
        explicit LibcOverrideModule(BoundaryCallServices& calls) noexcept
            : calls_(calls) {}
        [[nodiscard]] BoundaryCallServices& CallServices() noexcept {
            return calls_;
        }
#define OGPLAY_DECLARE_OVERRIDE(library, symbol, id, count, method)            \
        std::uint32_t method(const A32CallFrame& call) {                       \
            return ExecuteBionicMemoryIntercept(                              \
                calls_.address_space,                                         \
                {symbol, call.RegisterArguments(), call.ThreadId()});          \
        }
        OGPLAY_GUEST_SYMBOL_OVERRIDE_EXPORTS(OGPLAY_DECLARE_OVERRIDE)
#undef OGPLAY_DECLARE_OVERRIDE
    private:
        BoundaryCallServices& calls_;
    };

    using SlowInvokeFn = std::uint32_t (*)(void*, const A32CallFrame&);

    struct HotEntry final {
        cpu::HostCallResult (*invoke)(void*, cpu::A32HostCallContext&) noexcept{};
        void* self{};
        SlowInvokeFn slow{};
        bool gpu{};
    };

    template <typename Module, auto Method, std::size_t ParameterCount,
              bool Gpu>
    static std::uint32_t InvokeSlow(void* userdata,
                                    const A32CallFrame& call) {
        return (static_cast<Module*>(userdata)->*Method)(call);
    }

    static void ServiceRecordFastFault(
        void* owner, const cpu::A32HostCallContext& context) noexcept {
        static_cast<Impl*>(owner)->RecordFastFault(context);
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

    void RecordFastFault(const cpu::A32HostCallContext& context) noexcept {
        try {
            std::scoped_lock lock(fast_fault_mutex_);
            const auto found = std::find_if(
                fast_faults_.begin(), fast_faults_.end(),
                [&](const auto& fault) {
                    return fault.thread_id == context.thread_id &&
                           fault.pc == context.pc;
                });
            if (found != fast_faults_.end()) {
                found->exception = std::current_exception();
            } else {
                fast_faults_.push_back(
                    {context.thread_id, context.pc,
                     std::current_exception()});
            }
        } catch (...) {
        }
    }

    template <typename Module, auto Method, std::size_t ParameterCount,
              bool Gpu>
    static cpu::HostCallResult InvokeFast(
        void* userdata, cpu::A32HostCallContext& context) noexcept {
        if (userdata == nullptr) return cpu::HostCallResult::unhandled;
        auto& module = *static_cast<Module*>(userdata);
        auto& services = module.CallServices();
        try {
            const A32CallFrame call(services.address_space, context,
                                    ParameterCount);
            const auto arguments = call.RegisterArguments();
            const auto result = (module.*Method)(call);
            if constexpr (Gpu) {
                const auto slot = static_cast<std::size_t>(
                    (context.pc.Value() - kBionicHleThunkBegin) /
                    kThunkStride);
                services.RecordGpuCall(slot, arguments, true);
            }
            context.registers[0] = result;
            return cpu::HostCallResult::handled;
        } catch (...) {
            services.RecordFastFault(context);
            return cpu::HostCallResult::fault;
        }
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
        const auto slot = static_cast<std::size_t>(
            ((found->address.Value() & ~UINT32_C(1)) -
             kBionicHleThunkBegin) / kThunkStride);
        if (slot >= hot_.size() || hot_[slot].invoke != nullptr) {
            throw std::logic_error("boundary hot slot is invalid or already bound");
        }
        hot_[slot] = {&InvokeFast<Module, Method, ParameterCount, Gpu>,
                      &module,
                      &InvokeSlow<Module, Method, ParameterCount, Gpu>, Gpu};
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
        if (hot_[slot].invoke != nullptr) {
            throw std::logic_error("guest symbol override slot is already bound");
        }
        hot_[slot] = {&InvokeFast<Module, Method, ParameterCount, Gpu>,
                      &module,
                      &InvokeSlow<Module, Method, ParameterCount, Gpu>, Gpu};
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
        hot_.resize(descriptors_.size());
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
        const auto& gles2 = require("libGLESv2.so");
        BindGles2(gles2, std::make_index_sequence<
                              gles::generated::gles2::kFunctions.size()>{});
        const auto& log = require("liblog.so");
#define OGPLAY_BIND_LOG(name, id, count, method)                                \
        BindExport<LogModule, &LogModule::method, count, false>(                \
            log, name, log_module_);
        OGPLAY_LOG_BOUNDARY_EXPORTS(OGPLAY_BIND_LOG)
#undef OGPLAY_BIND_LOG

#define OGPLAY_BIND_OVERRIDE(library, symbol, id, count, method)               \
        BindOverride<LibcOverrideModule, &LibcOverrideModule::method, count,   \
                     false>(library, symbol, libc_override_module_);
        OGPLAY_GUEST_SYMBOL_OVERRIDE_EXPORTS(OGPLAY_BIND_OVERRIDE)
#undef OGPLAY_BIND_OVERRIDE
        if (std::any_of(hot_.begin(), hot_.end(),
                        [](const auto& entry) {
                            return entry.invoke == nullptr || entry.slow == nullptr;
                        })) {
            throw std::logic_error("boundary seal left an export unbound");
        }
    }

    static cpu::HostCallResult TryFastHostCall(
        void* userdata, const std::uint32_t svc,
        cpu::A32HostCallContext& call) noexcept {
        if (userdata == nullptr || svc != 2U) {
            return cpu::HostCallResult::unhandled;
        }
        return static_cast<Impl*>(userdata)->TryFastCall(call);
    }

    cpu::HostCallResult TryFastCall(cpu::A32HostCallContext& call) noexcept {
        if (!mapped_) return cpu::HostCallResult::unhandled;
        const auto pc = call.pc.Value();
        if (pc < kBionicHleThunkBegin ||
            pc >= kBionicHleThunkBegin + thunk_bytes_) {
            return cpu::HostCallResult::unhandled;
        }
        const auto offset = pc - kBionicHleThunkBegin;
        if ((offset % kThunkStride) != 0U) {
            return cpu::HostCallResult::unhandled;
        }
        const auto slot = static_cast<std::size_t>(offset / kThunkStride);
        if (slot >= hot_.size() || hot_[slot].invoke == nullptr) {
            return cpu::HostCallResult::unhandled;
        }
        return hot_[slot].invoke(hot_[slot].self, call);
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
        std::vector<std::uint8_t> readback;
        if (layout_.factor == 1U) {
            std::scoped_lock lock(mutex_);
            readback = std::move(recycled_rgba8_);
        }
        RequireFrame("present").ReadRgba8(readback);
        AndroidBoundaryFrame frame{
            layout_.logical_width, layout_.logical_height,
            ++frame_sequence_,
            gles::ResolveSupersampledRgba8(std::move(readback), layout_)};
        {
            std::scoped_lock lock(mutex_);
            if (layout_.factor == 1U && latest_frame_.has_value() &&
                latest_frame_->rgba8.capacity() >=
                    recycled_rgba8_.capacity()) {
                recycled_rgba8_ = std::move(latest_frame_->rgba8);
            }
            latest_frame_ = std::move(frame);
        }
        ready_.notify_all();
    }
    void RecordGpuCall(const std::size_t descriptor_index,
                       const std::array<std::uint32_t, 4>& args,
                       const bool gpu) {
        if (!gpu) return;
        if (descriptor_index >= descriptors_.size() ||
            descriptor_index > (std::numeric_limits<std::uint16_t>::max)()) {
            throw std::logic_error("GPU trace descriptor is outside its catalog");
        }
        std::scoped_lock lock(trace_mutex_);
        gpu_trace_[gpu_trace_write_] = {
            static_cast<std::uint16_t>(descriptor_index), args};
        gpu_trace_write_ = (gpu_trace_write_ + 1U) % gpu_trace_.size();
        gpu_trace_count_ = std::min(gpu_trace_count_ + 1U, gpu_trace_.size());
    }
    struct RawGpuTraceEntry final {
        std::uint16_t descriptor_index{};
        std::array<std::uint32_t, 4> registers{};
    };
    memory::AddressSpace& address_space_;
    gles::AngleBackend backend_;
    gles::SupersampleLayout layout_;
    std::vector<BionicHleSymbol> symbols_;
    std::vector<detail::HleThunkDescriptor> descriptors_;
    BionicHleSymbolProvider provider_;
    GuestGlContext gl_context_;
    AndroidBoundaryGles gles_dispatch_;
    detail::AndroidBoundaryGles1State gles1_state_;
    detail::AndroidBoundaryGles1DrawState gles1_draw_state_;
    detail::AndroidBoundaryGles1QueryStrings gles1_query_strings_{address_space_};
    detail::AndroidBoundaryGles1LegacyState gles1_legacy_state_;
    gles::GlesDispatchTable gles1_dispatch_{gles::GlesApi::gles1};
    gles::GlesDispatchTable gles1_extensions_dispatch_{
        gles::GlesApi::gles1_extensions};
    bool mapped_{};
    std::size_t thunk_bytes_{};
    std::vector<HotEntry> hot_;
    struct PendingFastFault final {
        std::uint64_t thread_id{};
        memory::GuestAddress pc{};
        std::exception_ptr exception;
    };
    std::mutex fast_fault_mutex_;
    std::vector<PendingFastFault> fast_faults_;
    std::optional<gles::AngleFrame> angle_frame_;
    std::optional<std::thread::id> gl_owner_;
    bool managed_surface_{};
    std::uint64_t frame_sequence_{};
    mutable std::mutex mutex_;
    mutable std::mutex trace_mutex_;
    std::condition_variable ready_;
    std::optional<AndroidBoundaryFrame> latest_frame_;
    std::vector<std::uint8_t> recycled_rgba8_;
    core::GpuStats gpu_stats_{0, 0, 0, 0, 0, {{0, 0, "color0"}}};
    std::array<RawGpuTraceEntry, 2048> gpu_trace_{};
    std::size_t gpu_trace_write_{};
    std::size_t gpu_trace_count_{};
    bool gpu_render_target_ready_{};
    BoundaryCallServices call_services_;
    AndroidBoundaryServices android_services_;
    GraphicsBoundaryContext graphics_context_;
    AndroidModule android_module_;
    EglModule egl_module_;
    Gles1Module gles1_module_;
    Gles2Module gles2_module_;
    LogBoundaryContext log_context_;
    LogModule log_module_;
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
