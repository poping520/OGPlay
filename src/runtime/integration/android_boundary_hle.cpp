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
#include "ogplay/gles/gles_dispatch.h"
#include "ogplay/gles/guest_transfer.h"
#include "ogplay/gles/supersample.h"
#include "ogplay/runtime/integration/android_boundary_gles.h"
#include "ogplay/runtime/integration/guest_gl_context.h"
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
        gles1_state_.Fixed().SetMaterialSingleFaceQuirk(
            options.allow_gles1_material_single_face);
    }
    void MapThunks() {
        if (mapped_) throw std::logic_error("Android boundary thunks are already mapped");
        const auto page_size = address_space_.PageSize();
        if (symbols_.size() > page_size / kThunkStride) {
            throw std::length_error("Android boundary thunk catalog exceeds its guest page");
        }
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
    void OpenManagedSurface() {
        if (angle_frame_.has_value()) {
            throw std::logic_error(
                "Android boundary already has a current ANGLE frame");
        }
        angle_frame_.emplace(gles::AngleFrame::CreatePbuffer(
            backend_, layout_.render_width, layout_.render_height));
        managed_surface_ = true;
        std::scoped_lock lock(mutex_);
        gpu_render_target_ready_ = true;
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
        if (!mapped_ || stopped.reason != cpu::RunStopReason::supervisor_call ||
            stopped.immediate != 2) return false;
        const auto* descriptor = detail::DecodeAndroidBoundaryThunk(
            stopped.pc.Value(), descriptors_);
        if (descriptor == nullptr) return false;
        auto state = cpu.GetState();
        const auto arguments = std::array{
            state.Register(cpu::CoreRegister::r0), state.Register(cpu::CoreRegister::r1),
            state.Register(cpu::CoreRegister::r2), state.Register(cpu::CoreRegister::r3)};
        std::uint32_t result{};
        const auto symbol = descriptor->name;
        try {
            if (descriptor->route == detail::HleRoute::bionic_memory) {
                result = ExecuteBionicMemoryIntercept(
                    address_space_, {symbol, arguments, state.ThreadId()});
            } else {
                result = Dispatch(descriptor->library, symbol, arguments, state);
            }
        } catch (const gles::GuestTransferError& error) {
            throw gles::GuestTransferError(
                "Android boundary guest transfer failed in " +
                std::string(descriptor->library) + "!" + std::string(symbol) +
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
        std::string result;
        result.reserve(std::min<std::size_t>(maximum_bytes, 256));
        for (std::size_t offset = 0; offset < maximum_bytes; ++offset) {
            std::array<std::byte, 1> bytes{};
            address_space_.Read(memory::GuestAddress{address}.Add(offset),
                                bytes, thread_id);
            const auto value = static_cast<char>(
                std::to_integer<unsigned char>(bytes[0]));
            if (value == '\0') return result;
            result.push_back(value);
        }
        throw std::length_error(std::string(operation) +
                                " guest string exceeds its byte limit");
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
    std::uint32_t Dispatch(const std::string_view module,
                           const std::string_view symbol,
                           const std::array<std::uint32_t, 4>& args,
                           const cpu::A32State& state) {
        const auto tid = state.ThreadId();
        if (module == "libGLESv1_CM.so") {
            const auto draw_call = symbol == "glDrawArrays" ||
                                   symbol == "glDrawElements";
            if (draw_call &&
                gl_context_.SelectDrawRenderer(
                    gles1_draw_state_
                        .Array(detail::kGles1VertexArray, 0x84C0U)
                        .enabled,
                    gles_dispatch_.HasEnabledVertexAttribute()) ==
                    GuestGlRenderer::programmable) {
                const auto result = gles_dispatch_.Dispatch(
                    symbol, args, state,
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
            auto api = gles::GlesApi::gles1;
            auto id = gles::FindGlesFunction(api, symbol);
            if (!id.has_value()) {
                api = gles::GlesApi::gles1_extensions;
                id = gles::FindGlesFunction(api, symbol);
            }
            if (!id.has_value()) {
                throw std::logic_error("GLES1 boundary symbol is outside its catalog");
            }
            const auto info = gles::DescribeGlesFunction(api, *id);
            std::vector<std::uint32_t> all;
            all.reserve(info.parameter_count);
            for (std::size_t index = 0; index < info.parameter_count; ++index) {
                all.push_back(index < args.size()
                                  ? args[index]
                                  : StackWord(state, static_cast<std::uint32_t>(
                                                         (index - args.size()) * 4U)));
            }
            auto& dispatch = api == gles::GlesApi::gles1
                                 ? gles1_dispatch_
                                 : gles1_extensions_dispatch_;
            const auto fixed_draw = api == gles::GlesApi::gles1 &&
                                    (symbol == "glDrawArrays" ||
                                     symbol == "glDrawElements");
            if (!fixed_draw) return dispatch.Invoke(*id, all, tid);
            gl_context_.Native().BeginFixedDraw();
            try {
                const auto result = dispatch.Invoke(*id, all, tid);
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
            if (managed_surface_) {
                throw std::runtime_error(
                    "guest EGL cannot replace a host-managed ANGLE surface");
            }
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
            PublishFrame();
            return 1;
        }
        if (symbol == "eglDestroyContext" || symbol == "eglDestroySurface") return 1;
        if (symbol == "eglTerminate") {
            if (managed_surface_) {
                throw std::runtime_error(
                    "guest EGL cannot terminate a host-managed ANGLE surface");
            }
            angle_frame_.reset();
            gles_dispatch_.Reset();
            gles1_state_.Reset();
            gles1_legacy_state_.Reset();
            gles1_draw_state_.Reset();
            std::scoped_lock lock(mutex_);
            gpu_render_target_ready_ = false;
            return 1;
        }
        if (const auto shader_program = DispatchShaderProgram(symbol, args, state);
            shader_program.has_value()) {
            return *shader_program;
        }
        if (const auto resources = gles_dispatch_.Dispatch(
                symbol, args, state,
                angle_frame_.has_value() ? &*angle_frame_ : nullptr);
            resources.has_value()) {
            if (symbol == "glDrawElements" || symbol == "glDrawArrays") {
                std::scoped_lock lock(mutex_);
                ++gpu_stats_.draws;
                ++gpu_stats_.draw_targets.front().draws;
            }
            return *resources;
        }
        if (symbol == "glViewport") {
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
        if (symbol == "glScissor") {
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
        if (symbol == "glClearColor") {
            const std::array color{std::bit_cast<float>(args[0]),
                                   std::bit_cast<float>(args[1]),
                                   std::bit_cast<float>(args[2]),
                                   std::bit_cast<float>(args[3])};
            RequireFrame(symbol).ClearColor(color[0], color[1], color[2], color[3]);
            gl_context_.Shared().SetClearColor(color);
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
    std::optional<std::uint32_t> DispatchShaderProgram(
        const std::string_view symbol,
        const std::array<std::uint32_t, 4>& args,
        const cpu::A32State& state) {
        const auto tid = state.ThreadId();
        if (symbol == "glCreateShader") return RequireFrame(symbol).CreateShader(args[0]);
        if (symbol == "glShaderSource") {
            RequireFrame(symbol).ShaderSource(args[0], ReadShaderSources(args, tid)); return 0;
        }
        if (symbol == "glCompileShader") {
            RequireFrame(symbol).CompileShader(args[0]); std::scoped_lock lock(mutex_);
            ++gpu_stats_.shader_compiles; return 0;
        }
        if (symbol == "glGetShaderiv") {
            const auto value = RequireFrame(symbol).GetShaderParameter(args[0], args[1]);
            WriteRequired32(args[2], std::bit_cast<std::uint32_t>(value), tid, symbol); return 0;
        }
        if (symbol == "glDeleteShader") { RequireFrame(symbol).DeleteShader(args[0]); return 0; }
        if (symbol == "glCreateProgram") return RequireFrame(symbol).CreateProgram();
        if (symbol == "glAttachShader") { RequireFrame(symbol).AttachShader(args[0], args[1]); return 0; }
        if (symbol == "glLinkProgram") {
            RequireFrame(symbol).LinkProgram(args[0]); std::scoped_lock lock(mutex_);
            ++gpu_stats_.program_links; return 0;
        }
        if (symbol == "glGetProgramiv") {
            const auto value = RequireFrame(symbol).GetProgramParameter(args[0], args[1]);
            WriteRequired32(args[2], std::bit_cast<std::uint32_t>(value), tid, symbol); return 0;
        }
        if (symbol == "glGetAttribLocation") {
            return SignedResult(RequireFrame(symbol).GetAttribLocation(
                args[0], ReadCString(args[1], kMaximumGlesNameBytes, tid, symbol)));
        }
        if (symbol == "glGetUniformLocation") {
            return SignedResult(RequireFrame(symbol).GetUniformLocation(
                args[0], ReadCString(args[1], kMaximumGlesNameBytes, tid, symbol)));
        }
        if (symbol == "glUseProgram") {
            RequireFrame(symbol).UseProgram(args[0]);
            gl_context_.Shared().SetCurrentProgram(args[0]);
            return 0;
        }
        if (symbol == "glDeleteProgram") { RequireFrame(symbol).DeleteProgram(args[0]); return 0; }
        return std::nullopt;
    }
    gles::AngleFrame& RequireFrame(const std::string_view operation) {
        if (!angle_frame_.has_value()) {
            throw std::runtime_error(std::string(operation) + " has no current ANGLE frame");
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
    std::optional<gles::AngleFrame> angle_frame_;
    bool managed_surface_{};
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
    std::vector<std::uint8_t> recycled_rgba8_;
    core::GpuStats gpu_stats_{0, 0, 0, 0, 0, {{0, 0, "color0"}}};
    std::deque<core::GpuTraceEntry> gpu_trace_;
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
void AndroidBoundaryHle::PresentManagedSurface() {
    impl_->PresentManagedSurface();
}
void AndroidBoundaryHle::CloseManagedSurface() {
    impl_->CloseManagedSurface();
}
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
