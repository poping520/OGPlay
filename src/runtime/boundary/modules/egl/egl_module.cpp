#include "runtime/boundary/modules/egl/egl_module.h"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "ogplay/gles/angle_frame.h"
#include "ogplay/memory/address_space.h"

namespace ogplay::runtime {
namespace {

constexpr std::uint32_t kFakeDisplay = 1U;
constexpr std::uint32_t kFakeConfig = 2U;
constexpr std::uint32_t kFakeSurface = 3U;
constexpr std::uint32_t kFakeContext = 4U;
constexpr std::uint32_t kEglSuccess = 0x3000U;
constexpr std::uint32_t kEglBadAttribute = 0x3004U;
constexpr std::uint32_t kEglBadConfig = 0x3005U;
constexpr std::uint32_t kEglBadContext = 0x3006U;
constexpr std::uint32_t kEglBadDisplay = 0x3008U;
constexpr std::uint32_t kEglBadParameter = 0x300CU;
constexpr std::uint32_t kEglWidth = 0x3057U;
constexpr std::uint32_t kEglHeight = 0x3056U;
constexpr std::uint32_t kEglDraw = 0x3059U;
constexpr std::uint32_t kEglRead = 0x305AU;
constexpr std::uint32_t kEglNone = 0x3038U;
constexpr std::uint32_t kEglVendor = 0x3053U;
constexpr std::uint32_t kEglVersion = 0x3054U;
constexpr std::uint32_t kEglExtensions = 0x3055U;
constexpr std::uint32_t kEglClientApis = 0x308DU;
constexpr std::uint32_t kEglConfigId = 0x3028U;
constexpr std::uint32_t kEglContextClientType = 0x3097U;
constexpr std::uint32_t kEglContextClientVersion = 0x3098U;
constexpr std::uint32_t kEglOpenGlEsApi = 0x30A0U;
constexpr memory::GuestAddress kEglStringPage{0x71C00000U};
constexpr std::size_t kMaximumProcNameBytes = 1024U;
constexpr std::size_t kMaximumAttributeWords = 64U;

struct PublishedString final {
    std::uint32_t name;
    std::uint32_t offset;
    std::string_view value;
};

constexpr std::array kPublishedStrings{
    PublishedString{kEglVendor, 0U, "OGPlay"},
    PublishedString{kEglVersion, 256U, "1.4 OGPlay"},
    PublishedString{kEglExtensions, 512U, ""},
    PublishedString{kEglClientApis, 768U, "OpenGL_ES"},
};

[[nodiscard]] const PublishedString* FindPublishedString(
    const std::uint32_t name) noexcept {
    for (const auto& value : kPublishedStrings) {
        if (value.name == name) return &value;
    }
    return nullptr;
}

}  // namespace

EglModule::EglModule(BoundaryCallServices& calls,
                     EglBoundaryContext& context) noexcept
    : calls_(calls), context_(context) {}

BoundaryCallServices& EglModule::CallServices() noexcept { return calls_; }

void EglModule::SetError(const std::uint64_t thread_id,
                         const std::uint32_t error) {
    std::scoped_lock lock(mutex_);
    threads_[thread_id].error = error;
}

std::uint32_t EglModule::TakeError(const std::uint64_t thread_id) {
    std::scoped_lock lock(mutex_);
    auto& state = threads_[thread_id];
    const auto error = state.error;
    state.error = kEglSuccess;
    return error;
}

std::uint32_t EglModule::PublishQueryString(const std::uint32_t name,
                                            const std::uint64_t thread_id) {
    const auto* published = FindPublishedString(name);
    if (published == nullptr) {
        SetError(thread_id, kEglBadParameter);
        return 0U;
    }
    std::scoped_lock lock(mutex_);
    if (!strings_mapped_) {
        const auto page = memory::GuestRange{kEglStringPage,
                                              calls_.address_space.PageSize()};
        calls_.address_space.Map(page, memory::PageProtection::read |
                                          memory::PageProtection::write);
        for (const auto& entry : kPublishedStrings) {
            std::vector<std::byte> bytes(entry.value.size() + 1U);
            for (std::size_t index = 0; index < entry.value.size(); ++index) {
                bytes[index] = static_cast<std::byte>(
                    static_cast<unsigned char>(entry.value[index]));
            }
            calls_.address_space.Write(kEglStringPage.Add(entry.offset), bytes,
                                       thread_id);
        }
        calls_.address_space.Protect(page, memory::PageProtection::read);
        strings_mapped_ = true;
    }
    return kEglStringPage.Add(published->offset).Value();
}

std::uint32_t EglModule::ResolveProcAddress(
    const GuestCString name, const std::uint64_t thread_id) const {
    if (name.IsNull()) return 0U;
    const auto length = calls_.address_space.CStringLength(
        name.Address(), kMaximumProcNameBytes, thread_id);
    std::vector<std::byte> bytes(length);
    calls_.address_space.Read(name.Address(), bytes, thread_id);
    std::string requested(length, '\0');
    for (std::size_t index = 0; index < length; ++index) {
        requested[index] = static_cast<char>(
            std::to_integer<unsigned char>(bytes[index]));
    }
    constexpr std::array preferred_libraries{
        std::string_view{"libGLESv2.so"},
        std::string_view{"libGLESv1_CM.so"},
        std::string_view{"libEGL.so"},
    };
    for (const auto library : preferred_libraries) {
        for (const auto& symbol : context_.symbols) {
            if (symbol.kind == BoundarySymbolKind::function &&
                symbol.library == library && symbol.symbol == requested) {
                return symbol.address.Value();
            }
        }
    }
    return 0U;
}

#if defined(_MSC_VER)
#pragma warning(push)
// VS 18.8 reports the discarded fallback of exhaustive if-constexpr
// instantiations as unreachable.
#pragma warning(disable : 4702)
#endif
template <std::uint16_t FunctionId>
std::uint32_t EglModule::ExecuteExport(const A32CallFrame& call) {
    const auto args = call.RegisterArguments();
    const auto tid = call.ThreadId();
    auto& graphics = context_.graphics;
    if constexpr (FunctionId == 0U) return kFakeDisplay;
    if constexpr (FunctionId == 1U) {
        if (args[0] != kFakeDisplay) {
            SetError(tid, kEglBadDisplay);
            return 0U;
        }
        if (args[1] != 0U) graphics.Write32(args[1], 1U, tid);
        if (args[2] != 0U) graphics.Write32(args[2], 4U, tid);
        std::scoped_lock lock(mutex_);
        initialized_ = true;
        return 1U;
    }
    if constexpr (FunctionId == 2U) {
        if (args[0] != kFakeDisplay) {
            SetError(tid, kEglBadDisplay);
            return 0U;
        }
        if (args[2] != 0U && args[3] > 0U) {
            graphics.Write32(args[2], kFakeConfig, tid);
        }
        if (call.Argument(4) != 0U) graphics.Write32(call.Argument(4), 1U, tid);
        return 1U;
    }
    if constexpr (FunctionId == 3U) {
        if (args[0] != kFakeDisplay) {
            SetError(tid, kEglBadDisplay);
            return 0U;
        }
        if (args[1] != kFakeConfig) {
            SetError(tid, kEglBadConfig);
            return 0U;
        }
        graphics.Write32(args[3], args[2] == kEglConfigId ? 1U : 0U, tid);
        return 1U;
    }
    if constexpr (FunctionId == 4U) {
        std::scoped_lock lock(mutex_);
        pbuffer_width_ = 0U;
        pbuffer_height_ = 0U;
        return kFakeSurface;
    }
    if constexpr (FunctionId == 5U) return kFakeContext;
    if constexpr (FunctionId == 6U) {
        if (graphics.managed_surface) {
            throw std::runtime_error(
                "guest EGL cannot replace a host-managed ANGLE surface");
        }
        if (args[3] != 0U && !graphics.angle_frame.has_value()) {
            graphics.angle_frame.emplace(gles::AngleFrame::CreatePbuffer(
                graphics.backend, graphics.layout.render_width,
                graphics.layout.render_height));
            graphics.gl_owner = std::this_thread::get_id();
            graphics.InitializeGuestGlDefaults();
            graphics.frames.SetRenderTargetReady(true);
        } else if (args[3] == 0U && graphics.gl_owner.has_value()) {
            graphics.ReleaseManagedSurfaceFromCallingThread();
        }
        std::scoped_lock lock(mutex_);
        auto& state = threads_[tid];
        state.display = args[3] == 0U ? 0U : args[0];
        state.draw_surface = args[3] == 0U ? 0U : args[1];
        state.read_surface = args[3] == 0U ? 0U : args[2];
        state.context = args[3];
        return 1U;
    }
    if constexpr (FunctionId == 7U) {
        std::uint32_t width{};
        std::uint32_t height{};
        {
            std::scoped_lock lock(mutex_);
            width = pbuffer_width_ == 0U ? graphics.layout.logical_width
                                         : pbuffer_width_;
            height = pbuffer_height_ == 0U ? graphics.layout.logical_height
                                           : pbuffer_height_;
        }
        graphics.Write32(
            args[3], args[2] == kEglWidth ? width
                         : args[2] == kEglHeight ? height : 0U,
            tid);
        return 1U;
    }
    if constexpr (FunctionId == 8U) {
        if (!graphics.angle_frame.has_value()) {
            throw std::runtime_error("eglSwapBuffers has no current ANGLE frame");
        }
        graphics.PublishFrame();
        return 1U;
    }
    if constexpr (FunctionId == 9U || FunctionId == 10U) return 1U;
    if constexpr (FunctionId == 11U) {
        if (graphics.managed_surface) {
            throw std::runtime_error(
                "guest EGL cannot terminate a host-managed ANGLE surface");
        }
        graphics.gl_owner.reset();
        graphics.angle_frame.reset();
        graphics.ResetGuestGraphics();
        graphics.frames.SetRenderTargetReady(false);
        std::scoped_lock lock(mutex_);
        initialized_ = false;
        threads_.clear();
        return 1U;
    }
    if constexpr (FunctionId == 12U) return TakeError(tid);
    if constexpr (FunctionId == 13U) {
        if (args[0] != kFakeDisplay) {
            SetError(tid, kEglBadDisplay);
            return 0U;
        }
        return PublishQueryString(args[1], tid);
    }
    if constexpr (FunctionId == 14U) {
        return ResolveProcAddress(call.CString(0), tid);
    }
    if constexpr (FunctionId == 15U) {
        if (args[0] != kFakeDisplay) {
            SetError(tid, kEglBadDisplay);
            return 0U;
        }
        const auto size = std::bit_cast<std::int32_t>(args[2]);
        if (size < 0) {
            SetError(tid, kEglBadParameter);
            return 0U;
        }
        if (args[1] != 0U && size > 0) {
            graphics.Write32(args[1], kFakeConfig, tid);
        }
        if (args[3] != 0U) graphics.Write32(args[3], 1U, tid);
        return 1U;
    }
    if constexpr (FunctionId == 16U) {
        std::scoped_lock lock(mutex_);
        return threads_[tid].context;
    }
    if constexpr (FunctionId == 17U) {
        std::scoped_lock lock(mutex_);
        if (args[0] == kEglDraw) return threads_[tid].draw_surface;
        if (args[0] == kEglRead) return threads_[tid].read_surface;
        threads_[tid].error = kEglBadParameter;
        return 0U;
    }
    if constexpr (FunctionId == 18U) {
        std::scoped_lock lock(mutex_);
        return threads_[tid].display;
    }
    if constexpr (FunctionId == 19U) {
        if (args[0] != kFakeDisplay) {
            SetError(tid, kEglBadDisplay);
            return 0U;
        }
        if (args[1] != kFakeContext) {
            SetError(tid, kEglBadContext);
            return 0U;
        }
        std::uint32_t value{};
        if (args[2] == kEglConfigId) value = 1U;
        else if (args[2] == kEglContextClientType) value = kEglOpenGlEsApi;
        else if (args[2] == kEglContextClientVersion) value = 2U;
        else {
            SetError(tid, kEglBadAttribute);
            return 0U;
        }
        graphics.Write32(args[3], value, tid);
        return 1U;
    }
    if constexpr (FunctionId == 20U) {
        if (args[0] != kEglOpenGlEsApi) {
            SetError(tid, kEglBadParameter);
            return 0U;
        }
        std::scoped_lock lock(mutex_);
        threads_[tid].bound_api = args[0];
        return 1U;
    }
    if constexpr (FunctionId == 21U) {
        std::scoped_lock lock(mutex_);
        return threads_[tid].bound_api;
    }
    if constexpr (FunctionId == 22U) {
        bool release_native{};
        {
            std::scoped_lock lock(mutex_);
            const auto found = threads_.find(tid);
            release_native = found != threads_.end() &&
                             found->second.context != 0U &&
                             graphics.gl_owner.has_value();
            threads_.erase(tid);
        }
        if (release_native) graphics.ReleaseManagedSurfaceFromCallingThread();
        return 1U;
    }
    if constexpr (FunctionId == 23U) {
        if (args[0] != kFakeDisplay) {
            SetError(tid, kEglBadDisplay);
            return 0U;
        }
        const auto interval = std::bit_cast<std::int32_t>(args[1]);
        if (interval < 0) {
            SetError(tid, kEglBadParameter);
            return 0U;
        }
        std::scoped_lock lock(mutex_);
        swap_interval_ = interval;
        return 1U;
    }
    if constexpr (FunctionId == 24U) {
        if (args[0] != kFakeDisplay) {
            SetError(tid, kEglBadDisplay);
            return 0U;
        }
        if (args[1] != kFakeConfig) {
            SetError(tid, kEglBadConfig);
            return 0U;
        }
        std::uint32_t width{};
        std::uint32_t height{};
        if (args[2] != 0U) {
            auto cursor = memory::GuestAddress{args[2]};
            bool terminated{};
            for (std::size_t word = 0U; word < kMaximumAttributeWords;
                 word += 2U) {
                const auto attribute = calls_.address_space.Read32(cursor, tid);
                if (attribute == kEglNone) {
                    terminated = true;
                    break;
                }
                const auto value = calls_.address_space.Read32(cursor.Add(4U), tid);
                if (attribute == kEglWidth) width = value;
                else if (attribute == kEglHeight) height = value;
                else {
                    SetError(tid, kEglBadAttribute);
                    return 0U;
                }
                cursor = cursor.Add(8U);
            }
            if (!terminated) {
                SetError(tid, kEglBadAttribute);
                return 0U;
            }
        }
        if (width == 0U || height == 0U ||
            width > static_cast<std::uint32_t>(
                        (std::numeric_limits<std::int32_t>::max)()) ||
            height > static_cast<std::uint32_t>(
                         (std::numeric_limits<std::int32_t>::max)())) {
            SetError(tid, kEglBadParameter);
            return 0U;
        }
        std::scoped_lock lock(mutex_);
        pbuffer_width_ = width;
        pbuffer_height_ = height;
        return kFakeSurface;
    }
    throw std::logic_error("unbound concrete libEGL export");
}

#define OGPLAY_DEFINE_EGL(name, id, count, method)                             \
    std::uint32_t EglModule::method(const A32CallFrame& call) {                \
        return ExecuteExport<id>(call);                                        \
    }
OGPLAY_EGL_BOUNDARY_EXPORTS(OGPLAY_DEFINE_EGL)
#undef OGPLAY_DEFINE_EGL
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

}  // namespace ogplay::runtime
