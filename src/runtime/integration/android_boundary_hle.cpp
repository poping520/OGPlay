#include "ogplay/runtime/integration/android_boundary_hle.h"

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <limits>
#include <mutex>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include "ogplay/gles/angle_frame.h"
#include "ogplay/gles/supersample.h"

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

std::vector<BionicHleSymbol> BuildSymbols() {
    static constexpr std::array<std::pair<std::string_view, std::string_view>, 42> names{{
        {"libc.so", "memcpy"}, {"libc.so", "memmove"}, {"libc.so", "memset"},
        {"libc.so", "memcmp"}, {"libc.so", "strlen"},
        {"libandroid.so", "AConfiguration_new"},
        {"libandroid.so", "AConfiguration_delete"},
        {"libandroid.so", "AConfiguration_fromAssetManager"},
        {"libandroid.so", "AConfiguration_getLanguage"},
        {"libandroid.so", "AConfiguration_getCountry"},
        {"libandroid.so", "ALooper_prepare"},
        {"libandroid.so", "ALooper_addFd"},
        {"libandroid.so", "ALooper_pollAll"},
        {"libandroid.so", "AInputQueue_attachLooper"},
        {"libandroid.so", "AInputQueue_detachLooper"},
        {"libandroid.so", "AInputQueue_getEvent"},
        {"libandroid.so", "AInputQueue_preDispatchEvent"},
        {"libandroid.so", "AInputQueue_finishEvent"},
        {"libandroid.so", "AInputEvent_getType"},
        {"libandroid.so", "AKeyEvent_getAction"},
        {"libandroid.so", "AKeyEvent_getKeyCode"},
        {"libandroid.so", "AMotionEvent_getAction"},
        {"libandroid.so", "AMotionEvent_getX"},
        {"libandroid.so", "AMotionEvent_getY"},
        {"libandroid.so", "ANativeWindow_setBuffersGeometry"},
        {"libEGL.so", "eglGetDisplay"}, {"libEGL.so", "eglInitialize"},
        {"libEGL.so", "eglChooseConfig"}, {"libEGL.so", "eglGetConfigAttrib"},
        {"libEGL.so", "eglCreateWindowSurface"}, {"libEGL.so", "eglCreateContext"},
        {"libEGL.so", "eglMakeCurrent"}, {"libEGL.so", "eglQuerySurface"},
        {"libEGL.so", "eglSwapBuffers"}, {"libEGL.so", "eglDestroyContext"},
        {"libEGL.so", "eglDestroySurface"}, {"libEGL.so", "eglTerminate"},
        {"libGLESv2.so", "glViewport"}, {"libGLESv2.so", "glClearColor"},
        {"libGLESv2.so", "glClear"}, {"liblog.so", "__android_log_print"},
        {"liblog.so", "__android_log_write"},
    }};
    std::vector<BionicHleSymbol> result;
    result.reserve(names.size());
    for (std::size_t index = 0; index < names.size(); ++index) {
        result.push_back({std::string(names[index].first), std::string(names[index].second),
                          memory::GuestAddress{kBionicHleThunkBegin +
                                               static_cast<std::uint32_t>(index) *
                                                   kThunkStride + 1U}});
    }
    return result;
}

std::uint32_t SignedResult(const std::int32_t value) noexcept {
    return std::bit_cast<std::uint32_t>(value);
}

std::int32_t ScaleViewportComponent(const std::int32_t value,
                                    const std::uint32_t factor) {
    const auto scaled = static_cast<std::int64_t>(value) * factor;
    if (scaled < std::numeric_limits<std::int32_t>::min() ||
        scaled > std::numeric_limits<std::int32_t>::max()) {
        throw std::overflow_error("supersampled viewport component overflows");
    }
    return static_cast<std::int32_t>(scaled);
}

}  // namespace

class AndroidBoundaryHle::Impl final {
public:
    Impl(memory::AddressSpace& address_space, const gles::AngleBackend backend,
         const std::uint32_t width, const std::uint32_t height,
         const std::uint32_t supersample_factor)
        : address_space_(address_space), backend_(backend),
          layout_(gles::MakeSupersampleLayout(width, height, supersample_factor)),
          symbols_(BuildSymbols()), provider_(symbols_) {
    }

