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
#include <type_traits>
#include <utility>

#include "ogplay/gles/angle_frame.h"
#include "ogplay/gles/gles_dispatch.h"
#include "ogplay/gles/guest_transfer.h"
#include "ogplay/gles/supersample.h"
#include "ogplay/runtime/boundary/android_boundary_gles.h"
#include "ogplay/runtime/boundary/a32_call_frame.h"
#include "ogplay/runtime/boundary/guest_gl_context.h"
#include "android_boundary_gles1.h"
#include "android_boundary_gles1_draw.h"
#include "android_boundary_gles1_fixed.h"
#include "android_boundary_gles1_query.h"
#include "android_boundary_symbols.h"

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

enum class AndroidFunction : std::uint16_t {
    configuration_new = 0,
    configuration_delete = 1,
    configuration_from_asset_manager = 2,
    configuration_get_language = 3,
    configuration_get_country = 4,
    looper_prepare = 5,
    looper_add_fd = 6,
    looper_poll_all = 7,
    input_queue_attach_looper = 8,
    input_queue_detach_looper = 9,
    input_queue_get_event = 10,
    input_queue_pre_dispatch_event = 11,
    input_queue_finish_event = 12,
    input_event_get_type = 13,
    key_event_get_action = 14,
    key_event_get_key_code = 15,
    motion_event_get_action = 16,
    motion_event_get_x = 17,
    motion_event_get_y = 18,
    native_window_set_buffers_geometry = 19,
};

enum class EglFunction : std::uint16_t {
    get_display = 0,
    initialize = 1,
    choose_config = 2,
    get_config_attrib = 3,
    create_window_surface = 4,
    create_context = 5,
    make_current = 6,
    query_surface = 7,
    swap_buffers = 8,
    destroy_context = 9,
    destroy_surface = 10,
    terminate = 11,
};

enum class Gles2Function : gles::GlesThunkId {
    attach_shader = 1, clear = 15, clear_color = 16, compile_shader = 20,
    create_program = 25, create_shader = 26, delete_program = 30,
    delete_shader = 32, draw_arrays = 40, draw_elements = 41,
    get_attrib_location = 57, get_program_iv = 65, get_shader_iv = 70,
    get_uniform_location = 74, link_program = 89, scissor = 96,
    shader_source = 98, use_program = 130, viewport = 141,
};