    void MapThunks() {
        if (mapped_) throw std::logic_error("Android boundary thunks are already mapped");
        const auto page_size = address_space_.PageSize();
        address_space_.Map({memory::GuestAddress{kBionicHleThunkBegin}, page_size},
                           memory::PageProtection::read | memory::PageProtection::write);
        std::vector<std::byte> code(page_size, std::byte{});
        for (std::size_t index = 0; index < symbols_.size(); ++index) {
            const auto offset = index * kThunkStride;
            code[offset] = std::byte{0x02};
            code[offset + 1] = std::byte{0xdf};  // Thumb svc #2
            code[offset + 2] = std::byte{0x70};
            code[offset + 3] = std::byte{0x47};  // bx lr
        }
        address_space_.Write(memory::GuestAddress{kBionicHleThunkBegin}, code);
        address_space_.Protect({memory::GuestAddress{kBionicHleThunkBegin}, page_size},
                               memory::PageProtection::read |
                                   memory::PageProtection::execute);
        mapped_ = true;
    }

    [[nodiscard]] bool Handle(cpu::Cpu& cpu, const cpu::RunResult& stopped) {
        if (!mapped_ || stopped.reason != cpu::RunStopReason::supervisor_call ||
            stopped.immediate != 2) return false;
        const auto resolved = provider_.Resolve(stopped.pc.Value());
        if (!resolved.has_value()) return false;
        auto state = cpu.GetState();
        const auto arguments = std::array{
            state.Register(cpu::CoreRegister::r0), state.Register(cpu::CoreRegister::r1),
            state.Register(cpu::CoreRegister::r2), state.Register(cpu::CoreRegister::r3)};
        std::uint32_t result{};
        const auto& symbol = resolved->symbol;
        if (resolved->module == "libc.so") {
            result = ExecuteBionicMemoryIntercept(
                address_space_, {symbol, arguments, state.ThreadId()});
        } else {
            result = Dispatch(symbol, arguments, state);
        }
        RecordGpuCall(symbol, arguments);
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
        std::scoped_lock lock(mutex_);
        std::vector<core::GpuTraceEntry> result;
        result.reserve(std::min(limit, gpu_trace_.size()));
        for (auto entry = gpu_trace_.rbegin();
             entry != gpu_trace_.rend() && result.size() < limit; ++entry) {
            if (filter.empty() || entry->call.find(filter) != std::string::npos) {
                result.push_back(*entry);
            }
        }
        std::reverse(result.begin(), result.end());
        return result;
    }

private:
    void Write32(const std::uint32_t address, const std::uint32_t value,
                 const std::uint64_t thread_id) {
        if (address == 0) return;
        std::array<std::byte, 4> bytes{};
        for (std::size_t index = 0; index < bytes.size(); ++index) {
            bytes[index] = static_cast<std::byte>(value >> (index * 8U));
        }
        address_space_.Write(memory::GuestAddress{address}, bytes, thread_id);
    }

    std::uint32_t StackWord(const cpu::A32State& state, const std::uint32_t offset) {
        std::array<std::byte, 4> bytes{};
        address_space_.Read(memory::GuestAddress{
                                state.Register(cpu::CoreRegister::sp) + offset},
                            bytes, state.ThreadId());
        std::uint32_t value{};
        for (std::size_t index = 0; index < bytes.size(); ++index) {
            value |= static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[index]))
                     << (index * 8U);
        }
        return value;
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

    std::uint32_t Dispatch(const std::string_view symbol,
                           const std::array<std::uint32_t, 4>& args,
                           const cpu::A32State& state) {
        const auto tid = state.ThreadId();
        if (symbol == "AConfiguration_new") return kFakeConfiguration;
        if (symbol == "AConfiguration_delete" ||
            symbol == "AConfiguration_fromAssetManager") return 0;
        if (symbol == "AConfiguration_getLanguage" ||
            symbol == "AConfiguration_getCountry") {
            if (args[1] != 0) {
                const std::array bytes{std::byte{'e'}, std::byte{'n'}};
                address_space_.Write(memory::GuestAddress{args[1]}, bytes, tid);
            }
            return 0;
        }
        if (symbol == "ALooper_prepare") return kFakeLooper;
        if (symbol == "ALooper_addFd") {
            std::scoped_lock lock(mutex_);
            command_ident_ = args[2];
            command_data_ = StackWord(state, 4);
            return 1;
        }
        if (symbol == "ALooper_pollAll") return PollAll(args, tid);
        if (symbol == "AInputQueue_attachLooper") {
            std::scoped_lock lock(mutex_);
            input_ident_ = args[2];
            input_data_ = StackWord(state, 0);
            return 0;
        }
        if (symbol == "AInputQueue_detachLooper") return 0;
        if (symbol == "AInputQueue_getEvent") {
            std::scoped_lock lock(mutex_);
            if (inputs_.empty()) return SignedResult(-1);
            active_input_ = inputs_.front();
            inputs_.pop_front();
            Write32(args[1], kFakeInputEvent, tid);
            return 0;
        }
        if (symbol == "AInputQueue_preDispatchEvent") return 0;
        if (symbol == "AInputQueue_finishEvent") {
            std::scoped_lock lock(mutex_);
            active_input_.reset();
            return 0;
        }
        if (symbol == "AInputEvent_getType") {
            std::scoped_lock lock(mutex_);
            return active_input_.has_value() && active_input_->type == AndroidBoundaryInputType::key
                       ? 1U : 2U;
        }
        if (symbol == "AKeyEvent_getAction") {
            std::scoped_lock lock(mutex_);
            return active_input_.has_value() && active_input_->pressed ? 0U : 1U;
        }
        if (symbol == "AKeyEvent_getKeyCode") {
            std::scoped_lock lock(mutex_);
            return active_input_.has_value() ? static_cast<std::uint32_t>(active_input_->code) : 0U;
        }
        if (symbol == "AMotionEvent_getAction") {
            std::scoped_lock lock(mutex_);
            if (!active_input_.has_value() ||
                active_input_->type == AndroidBoundaryInputType::pointer_motion) return 2U;
            return active_input_->pressed ? 0U : 1U;
        }
        if (symbol == "AMotionEvent_getX" || symbol == "AMotionEvent_getY") {
            std::scoped_lock lock(mutex_);
            const auto value = !active_input_.has_value() ? 0.0F
                : symbol == "AMotionEvent_getX" ? active_input_->x : active_input_->y;
            return std::bit_cast<std::uint32_t>(value);
        }
        if (symbol == "ANativeWindow_setBuffersGeometry") return 0;
        if (symbol == "eglGetDisplay") return kFakeDisplay;
        if (symbol == "eglInitialize") {
            Write32(args[1], 1, tid); Write32(args[2], 5, tid); return 1;
        }
        if (symbol == "eglChooseConfig") {
            Write32(args[2], kFakeConfig, tid);
            Write32(StackWord(state, 0), 1, tid);
            return 1;
        }
        if (symbol == "eglGetConfigAttrib") { Write32(args[3], 0, tid); return 1; }
        if (symbol == "eglCreateWindowSurface") return kFakeSurface;
        if (symbol == "eglCreateContext") return kFakeContext;
        if (symbol == "eglMakeCurrent") {
            if (args[3] != 0 && !angle_frame_.has_value()) {
                angle_frame_.emplace(gles::AngleFrame::CreatePbuffer(
                    backend_, layout_.render_width, layout_.render_height));
                std::scoped_lock lock(mutex_);
                gpu_render_target_ready_ = true;
            }
            return 1;
        }
        if (symbol == "eglQuerySurface") {
            Write32(args[3], args[2] == kEglWidth ? layout_.logical_width :
                             args[2] == kEglHeight ? layout_.logical_height : 0, tid);
            return 1;
        }
        if (symbol == "eglSwapBuffers") {
            if (!angle_frame_.has_value()) throw std::runtime_error("eglSwapBuffers has no current ANGLE frame");
            AndroidBoundaryFrame frame{
                layout_.logical_width, layout_.logical_height, ++frame_sequence_,
                gles::ResolveSupersampledRgba8(angle_frame_->ReadRgba8(), layout_)};
            { std::scoped_lock lock(mutex_); latest_frame_ = std::move(frame); }
            ready_.notify_all();
            return 1;
        }
        if (symbol == "eglDestroyContext" || symbol == "eglDestroySurface") return 1;
        if (symbol == "eglTerminate") {
            angle_frame_.reset();
            std::scoped_lock lock(mutex_);
            gpu_render_target_ready_ = false;
            return 1;
        }
        if (symbol == "glViewport") {
            RequireFrame(symbol).Viewport(
                ScaleViewportComponent(std::bit_cast<std::int32_t>(args[0]),
                                       layout_.factor),
                ScaleViewportComponent(std::bit_cast<std::int32_t>(args[1]),
                                       layout_.factor),
                ScaleViewportComponent(std::bit_cast<std::int32_t>(args[2]),
                                       layout_.factor),
                ScaleViewportComponent(std::bit_cast<std::int32_t>(args[3]),
                                       layout_.factor));
            return 0;
        }
        if (symbol == "glClearColor") {
            RequireFrame(symbol).ClearColor(std::bit_cast<float>(args[0]),
                                            std::bit_cast<float>(args[1]),
                                            std::bit_cast<float>(args[2]),
                                            std::bit_cast<float>(args[3]));
            return 0;
        }
        if (symbol == "glClear") {
            RequireFrame(symbol).Clear(args[0]);
            std::scoped_lock lock(mutex_);
            ++gpu_stats_.clears;
            return 0;
        }
        if (symbol == "__android_log_print" || symbol == "__android_log_write") return 0;
        throw std::runtime_error("Android boundary HLE is not implemented: " + std::string(symbol));
    }

    gles::AngleFrame& RequireFrame(const std::string_view operation) {
        if (!angle_frame_.has_value()) {
            throw std::runtime_error(std::string(operation) + " has no current ANGLE frame");
        }
        return *angle_frame_;
    }

    void RecordGpuCall(const std::string_view symbol,
                       const std::array<std::uint32_t, 4>& args) {
        if (!symbol.starts_with("egl") && !symbol.starts_with("gl")) return;
        core::GpuTraceEntry entry;
        entry.call = symbol;
        for (std::size_t index = 0; index < args.size(); ++index) {
            entry.arguments.emplace("r" + std::to_string(index),
                                    std::to_string(args[index]));
        }
        std::scoped_lock lock(mutex_);
        gpu_trace_.push_back(std::move(entry));
        constexpr std::size_t kMaximumGpuTraceEntries = 2048;
        if (gpu_trace_.size() > kMaximumGpuTraceEntries) gpu_trace_.pop_front();
    }

    memory::AddressSpace& address_space_;
    gles::AngleBackend backend_;
    gles::SupersampleLayout layout_;
    std::vector<BionicHleSymbol> symbols_;
    BionicHleSymbolProvider provider_;
    bool mapped_{};
    std::optional<gles::AngleFrame> angle_frame_;
    std::uint64_t frame_sequence_{};
    mutable std::mutex mutex_;
    std::condition_variable ready_;
    std::uint64_t pending_command_writes_{};
    std::uint32_t command_ident_{};
    std::uint32_t command_data_{};
    std::uint32_t input_ident_{};
    std::uint32_t input_data_{};
    std::deque<AndroidBoundaryInput> inputs_;
    std::optional<AndroidBoundaryInput> active_input_;
    std::optional<AndroidBoundaryFrame> latest_frame_;
    core::GpuStats gpu_stats_{0, 0, 0, 0, 0, {{0, 0, "color0"}}};
    std::deque<core::GpuTraceEntry> gpu_trace_;
    bool gpu_render_target_ready_{};
};

AndroidBoundaryHle::AndroidBoundaryHle(memory::AddressSpace& address_space,
                                       const gles::AngleBackend backend,
                                       const std::uint32_t width,
                                       const std::uint32_t height,
                                       const std::uint32_t supersample_factor)
    : impl_(std::make_unique<Impl>(address_space, backend, width, height,
                                   supersample_factor)) {}
AndroidBoundaryHle::~AndroidBoundaryHle() = default;
void AndroidBoundaryHle::MapThunks() { impl_->MapThunks(); }
const BionicHleSymbolProvider& AndroidBoundaryHle::Symbols() const noexcept {
    return impl_->Symbols();
}
bool AndroidBoundaryHle::Handle(cpu::Cpu& cpu, const cpu::RunResult& stopped) {
    return impl_->Handle(cpu, stopped);
}
void AndroidBoundaryHle::NotifyFileWrite() { impl_->NotifyFileWrite(); }
void AndroidBoundaryHle::PushInput(const AndroidBoundaryInput& input) { impl_->PushInput(input); }
std::optional<AndroidBoundaryFrame> AndroidBoundaryHle::TakeLatestFrame() {
    return impl_->TakeLatestFrame();
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