[[nodiscard]] constexpr std::uint16_t Id(const AndroidFunction function) {
    return static_cast<std::uint16_t>(function);
}
[[nodiscard]] constexpr std::uint16_t Id(const EglFunction function) {
    return static_cast<std::uint16_t>(function);
}
[[nodiscard]] constexpr gles::GlesThunkId Id(const Gles2Function function) {
    return static_cast<gles::GlesThunkId>(function);
}

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
          gles1_state_(gl_context_.Shared()) {
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
        SealModuleInstances();
        hot_bindings_.reserve(descriptors_.size());
        hot_.reserve(descriptors_.size());
        for (const auto& descriptor : descriptors_) {
            hot_bindings_.push_back(MakeBinding(descriptor));
        }
        for (auto& binding : hot_bindings_) {
            hot_.push_back({&InvokeLegacyFast, &binding});
        }
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
        const auto& binding = hot_bindings_.at(descriptor_index);
        auto state = cpu.GetState();
        const A32CallFrame call(address_space_, state,
                                descriptor->parameter_count);
        const auto arguments = call.RegisterArguments();
        std::uint32_t result{};
        try {
            result = binding.invoke(*this, *descriptor, call);
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
        RecordGpuCall(*descriptor, arguments, binding.gpu);
        state.SetRegister(cpu::CoreRegister::r0, result);
        cpu.SetState(state);
        return true;
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
    struct AndroidModule final {};
    struct EglModule final {};
    struct Gles1Module final {};
    struct Gles1ExtensionModule final {};
    struct Gles2Module final {};
    struct LogModule final {};

    void SealModuleInstances() {
        const auto& catalog = AndroidBoundaryCatalog(AndroidApi::api19);
        module_instances_.reserve(catalog.Modules().size());
        for (const auto& module : catalog.Modules()) {
            void* instance{};
            if (module.soname == "libandroid.so") instance = &android_module_;
            else if (module.soname == "libEGL.so") instance = &egl_module_;
            else if (module.soname == "libGLESv1_CM.so") instance = &gles1_module_;
            else if (module.soname == "libGLESv2.so") instance = &gles2_module_;
            else if (module.soname == "liblog.so") instance = &log_module_;
            if (instance == nullptr) {
                throw std::logic_error("boundary catalog module has no instance");
            }
            module_instances_.push_back({&module, instance});
        }
    }

    using ModuleInvokeFn = std::uint32_t (*)(
        Impl&, const detail::HleThunkDescriptor&, const A32CallFrame&);

    struct FastBinding final {
        Impl* owner{};
        const detail::HleThunkDescriptor* descriptor{};
        ModuleInvokeFn invoke{};
        bool gpu{};
    };

    struct HotEntry final {
        cpu::HostCallResult (*invoke)(void*, cpu::A32HostCallContext&) noexcept{};
        void* self{};
    };

    FastBinding MakeBinding(
        const detail::HleThunkDescriptor& descriptor) {
        if (descriptor.library == "libc.so") {
            return {this, &descriptor, &InvokeLibc, false};
        }
        if (descriptor.library == "libandroid.so") {
            return {this, &descriptor, &InvokeAndroid, false};
        }
        if (descriptor.library == "libEGL.so") {
            return {this, &descriptor, &InvokeEgl, true};
        }
        if (descriptor.library == "libGLESv1_CM.so") {
            const auto core_count =
                gles::GlesFunctionCount(gles::GlesApi::gles1);
            return {this, &descriptor,
                    descriptor.local_id < core_count
                        ? &InvokeGles1
                        : &InvokeGles1Extension,
                    true};
        }
        if (descriptor.library == "libGLESv2.so") {
            return {this, &descriptor, &InvokeGles2, true};
        }
        if (descriptor.library == "liblog.so") {
            return {this, &descriptor, &InvokeLog, false};
        }
        throw std::logic_error("boundary descriptor has no module binding");
    }

    static std::uint32_t InvokeLibc(
        Impl& self, const detail::HleThunkDescriptor& descriptor,
        const A32CallFrame& call) {
        return ExecuteBionicMemoryIntercept(
            self.address_space_, {descriptor.name, call.RegisterArguments(),
                                  call.ThreadId()});
    }
    static std::uint32_t InvokeAndroid(
        Impl& self, const detail::HleThunkDescriptor& descriptor,
        const A32CallFrame& call) {
        return self.InvokeModule<AndroidModule>(descriptor, call);
    }
    static std::uint32_t InvokeEgl(
        Impl& self, const detail::HleThunkDescriptor& descriptor,
        const A32CallFrame& call) {
        return self.InvokeModule<EglModule>(descriptor, call);
    }
    static std::uint32_t InvokeGles1(
        Impl& self, const detail::HleThunkDescriptor& descriptor,
        const A32CallFrame& call) {
        return self.InvokeModule<Gles1Module>(descriptor, call);
    }
    static std::uint32_t InvokeGles1Extension(
        Impl& self, const detail::HleThunkDescriptor& descriptor,
        const A32CallFrame& call) {
        return self.InvokeModule<Gles1ExtensionModule>(descriptor, call);
    }
    static std::uint32_t InvokeGles2(
        Impl& self, const detail::HleThunkDescriptor& descriptor,
        const A32CallFrame& call) {
        return self.InvokeModule<Gles2Module>(descriptor, call);
    }
    static std::uint32_t InvokeLog(
        Impl&, const detail::HleThunkDescriptor& descriptor,
        const A32CallFrame&) {
        if (descriptor.local_id >= 2U) {
            throw std::logic_error("liblog local id is outside its module");
        }
        return 0U;
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

    static cpu::HostCallResult InvokeLegacyFast(
        void* userdata, cpu::A32HostCallContext& context) noexcept {
        if (userdata == nullptr) return cpu::HostCallResult::unhandled;
        auto& binding = *static_cast<FastBinding*>(userdata);
        try {
            const A32CallFrame call(binding.owner->address_space_, context,
                                    binding.descriptor->parameter_count);
            const auto arguments = call.RegisterArguments();
            const auto result = binding.invoke(
                *binding.owner, *binding.descriptor, call);
            binding.owner->RecordGpuCall(*binding.descriptor, arguments,
                                         binding.gpu);
            context.registers[0] = result;
            return cpu::HostCallResult::handled;
        } catch (...) {
            std::scoped_lock lock(binding.owner->fast_fault_mutex_);
            const auto found = std::find_if(
                binding.owner->fast_faults_.begin(),
                binding.owner->fast_faults_.end(),
                [&](const auto& fault) {
                    return fault.thread_id == context.thread_id &&
                           fault.pc == context.pc;
                });
            if (found != binding.owner->fast_faults_.end()) {
                found->exception = std::current_exception();
            } else {
                binding.owner->fast_faults_.push_back(
                    {context.thread_id, context.pc,
                     std::current_exception()});
            }
            return cpu::HostCallResult::fault;
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
    std::uint32_t PollAll(const std::array<std::uint32_t, 4>& args,
                          const std::uint64_t thread_id) {
        const auto timeout = std::bit_cast<std::int32_t>(args[0]);
        std::unique_lock lock(mutex_);
        const auto has_source = [this] {
            return pending_command_writes_ != 0 || !inputs_.empty();
        };
        if (!has_source()) {
            if (timeout < 0) ready_.wait(lock, has_source);
            else if (timeout > 0) ready_.wait_for(lock, std::chrono::milliseconds(timeout), has_source);
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
        Write32(args[1], 0, thread_id);
        Write32(args[2], 1, thread_id);
        Write32(args[3], data, thread_id);
        return ident;
    }
    template <typename Module>
    std::uint32_t InvokeModule(
        const detail::HleThunkDescriptor& descriptor,
        const A32CallFrame& call) {
        const auto args = call.RegisterArguments();
        const auto symbol = descriptor.name;
        auto function_id = descriptor.local_id;
        if constexpr (std::is_same_v<Module, Gles1ExtensionModule>) {
            function_id = static_cast<std::uint16_t>(
                function_id -
                gles::GlesFunctionCount(gles::GlesApi::gles1));
        }
        const auto tid = call.ThreadId();
        if constexpr (std::is_same_v<Module, Gles1Module> ||
                      std::is_same_v<Module, Gles1ExtensionModule>) {
            const auto draw_call =
                std::is_same_v<Module, Gles1Module> &&
                (function_id == 35U || function_id == 36U);
            if (draw_call &&
                gl_context_.SelectDrawRenderer(
                    gles1_draw_state_
                        .Array(detail::kGles1VertexArray, 0x84C0U)
                        .enabled,
                    gles_dispatch_.HasEnabledVertexAttribute()) ==
                    GuestGlRenderer::programmable) {
                const auto result = gles_dispatch_.Dispatch(
                    function_id == 35U ? Id(Gles2Function::draw_arrays)
                                       : Id(Gles2Function::draw_elements),
                    call,
                    angle_frame_.has_value() ? &*angle_frame_ : nullptr);
                if (!result.has_value()) {
                    throw std::logic_error(
                        "selected programmable draw has no GLES2 handler");
                }
                std::scoped_lock lock(mutex_);
                ++gpu_stats_.draws;
                ++gpu_stats_.draw_targets.front().draws;
                return *result;
            }
            const auto all = call.Arguments();
            auto& dispatch = std::is_same_v<Module, Gles1Module>
                                 ? gles1_dispatch_
                                 : gles1_extensions_dispatch_;
            if (!draw_call) return dispatch.Invoke(function_id, all, tid);
            gl_context_.Native().BeginFixedDraw();
            try {
                const auto result = dispatch.Invoke(function_id, all, tid);
                gles_dispatch_.RestoreNativeState(RequireFrame(symbol));
                gl_context_.Native().EndFixedDraw();
                return result;
            } catch (...) {
                try {
                    gles_dispatch_.RestoreNativeState(RequireFrame(symbol));
                    gl_context_.Native().EndFixedDraw();
                } catch (...) {
                    gl_context_.Native().Reset();
                }
                throw;
            }
        }
        if (std::is_same_v<Module, AndroidModule> &&
            function_id == Id(AndroidFunction::configuration_new)) {
            return kFakeConfiguration;
        }
        if (std::is_same_v<Module, AndroidModule> &&
            (function_id == Id(AndroidFunction::configuration_delete) ||
             function_id == Id(AndroidFunction::configuration_from_asset_manager))) {
            return 0;
        }
        if (std::is_same_v<Module, AndroidModule> &&
            (function_id == Id(AndroidFunction::configuration_get_language) ||
             function_id == Id(AndroidFunction::configuration_get_country))) {
            if (args[1] != 0) {
                const std::array bytes{std::byte{'e'}, std::byte{'n'}};
                address_space_.Write(memory::GuestAddress{args[1]}, bytes, tid);
            }
            return 0;
        }
        if (std::is_same_v<Module, AndroidModule> &&
            function_id == Id(AndroidFunction::looper_prepare)) return kFakeLooper;
        if (std::is_same_v<Module, AndroidModule> &&
            function_id == Id(AndroidFunction::looper_add_fd)) {
            std::scoped_lock lock(mutex_);
            command_ident_ = args[2];
            command_data_ = call.Argument(5);
            return 1;
        }
        if (std::is_same_v<Module, AndroidModule> &&
            function_id == Id(AndroidFunction::looper_poll_all)) {
            return PollAll(args, tid);
        }
        if (std::is_same_v<Module, AndroidModule> &&
            function_id == Id(AndroidFunction::input_queue_attach_looper)) {
            std::scoped_lock lock(mutex_);
            input_ident_ = args[2];
            input_data_ = call.Argument(4);
            return 0;
        }
        if (std::is_same_v<Module, AndroidModule> &&
            function_id == Id(AndroidFunction::input_queue_detach_looper)) return 0;
        if (std::is_same_v<Module, AndroidModule> &&
            function_id == Id(AndroidFunction::input_queue_get_event)) {
            std::scoped_lock lock(mutex_);
            if (inputs_.empty()) return SignedResult(-1);
            active_input_ = inputs_.front();
            inputs_.pop_front();
            Write32(args[1], kFakeInputEvent, tid);
            return 0;
        }
        if (std::is_same_v<Module, AndroidModule> &&
            function_id == Id(AndroidFunction::input_queue_pre_dispatch_event)) return 0;
        if (std::is_same_v<Module, AndroidModule> &&
            function_id == Id(AndroidFunction::input_queue_finish_event)) {
            std::scoped_lock lock(mutex_);
            active_input_.reset();
            return 0;
        }
        if (std::is_same_v<Module, AndroidModule> &&
            function_id == Id(AndroidFunction::input_event_get_type)) {
            std::scoped_lock lock(mutex_);
            return active_input_.has_value() && active_input_->type == AndroidBoundaryInputType::key
                       ? 1U : 2U;
        }
        if (std::is_same_v<Module, AndroidModule> &&
            function_id == Id(AndroidFunction::key_event_get_action)) {
            std::scoped_lock lock(mutex_);
            return active_input_.has_value() && active_input_->pressed ? 0U : 1U;
        }
        if (std::is_same_v<Module, AndroidModule> &&
            function_id == Id(AndroidFunction::key_event_get_key_code)) {
            std::scoped_lock lock(mutex_);
            return active_input_.has_value() ? static_cast<std::uint32_t>(active_input_->code) : 0U;
        }
        if (std::is_same_v<Module, AndroidModule> &&
            function_id == Id(AndroidFunction::motion_event_get_action)) {
            std::scoped_lock lock(mutex_);
            if (!active_input_.has_value() ||
                active_input_->type == AndroidBoundaryInputType::pointer_motion) return 2U;
            return active_input_->pressed ? 0U : 1U;
        }
        if (std::is_same_v<Module, AndroidModule> &&
            (function_id == Id(AndroidFunction::motion_event_get_x) ||
             function_id == Id(AndroidFunction::motion_event_get_y))) {
            std::scoped_lock lock(mutex_);
            const auto value = !active_input_.has_value() ? 0.0F
                : function_id == Id(AndroidFunction::motion_event_get_x)
                      ? active_input_->x : active_input_->y;
            return std::bit_cast<std::uint32_t>(value);
        }
        if (std::is_same_v<Module, AndroidModule> &&
            function_id == Id(AndroidFunction::native_window_set_buffers_geometry)) return 0;
        if (std::is_same_v<Module, EglModule> &&
            function_id == Id(EglFunction::get_display)) return kFakeDisplay;
        if (std::is_same_v<Module, EglModule> &&
            function_id == Id(EglFunction::initialize)) {
            Write32(args[1], 1, tid); Write32(args[2], 5, tid); return 1;
        }
        if (std::is_same_v<Module, EglModule> &&
            function_id == Id(EglFunction::choose_config)) {
            Write32(args[2], kFakeConfig, tid);
            Write32(call.Argument(4), 1, tid);
            return 1;
        }
        if (std::is_same_v<Module, EglModule> &&
            function_id == Id(EglFunction::get_config_attrib)) {
            Write32(args[3], 0, tid); return 1;
        }
        if (std::is_same_v<Module, EglModule> &&
            function_id == Id(EglFunction::create_window_surface)) return kFakeSurface;
        if (std::is_same_v<Module, EglModule> &&
            function_id == Id(EglFunction::create_context)) return kFakeContext;
        if (std::is_same_v<Module, EglModule> &&
            function_id == Id(EglFunction::make_current)) {
            if (managed_surface_) {
                throw std::runtime_error(
                    "guest EGL cannot replace a host-managed ANGLE surface");
            }
            if (args[3] != 0 && !angle_frame_.has_value()) {
                angle_frame_.emplace(gles::AngleFrame::CreatePbuffer(
                    backend_, layout_.render_width, layout_.render_height));
                gl_owner_ = std::this_thread::get_id();
                InitializeGuestGlDefaults();
                std::scoped_lock lock(mutex_);
                gpu_render_target_ready_ = true;
            } else if (args[3] == 0 && gl_owner_.has_value()) {
                ReleaseManagedSurfaceFromCallingThread();
            }
            return 1;
        }
        if (std::is_same_v<Module, EglModule> &&
            function_id == Id(EglFunction::query_surface)) {
            Write32(args[3], args[2] == kEglWidth ? layout_.logical_width :
                             args[2] == kEglHeight ? layout_.logical_height : 0, tid);
            return 1;
        }
        if (std::is_same_v<Module, EglModule> &&
            function_id == Id(EglFunction::swap_buffers)) {
            if (!angle_frame_.has_value()) throw std::runtime_error("eglSwapBuffers has no current ANGLE frame");
            PublishFrame();
            return 1;
        }
        if (std::is_same_v<Module, EglModule> &&
            (function_id == Id(EglFunction::destroy_context) ||
             function_id == Id(EglFunction::destroy_surface))) return 1;
        if (std::is_same_v<Module, EglModule> &&
            function_id == Id(EglFunction::terminate)) {
            if (managed_surface_) {
                throw std::runtime_error(
                    "guest EGL cannot terminate a host-managed ANGLE surface");
            }
            gl_owner_.reset();
            angle_frame_.reset();
            gles_dispatch_.Reset();
            gles1_state_.Reset();
            gles1_legacy_state_.Reset();
            gles1_draw_state_.Reset();
            std::scoped_lock lock(mutex_);
            gpu_render_target_ready_ = false;
            return 1;
        }
        if constexpr (!std::is_same_v<Module, Gles2Module>) {
            throw std::runtime_error(
                "Android boundary HLE function id is not implemented");
        }
        if (const auto shader_program = DispatchShaderProgram(function_id, call);
            shader_program.has_value()) {
            return *shader_program;
        }
        if (const auto resources = gles_dispatch_.Dispatch(
                function_id, call,
                angle_frame_.has_value() ? &*angle_frame_ : nullptr);
            resources.has_value()) {
            if (function_id == Id(Gles2Function::draw_elements) ||
                function_id == Id(Gles2Function::draw_arrays)) {
                std::scoped_lock lock(mutex_);
                ++gpu_stats_.draws;
                ++gpu_stats_.draw_targets.front().draws;
            }
            return *resources;
        }
        if (function_id == Id(Gles2Function::viewport)) {
            const std::array logical{
                std::bit_cast<std::int32_t>(args[0]),
                std::bit_cast<std::int32_t>(args[1]),
                std::bit_cast<std::int32_t>(args[2]),
                std::bit_cast<std::int32_t>(args[3])};
            RequireFrame(symbol).Viewport(
                detail::ScaleAndroidBoundaryViewportComponent(
                    std::bit_cast<std::int32_t>(args[0]), layout_.factor),
                detail::ScaleAndroidBoundaryViewportComponent(
                    std::bit_cast<std::int32_t>(args[1]), layout_.factor),
                detail::ScaleAndroidBoundaryViewportComponent(
                    std::bit_cast<std::int32_t>(args[2]), layout_.factor),
                detail::ScaleAndroidBoundaryViewportComponent(
                    std::bit_cast<std::int32_t>(args[3]), layout_.factor));
            gl_context_.Shared().SetViewport(logical);
            return 0;
        }
        if (function_id == Id(Gles2Function::scissor)) {
            const std::array logical{
                std::bit_cast<std::int32_t>(args[0]),
                std::bit_cast<std::int32_t>(args[1]),
                std::bit_cast<std::int32_t>(args[2]),
                std::bit_cast<std::int32_t>(args[3])};
            RequireFrame(symbol).Scissor(
                detail::ScaleAndroidBoundaryViewportComponent(
                    std::bit_cast<std::int32_t>(args[0]), layout_.factor),
                detail::ScaleAndroidBoundaryViewportComponent(
                    std::bit_cast<std::int32_t>(args[1]), layout_.factor),
                detail::ScaleAndroidBoundaryViewportComponent(
                    std::bit_cast<std::int32_t>(args[2]), layout_.factor),
                detail::ScaleAndroidBoundaryViewportComponent(
                    std::bit_cast<std::int32_t>(args[3]), layout_.factor));
            gl_context_.Shared().SetScissor(logical);
            return 0;
        }
        if (function_id == Id(Gles2Function::clear_color)) {
            const std::array color{std::bit_cast<float>(args[0]),
                                   std::bit_cast<float>(args[1]),
                                   std::bit_cast<float>(args[2]),
                                   std::bit_cast<float>(args[3])};
            RequireFrame(symbol).ClearColor(color[0], color[1], color[2], color[3]);
            gl_context_.Shared().SetClearColor(color);
            return 0;
        }
        if (function_id == Id(Gles2Function::clear)) {
            RequireFrame(symbol).Clear(args[0]);
            std::scoped_lock lock(mutex_);
            ++gpu_stats_.clears;
            return 0;
        }
        throw std::runtime_error("Android boundary HLE is not implemented: " + std::string(symbol));
    }
    std::optional<std::uint32_t> DispatchShaderProgram(
        const gles::GlesThunkId function_id,
        const A32CallFrame& call) {
        const auto args = call.RegisterArguments();
        const auto symbol = gles::DescribeGlesFunction(
                                gles::GlesApi::gles2, function_id).name;
        const auto tid = call.ThreadId();
        if (function_id == Id(Gles2Function::create_shader)) {
            return RequireFrame(symbol).CreateShader(args[0]);
        }
        if (function_id == Id(Gles2Function::shader_source)) {
            RequireFrame(symbol).ShaderSource(args[0], ReadShaderSources(args, tid)); return 0;
        }
        if (function_id == Id(Gles2Function::compile_shader)) {
            RequireFrame(symbol).CompileShader(args[0]); std::scoped_lock lock(mutex_);
            ++gpu_stats_.shader_compiles; return 0;
        }
        if (function_id == Id(Gles2Function::get_shader_iv)) {
            const auto value = RequireFrame(symbol).GetShaderParameter(args[0], args[1]);
            WriteRequired32(args[2], std::bit_cast<std::uint32_t>(value), tid, symbol); return 0;
        }
        if (function_id == Id(Gles2Function::delete_shader)) {
            RequireFrame(symbol).DeleteShader(args[0]); return 0;
        }
        if (function_id == Id(Gles2Function::create_program)) {
            return RequireFrame(symbol).CreateProgram();
        }
        if (function_id == Id(Gles2Function::attach_shader)) {
            RequireFrame(symbol).AttachShader(args[0], args[1]); return 0;
        }
        if (function_id == Id(Gles2Function::link_program)) {
            RequireFrame(symbol).LinkProgram(args[0]); std::scoped_lock lock(mutex_);
            ++gpu_stats_.program_links; return 0;
        }
        if (function_id == Id(Gles2Function::get_program_iv)) {
            const auto value = RequireFrame(symbol).GetProgramParameter(args[0], args[1]);
            WriteRequired32(args[2], std::bit_cast<std::uint32_t>(value), tid, symbol); return 0;
        }
        if (function_id == Id(Gles2Function::get_attrib_location)) {
            return SignedResult(RequireFrame(symbol).GetAttribLocation(
                args[0], ReadCString(args[1], kMaximumGlesNameBytes, tid, symbol)));
        }
        if (function_id == Id(Gles2Function::get_uniform_location)) {
            return SignedResult(RequireFrame(symbol).GetUniformLocation(
                args[0], ReadCString(args[1], kMaximumGlesNameBytes, tid, symbol)));
        }
        if (function_id == Id(Gles2Function::use_program)) {
            RequireFrame(symbol).UseProgram(args[0]);
            gl_context_.Shared().SetCurrentProgram(args[0]);
            return 0;
        }
        if (function_id == Id(Gles2Function::delete_program)) {
            RequireFrame(symbol).DeleteProgram(args[0]); return 0;
        }
        return std::nullopt;
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
    void RecordGpuCall(const detail::HleThunkDescriptor& descriptor,
                       const std::array<std::uint32_t, 4>& args,
                       const bool gpu) {
        if (!gpu) return;
        const auto descriptor_index = static_cast<std::size_t>(
            &descriptor - descriptors_.data());
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
    std::vector<FastBinding> hot_bindings_;
    std::vector<HotEntry> hot_;
    AndroidModule android_module_;
    EglModule egl_module_;
    Gles1Module gles1_module_;
    Gles2Module gles2_module_;
    LogModule log_module_;
    std::vector<BoundaryModuleInstance> module_instances_;
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
    std::uint64_t pending_command_writes_{};
    std::uint32_t command_ident_{};
    std::uint32_t command_data_{};
    std::uint32_t input_ident_{};
    std::uint32_t input_data_{};
    std::deque<AndroidBoundaryInput> inputs_;
    std::optional<AndroidBoundaryInput> active_input_;
    std::optional<AndroidBoundaryFrame> latest_frame_;
    std::vector<std::uint8_t> recycled_rgba8_;
    core::GpuStats gpu_stats_{0, 0, 0, 0, 0, {{0, 0, "color0"}}};
    std::array<RawGpuTraceEntry, 2048> gpu_trace_{};
    std::size_t gpu_trace_write_{};
    std::size_t gpu_trace_count_{};
    bool gpu_render_target_ready_{};
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
