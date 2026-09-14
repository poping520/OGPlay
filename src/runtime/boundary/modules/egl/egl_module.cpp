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
constexpr std::uint32_t kEglNotInitialized = 0x3001U;
constexpr std::uint32_t kEglBadAccess = 0x3002U;
constexpr std::uint32_t kEglBadAttribute = 0x3004U;
constexpr std::uint32_t kEglBadConfig = 0x3005U;
constexpr std::uint32_t kEglBadContext = 0x3006U;
constexpr std::uint32_t kEglBadCurrentSurface = 0x3007U;
constexpr std::uint32_t kEglBadDisplay = 0x3008U;
constexpr std::uint32_t kEglBadNativePixmap = 0x300AU;
constexpr std::uint32_t kEglBadNativeWindow = 0x300BU;
constexpr std::uint32_t kEglBadParameter = 0x300CU;
constexpr std::uint32_t kEglBadSurface = 0x300DU;
constexpr std::uint32_t kEglBadMatch = 0x3009U;
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
constexpr std::uint32_t kEglBufferSize = 0x3020U;
constexpr std::uint32_t kEglAlphaSize = 0x3021U;
constexpr std::uint32_t kEglBlueSize = 0x3022U;
constexpr std::uint32_t kEglGreenSize = 0x3023U;
constexpr std::uint32_t kEglRedSize = 0x3024U;
constexpr std::uint32_t kEglDepthSize = 0x3025U;
constexpr std::uint32_t kEglStencilSize = 0x3026U;
constexpr std::uint32_t kEglConfigCaveat = 0x3027U;
constexpr std::uint32_t kEglLevel = 0x3029U;
constexpr std::uint32_t kEglMaxPbufferHeight = 0x302AU;
constexpr std::uint32_t kEglMaxPbufferPixels = 0x302BU;
constexpr std::uint32_t kEglMaxPbufferWidth = 0x302CU;
constexpr std::uint32_t kEglNativeRenderable = 0x302DU;
constexpr std::uint32_t kEglNativeVisualId = 0x302EU;
constexpr std::uint32_t kEglNativeVisualType = 0x302FU;
constexpr std::uint32_t kEglSamples = 0x3031U;
constexpr std::uint32_t kEglSampleBuffers = 0x3032U;
constexpr std::uint32_t kEglSurfaceType = 0x3033U;
constexpr std::uint32_t kEglTransparentType = 0x3034U;
constexpr std::uint32_t kEglTransparentBlueValue = 0x3035U;
constexpr std::uint32_t kEglTransparentGreenValue = 0x3036U;
constexpr std::uint32_t kEglTransparentRedValue = 0x3037U;
constexpr std::uint32_t kEglLuminanceSize = 0x303DU;
constexpr std::uint32_t kEglAlphaMaskSize = 0x303EU;
constexpr std::uint32_t kEglColorBufferType = 0x303FU;
constexpr std::uint32_t kEglRenderableType = 0x3040U;
constexpr std::uint32_t kEglConformant = 0x3042U;
constexpr std::uint32_t kEglContextClientType = 0x3097U;
constexpr std::uint32_t kEglContextClientVersion = 0x3098U;
constexpr std::uint32_t kEglMinSwapInterval = 0x303BU;
constexpr std::uint32_t kEglMaxSwapInterval = 0x303CU;
constexpr std::uint32_t kEglLargestPbuffer = 0x3058U;
constexpr std::uint32_t kEglTextureFormat = 0x3080U;
constexpr std::uint32_t kEglTextureTarget = 0x3081U;
constexpr std::uint32_t kEglMipmapTexture = 0x3082U;
constexpr std::uint32_t kEglMipmapLevel = 0x3083U;
constexpr std::uint32_t kEglBackBuffer = 0x3084U;
constexpr std::uint32_t kEglRenderBuffer = 0x3086U;
constexpr std::uint32_t kEglHorizontalResolution = 0x3090U;
constexpr std::uint32_t kEglVerticalResolution = 0x3091U;
constexpr std::uint32_t kEglPixelAspectRatio = 0x3092U;
constexpr std::uint32_t kEglCoreNativeEngine = 0x305BU;
constexpr std::uint32_t kEglOpenVgImage = 0x3096U;
constexpr std::uint32_t kEglSwapBehavior = 0x3093U;
constexpr std::uint32_t kEglBufferDestroyed = 0x3095U;
constexpr std::uint32_t kEglMultisampleResolve = 0x3099U;
constexpr std::uint32_t kEglMultisampleResolveDefault = 0x309AU;
constexpr std::uint32_t kEglOpenGlEsApi = 0x30A0U;
constexpr std::uint32_t kEglDontCare = 0xFFFFFFFFU;
constexpr std::uint32_t kEglWindowBit = 0x0004U;
constexpr std::uint32_t kEglPbufferBit = 0x0001U;
constexpr std::uint32_t kEglOpenGlEsBit = 0x0001U;
constexpr std::uint32_t kEglOpenGlEs2Bit = 0x0004U;
constexpr std::uint32_t kEglOpenGlEs3Bit = 0x0040U;
constexpr std::uint32_t kEglRgbBuffer = 0x308EU;
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

void EglModule::RetireGuestGraphics() noexcept {
    guest_graphics_retired_.store(true, std::memory_order_release);
}

gles::AngleFrame* EglModule::CurrentFrameForHostThread(
    const std::thread::id host_thread, const std::string_view operation) {
    std::scoped_lock lock(mutex_);
    for (auto& [handle, context] : contexts_) {
        static_cast<void>(handle);
        if (context.current_host_thread == host_thread &&
            context.current_draw_surface != 0U) {
            auto surface = context.current_draw_surface;
            if (operation == "glReadPixels") {
                for (const auto& [thread_id, thread] : threads_) {
                    static_cast<void>(thread_id);
                    if (thread.context == handle) {
                        surface = thread.read_surface;
                        break;
                    }
                }
            }
            const auto frame = context.frames.find(surface);
            if (frame == context.frames.end()) return nullptr;
            if (context.current_bound_surface != surface) {
                const auto bound = context.frames.find(
                    context.current_bound_surface);
                if (bound != context.frames.end()) {
                    bound->second->ReleaseCurrent();
                }
                frame->second->BindCurrentOnCallingThread();
                context.current_bound_surface = surface;
            }
            return frame->second.get();
        }
    }
    return nullptr;
}

void EglModule::ActivateStateForHostThread(
    const std::thread::id host_thread) {
    std::scoped_lock lock(mutex_);
    std::uint32_t target{};
    for (const auto& [handle, context] : contexts_) {
        if (context.current_host_thread == host_thread) {
            target = handle;
            break;
        }
    }
    if (target == 0U || target == active_shadow_context_) return;
    if (active_shadow_context_ != 0U) {
        const auto old = contexts_.find(active_shadow_context_);
        if (old != contexts_.end()) {
            old->second.guest_state = context_.graphics.gl_context;
            old->second.gles1_matrices->CopyValuesFrom(
                context_.gles1_state.Matrices());
            old->second.gles1_shade_model =
                context_.gles1_state.ShadeModel();
            old->second.gles1_normalize =
                context_.gles1_state.Capability(0x0BA1U);
            old->second.gles1_rescale_normal =
                context_.gles1_state.Capability(0x803AU);
        }
    }
    context_.graphics.gl_context = contexts_.at(target).guest_state;
    context_.gles1_state.Matrices().CopyValuesFrom(
        *contexts_.at(target).gles1_matrices);
    context_.gles1_state.SetShadeModel(
        contexts_.at(target).gles1_shade_model);
    context_.gles1_state.SetCapability(
        0x0BA1U, contexts_.at(target).gles1_normalize);
    context_.gles1_state.SetCapability(
        0x803AU, contexts_.at(target).gles1_rescale_normal);
    active_shadow_context_ = target;
}

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

void EglModule::CollectRetiredObjectsLocked() {
    std::erase_if(contexts_, [](const auto& entry) {
        return entry.second.destroy_pending &&
               !entry.second.current_thread.has_value();
    });
    std::erase_if(surfaces_, [](const auto& entry) {
        return entry.second.destroy_pending && entry.second.current_count == 0U;
    });
    for (auto& [handle, context] : contexts_) {
        static_cast<void>(handle);
        std::erase_if(context.frames, [&](const auto& frame) {
            return frame.first != context.current_draw_surface &&
                   !surfaces_.contains(frame.first);
        });
    }
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
    for (std::size_t index = 0; index < context_.descriptors.size(); ++index) {
        const auto& descriptor = context_.descriptors[index];
        if (descriptor.library == "$egl.proc" &&
            descriptor.name == requested) {
            return kBionicHleThunkBegin +
                   static_cast<std::uint32_t>(index * 4U) + 1U;
        }
    }
    for (const auto& symbol : context_.symbols) {
        if (symbol.kind == BoundarySymbolKind::function &&
            symbol.library == "libEGL.so" && symbol.symbol == requested) {
            return symbol.address.Value();
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
        context_.api_routing.Activate();
        return 1U;
    }
    if constexpr (FunctionId == 2U) {
        const auto num_config = call.Argument(4);
        if (args[0] != kFakeDisplay) { SetError(tid, kEglBadDisplay); return 0U; }
        if (num_config == 0U) { SetError(tid, kEglBadParameter); return 0U; }
        {
            std::scoped_lock lock(mutex_);
            if (!initialized_) { threads_[tid].error = kEglNotInitialized; return 0U; }
        }
        bool matches = true;
        if (args[1] != 0U) {
            auto cursor = memory::GuestAddress{args[1]};
            bool terminated{};
            for (std::size_t word = 0; word < kMaximumAttributeWords; word += 2U) {
                const auto attribute = calls_.address_space.Read32(cursor, tid);
                if (attribute == kEglNone) { terminated = true; break; }
                const auto value = calls_.address_space.Read32(cursor.Add(4U), tid);
                const auto at_least = [&](const std::uint32_t actual) {
                    return value == kEglDontCare || actual >= value;
                };
                if (attribute == kEglRedSize || attribute == kEglGreenSize ||
                    attribute == kEglBlueSize || attribute == kEglAlphaSize) matches &= at_least(8U);
                else if (attribute == kEglBufferSize) matches &= at_least(32U);
                else if (attribute == kEglDepthSize) matches &= at_least(24U);
                else if (attribute == kEglStencilSize) matches &= at_least(8U);
                else if (attribute == kEglSamples || attribute == kEglSampleBuffers ||
                         attribute == kEglLevel) matches &= at_least(0U);
                else if (attribute == kEglConfigId) matches &= value == kEglDontCare || value == 1U;
                else if (attribute == kEglSurfaceType) matches &= value == kEglDontCare ||
                    (value & ~(kEglWindowBit | kEglPbufferBit)) == 0U;
                else if (attribute == kEglRenderableType || attribute == kEglConformant) matches &=
                    value == kEglDontCare ||
                    (value & ~(kEglOpenGlEsBit | kEglOpenGlEs2Bit |
                               kEglOpenGlEs3Bit)) == 0U;
                else if (attribute == kEglConfigCaveat ||
                         attribute == kEglTransparentType ||
                         attribute == kEglNativeVisualType) {
                    matches &= value == kEglDontCare || value == kEglNone;
                } else if (attribute == kEglColorBufferType) {
                    matches &= value == kEglDontCare || value == kEglRgbBuffer;
                } else if (attribute == kEglNativeRenderable) {
                    matches &= value == kEglDontCare || value == 0U;
                } else if (attribute == kEglNativeVisualId ||
                           attribute == kEglLuminanceSize ||
                           attribute == kEglAlphaMaskSize ||
                           attribute == kEglTransparentRedValue ||
                           attribute == kEglTransparentGreenValue ||
                           attribute == kEglTransparentBlueValue) {
                    matches &= value == kEglDontCare || value == 0U;
                }
                else { SetError(tid, kEglBadAttribute); return 0U; }
                cursor = cursor.Add(8U);
            }
            if (!terminated) { SetError(tid, kEglBadAttribute); return 0U; }
        }
        if (args[2] != 0U && std::bit_cast<std::int32_t>(args[3]) < 0) {
            SetError(tid, kEglBadParameter); return 0U;
        }
        if (matches && args[2] != 0U && args[3] > 0U) graphics.Write32(args[2], kFakeConfig, tid);
        graphics.Write32(num_config, matches ? 1U : 0U, tid);
        return 1U;
    }
    if constexpr (FunctionId == 3U) {
        if (args[0] != kFakeDisplay) { SetError(tid, kEglBadDisplay); return 0U; }
        if (args[1] != kFakeConfig) { SetError(tid, kEglBadConfig); return 0U; }
        { std::scoped_lock lock(mutex_); if (!initialized_) { threads_[tid].error = kEglNotInitialized; return 0U; } }
        std::uint32_t value{};
        switch (args[2]) {
        case kEglConfigId: value = 1U; break;
        case kEglBufferSize: value = 32U; break;
        case kEglRedSize: case kEglGreenSize: case kEglBlueSize: case kEglAlphaSize: value = 8U; break;
        case kEglDepthSize: value = 24U; break;
        case kEglStencilSize: value = 8U; break;
        case kEglSurfaceType: value = kEglWindowBit | kEglPbufferBit; break;
        case kEglRenderableType: case kEglConformant:
            value = kEglOpenGlEsBit | kEglOpenGlEs2Bit | kEglOpenGlEs3Bit;
            break;
        case kEglMaxPbufferWidth: case kEglMaxPbufferHeight: value = 4096U; break;
        case kEglMaxPbufferPixels: value = 4096U * 4096U; break;
        case kEglMinSwapInterval: value = 0U; break;
        case kEglMaxSwapInterval: value = 1U; break;
        case kEglColorBufferType: value = kEglRgbBuffer; break;
        case kEglConfigCaveat: case kEglNativeVisualType:
        case kEglTransparentType: value = kEglNone; break;
        case kEglLevel: case kEglNativeRenderable: case kEglNativeVisualId:
        case kEglSamples: case kEglSampleBuffers: case kEglLuminanceSize:
        case kEglAlphaMaskSize: case kEglTransparentRedValue:
        case kEglTransparentGreenValue: case kEglTransparentBlueValue:
            value = 0U; break;
        default: SetError(tid, kEglBadAttribute); return 0U;
        }
        if (args[3] == 0U) { SetError(tid, kEglBadParameter); return 0U; }
        graphics.Write32(args[3], value, tid);
        return 1U;
    }
    if constexpr (FunctionId == 4U) {
        if (args[0] != kFakeDisplay) { SetError(tid, kEglBadDisplay); return 0U; }
        if (args[1] != kFakeConfig) { SetError(tid, kEglBadConfig); return 0U; }
        if (args[2] == 0U) { SetError(tid, kEglBadNativeWindow); return 0U; }
        if (args[3] != 0U && calls_.address_space.Read32(memory::GuestAddress{args[3]}, tid) != kEglNone) {
            SetError(tid, kEglBadAttribute); return 0U;
        }
        std::scoped_lock lock(mutex_);
        if (!initialized_) { threads_[tid].error = kEglNotInitialized; return 0U; }
        const auto handle = next_surface_++;
        surfaces_.emplace(handle, SurfaceState{kFakeDisplay, kFakeConfig, SurfaceKind::window,
            graphics.layout.logical_width, graphics.layout.logical_height});
        return handle;
    }
    if constexpr (FunctionId == 5U) {
        if (args[0] != kFakeDisplay) { SetError(tid, kEglBadDisplay); return 0U; }
        if (args[1] != kFakeConfig) { SetError(tid, kEglBadConfig); return 0U; }
        std::uint32_t version = 1U;
        if (args[3] != 0U) {
            auto cursor = memory::GuestAddress{args[3]}; bool terminated{};
            for (std::size_t word = 0; word < kMaximumAttributeWords; word += 2U) {
                const auto attribute = calls_.address_space.Read32(cursor, tid);
                if (attribute == kEglNone) { terminated = true; break; }
                if (attribute != kEglContextClientVersion) { SetError(tid, kEglBadAttribute); return 0U; }
                version = calls_.address_space.Read32(cursor.Add(4U), tid);
                cursor = cursor.Add(8U);
            }
            if (!terminated) { SetError(tid, kEglBadAttribute); return 0U; }
        }
        if (version < 1U || version > 3U) { SetError(tid, kEglBadAttribute); return 0U; }
        std::scoped_lock lock(mutex_);
        if (!initialized_) { threads_[tid].error = kEglNotInitialized; return 0U; }
        if (args[2] != 0U && !contexts_.contains(args[2])) { threads_[tid].error = kEglBadContext; return 0U; }
        const auto handle = next_context_++;
        ContextState state;
        state.display = kFakeDisplay;
        state.config = kFakeConfig;
        state.client_version = version;
        state.share_context = args[2];
        state.guest_state = GuestGlContext{handle};
        const auto inserted = contexts_.emplace(handle, std::move(state)).first;
        inserted->second.gles1_matrices =
            std::make_unique<detail::AndroidBoundaryGles1MatrixState>(
                inserted->second.guest_state.Shared());
        return handle;
    }
    if constexpr (FunctionId == 6U) {
        std::scoped_lock execution_lock(graphics.execution_mutex);
        if (args[0] != kFakeDisplay) { SetError(tid, kEglBadDisplay); return 0U; }
        const bool release = args[1] == 0U && args[2] == 0U && args[3] == 0U;
        if (!release && (args[1] == 0U || args[2] == 0U || args[3] == 0U)) {
            SetError(tid, kEglBadMatch); return 0U;
        }
        {
            std::scoped_lock lock(mutex_);
            if (!initialized_) { threads_[tid].error = kEglNotInitialized; return 0U; }
            if (!release) {
                const auto context = contexts_.find(args[3]);
                if (context == contexts_.end() || context->second.destroy_pending) { threads_[tid].error = kEglBadContext; return 0U; }
                const auto draw = surfaces_.find(args[1]); const auto read = surfaces_.find(args[2]);
                if (draw == surfaces_.end() || read == surfaces_.end() || draw->second.destroy_pending || read->second.destroy_pending) {
                    threads_[tid].error = kEglBadSurface; return 0U;
                }
                if (context->second.current_thread.has_value() &&
                    *context->second.current_thread != tid) {
                    threads_[tid].error = kEglBadAccess; return 0U;
                }
            }
        }
        bool use_managed_surface{};
        if (!release && graphics.managed_surface) {
            std::scoped_lock lock(mutex_);
            use_managed_surface =
                surfaces_.at(args[1]).kind == SurfaceKind::window &&
                surfaces_.at(args[2]).kind == SurfaceKind::window;
        }
        if (use_managed_surface) {
            if (!graphics.angle_frame.has_value()) { SetError(tid, kEglBadNativeWindow); return 0U; }
            std::scoped_lock lock(mutex_);
            const auto old = threads_.find(tid);
            if (old != threads_.end() && old->second.context != 0U) {
                auto& old_context = contexts_.at(old->second.context);
                const auto old_frame = old_context.frames.find(
                    old_context.current_bound_surface);
                if (old_frame != old_context.frames.end()) {
                    old_frame->second->ReleaseCurrent();
                }
            }
            graphics.angle_frame->BindCurrentOnCallingThread();
            graphics.gl_owner = std::this_thread::get_id();
        } else {
            std::scoped_lock lock(mutex_);
            auto& thread = threads_[tid];
            const auto old_context_handle = thread.context;
            const auto old_surface_handle = thread.draw_surface;
            if (old_context_handle != 0U &&
                (release || old_context_handle != args[3] ||
                 old_surface_handle != args[1])) {
                auto& old_context = contexts_.at(old_context_handle);
                const auto old_frame = old_context.frames.find(old_surface_handle);
                if (old_frame != old_context.frames.end()) {
                    const auto native_error = old_frame->second->GetError();
                    if (native_error != 0U) {
                        graphics.gl_context.Shared().SetGuestError(native_error);
                    }
                    old_frame->second->ReleaseCurrent();
                }
                old_context.current_host_thread.reset();
                old_context.current_draw_surface = 0U;
                old_context.current_bound_surface = 0U;
            }
            if (!release &&
                (old_context_handle != args[3] || old_surface_handle != args[1])) {
                auto& target = contexts_.at(args[3]);
                auto frame = target.frames.find(args[1]);
                if (frame == target.frames.end()) {
                    const auto& surface = surfaces_.at(args[1]);
                    gles::EglHandle share_native{};
                    if (!target.frames.empty()) {
                        share_native = target.frames.begin()->second->NativeContext();
                    } else if (target.share_context != 0U) {
                        auto& share = contexts_.at(target.share_context);
                        if (!share.frames.empty()) {
                            share_native = share.frames.begin()->second->NativeContext();
                        }
                    }
                    auto created = std::make_unique<gles::AngleFrame>(
                        gles::AngleFrame::CreatePbuffer(
                            graphics.backend,
                            surface.width * graphics.layout.factor,
                            surface.height * graphics.layout.factor,
                            static_cast<int>(target.client_version),
                            share_native));
                    const auto logical_width = static_cast<std::int32_t>(surface.width);
                    const auto logical_height = static_cast<std::int32_t>(surface.height);
                    const std::array<std::int32_t, 4> logical{
                        0, 0, logical_width, logical_height};
                    created->Viewport(0, 0,
                        logical_width * static_cast<std::int32_t>(graphics.layout.factor),
                        logical_height * static_cast<std::int32_t>(graphics.layout.factor));
                    created->Scissor(0, 0,
                        logical_width * static_cast<std::int32_t>(graphics.layout.factor),
                        logical_height * static_cast<std::int32_t>(graphics.layout.factor));
                    target.guest_state.Shared().SetViewport(logical);
                    target.guest_state.Shared().SetScissor(logical);
                    frame = target.frames.emplace(args[1], std::move(created)).first;
                    graphics.frames.SetRenderTargetReady(true);
                } else {
                    frame->second->BindCurrentOnCallingThread();
                    const auto& viewport = target.guest_state.Shared().Viewport();
                    const auto& scissor = target.guest_state.Shared().Scissor();
                    const auto factor = static_cast<std::int32_t>(
                        graphics.layout.factor);
                    frame->second->Viewport(viewport[0] * factor,
                                            viewport[1] * factor,
                                            viewport[2] * factor,
                                            viewport[3] * factor);
                    frame->second->Scissor(scissor[0] * factor,
                                           scissor[1] * factor,
                                           scissor[2] * factor,
                                           scissor[3] * factor);
                }
                if (args[2] != args[1] && !target.frames.contains(args[2])) {
                    const auto& read_surface = surfaces_.at(args[2]);
                    auto read_frame = std::make_unique<gles::AngleFrame>(
                        gles::AngleFrame::CreatePbuffer(
                            graphics.backend,
                            read_surface.width * graphics.layout.factor,
                            read_surface.height * graphics.layout.factor,
                            static_cast<int>(target.client_version),
                            frame->second->NativeContext()));
                    read_frame->ReleaseCurrent();
                    target.frames.emplace(
                        args[2], std::make_unique<gles::AngleFrame>(
                            std::move(*read_frame)));
                    frame->second->BindCurrentOnCallingThread();
                }
                target.current_host_thread = std::this_thread::get_id();
                target.current_draw_surface = args[1];
                target.current_bound_surface = args[1];
            }
        }
        std::scoped_lock lock(mutex_);
        auto& state = threads_[tid];
        const auto previous_context = state.context;
        if (state.context != 0U) {
            contexts_.at(state.context).current_thread.reset();
        }
        if (state.draw_surface != 0U) --surfaces_.at(state.draw_surface).current_count;
        if (state.read_surface != 0U && state.read_surface != state.draw_surface) --surfaces_.at(state.read_surface).current_count;
        state.display = release ? 0U : args[0]; state.draw_surface = release ? 0U : args[1];
        state.read_surface = release ? 0U : args[2]; state.context = release ? 0U : args[3];
        if (!release) {
            contexts_.at(args[3]).current_thread = tid;
            contexts_.at(args[3]).current_host_thread = std::this_thread::get_id();
            contexts_.at(args[3]).current_draw_surface = args[1];
            ++surfaces_.at(args[1]).current_count;
            if (args[2] != args[1]) ++surfaces_.at(args[2]).current_count;
        }
        if (active_shadow_context_ != 0U &&
            active_shadow_context_ != args[3]) {
            const auto old = contexts_.find(active_shadow_context_);
            if (old != contexts_.end()) {
                old->second.guest_state = graphics.gl_context;
                old->second.gles1_matrices->CopyValuesFrom(
                    context_.gles1_state.Matrices());
                old->second.gles1_shade_model =
                    context_.gles1_state.ShadeModel();
                old->second.gles1_normalize =
                    context_.gles1_state.Capability(0x0BA1U);
                old->second.gles1_rescale_normal =
                    context_.gles1_state.Capability(0x803AU);
            }
        }
        if (!release) {
            graphics.gl_context = contexts_.at(args[3]).guest_state;
            context_.gles1_state.Matrices().CopyValuesFrom(
                *contexts_.at(args[3]).gles1_matrices);
            context_.gles1_state.SetShadeModel(
                contexts_.at(args[3]).gles1_shade_model);
            context_.gles1_state.SetCapability(
                0x0BA1U, contexts_.at(args[3]).gles1_normalize);
            context_.gles1_state.SetCapability(
                0x803AU, contexts_.at(args[3]).gles1_rescale_normal);
            active_shadow_context_ = args[3];
        } else if (active_shadow_context_ == previous_context) {
            active_shadow_context_ = 0U;
            graphics.gl_context.Reset();
        }
        CollectRetiredObjectsLocked();
        if (release) context_.api_routing.Release(tid);
        else context_.api_routing.Bind(tid, contexts_.at(args[3]).client_version);
        return 1U;
    }
    if constexpr (FunctionId == 7U) {
        if (args[0] != kFakeDisplay) { SetError(tid, kEglBadDisplay); return 0U; }
        std::uint32_t value{};
        {
            std::scoped_lock lock(mutex_);
            if (!initialized_) { threads_[tid].error = kEglNotInitialized; return 0U; }
            const auto surface = surfaces_.find(args[1]);
            if (surface == surfaces_.end()) { threads_[tid].error = kEglBadSurface; return 0U; }
            switch (args[2]) {
            case kEglWidth: value = surface->second.width; break;
            case kEglHeight: value = surface->second.height; break;
            case kEglConfigId: value = 1U; break;
            case kEglLargestPbuffer: case kEglMipmapTexture:
            case kEglMipmapLevel: value = 0U; break;
            case kEglTextureFormat: case kEglTextureTarget: value = kEglNone; break;
            case kEglRenderBuffer: value = kEglBackBuffer; break;
            case kEglSwapBehavior: value = kEglBufferDestroyed; break;
            case kEglMultisampleResolve: value = kEglMultisampleResolveDefault; break;
            case kEglHorizontalResolution: case kEglVerticalResolution:
            case kEglPixelAspectRatio: value = kEglDontCare; break;
            default: threads_[tid].error = kEglBadAttribute; return 0U;
            }
        }
        if (args[3] == 0U) { SetError(tid, kEglBadParameter); return 0U; }
        graphics.Write32(args[3], value, tid);
        return 1U;
    }
    if constexpr (FunctionId == 8U) {
        if (guest_graphics_retired_.load(std::memory_order_acquire)) {
            SetError(tid, kEglBadNativeWindow);
            return 0U;
        }
        if (args[0] != kFakeDisplay) { SetError(tid, kEglBadDisplay); return 0U; }
        { std::scoped_lock lock(mutex_); const auto found = surfaces_.find(args[1]);
          if (!initialized_) { threads_[tid].error = kEglNotInitialized; return 0U; }
          if (found == surfaces_.end()) { threads_[tid].error = kEglBadSurface; return 0U; }
          if (threads_[tid].draw_surface != args[1]) { threads_[tid].error = kEglBadSurface; return 0U; } }
        auto* frame = graphics.CurrentFrame();
        if (frame == nullptr) { SetError(tid, kEglBadSurface); return 0U; }
        SurfaceKind kind{};
        { std::scoped_lock lock(mutex_); kind = surfaces_.at(args[1]).kind; }
        if (kind == SurfaceKind::window) graphics.PublishFrame();
        else frame->Finish();
        return 1U;
    }
    if constexpr (FunctionId == 9U) {
        if (args[0] != kFakeDisplay) { SetError(tid, kEglBadDisplay); return 0U; }
        std::scoped_lock lock(mutex_);
        if (!initialized_) { threads_[tid].error = kEglNotInitialized; return 0U; }
        const auto found = contexts_.find(args[1]);
        if (found == contexts_.end()) { threads_[tid].error = kEglBadContext; return 0U; }
        found->second.destroy_pending = true; CollectRetiredObjectsLocked(); return 1U;
    }
    if constexpr (FunctionId == 10U) {
        if (args[0] != kFakeDisplay) { SetError(tid, kEglBadDisplay); return 0U; }
        std::scoped_lock lock(mutex_);
        if (!initialized_) { threads_[tid].error = kEglNotInitialized; return 0U; }
        const auto found = surfaces_.find(args[1]);
        if (found == surfaces_.end()) { threads_[tid].error = kEglBadSurface; return 0U; }
        found->second.destroy_pending = true; CollectRetiredObjectsLocked(); return 1U;
    }
    if constexpr (FunctionId == 11U) {
        std::scoped_lock execution_lock(graphics.execution_mutex);
        if (args[0] != kFakeDisplay) { SetError(tid, kEglBadDisplay); return 0U; }
        gles::AngleFrame* release_frame{};
        {
            std::scoped_lock lock(mutex_);
            if (!initialized_) { threads_[tid].error = kEglNotInitialized; return 0U; }
            const auto current = threads_.find(tid);
            if (current != threads_.end() && current->second.context != 0U) {
                auto& context = contexts_.at(current->second.context);
                const auto frame = context.frames.find(
                    context.current_bound_surface);
                if (frame != context.frames.end()) {
                    release_frame = frame->second.get();
                }
            }
        }
        if (release_frame != nullptr) release_frame->ReleaseCurrent();
        if (!graphics.managed_surface) {
            graphics.gl_owner.reset();
            graphics.angle_frame.reset();
            graphics.frames.SetRenderTargetReady(false);
        }
        std::scoped_lock lock(mutex_);
        initialized_ = false;
        for (auto& [handle, context] : contexts_) {
            static_cast<void>(handle);
            context.destroy_pending = true;
        }
        for (auto& [handle, surface] : surfaces_) {
            static_cast<void>(handle);
            surface.destroy_pending = true;
        }
        const auto current = threads_.find(tid);
        if (current != threads_.end() && current->second.context != 0U) {
            auto& context = contexts_.at(current->second.context);
            context.current_thread.reset();
            context.current_host_thread.reset();
            context.current_draw_surface = 0U;
            context.current_bound_surface = 0U;
            --surfaces_.at(current->second.draw_surface).current_count;
            if (current->second.read_surface != current->second.draw_surface) {
                --surfaces_.at(current->second.read_surface).current_count;
            }
        }
        threads_.erase(tid);
        CollectRetiredObjectsLocked();
        if (active_shadow_context_ != 0U &&
            !contexts_.contains(active_shadow_context_)) {
            active_shadow_context_ = 0U;
            graphics.ResetGuestGraphics();
        }
        context_.api_routing.Deactivate();
        return 1U;
    }
    if constexpr (FunctionId == 12U) return TakeError(tid);
    if constexpr (FunctionId == 13U) {
        if (args[0] != kFakeDisplay) {
            SetError(tid, kEglBadDisplay);
            return 0U;
        }
        { std::scoped_lock lock(mutex_); if (!initialized_) { threads_[tid].error = kEglNotInitialized; return 0U; } }
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
        { std::scoped_lock lock(mutex_); if (!initialized_) { threads_[tid].error = kEglNotInitialized; return 0U; } }
        if (args[3] == 0U) { SetError(tid, kEglBadParameter); return 0U; }
        const auto size = std::bit_cast<std::int32_t>(args[2]);
        if (size < 0) {
            SetError(tid, kEglBadParameter);
            return 0U;
        }
        if (args[1] != 0U && size > 0) {
            graphics.Write32(args[1], kFakeConfig, tid);
        }
        graphics.Write32(args[3], 1U, tid);
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
        std::uint32_t value{};
        {
            std::scoped_lock lock(mutex_);
            if (!initialized_) { threads_[tid].error = kEglNotInitialized; return 0U; }
            const auto context = contexts_.find(args[1]);
            if (context == contexts_.end()) { threads_[tid].error = kEglBadContext; return 0U; }
            if (args[2] == kEglConfigId) value = 1U;
            else if (args[2] == kEglContextClientType) value = kEglOpenGlEsApi;
            else if (args[2] == kEglContextClientVersion) value = context->second.client_version;
            else { threads_[tid].error = kEglBadAttribute; return 0U; }
        }
        if (args[3] == 0U) { SetError(tid, kEglBadParameter); return 0U; }
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
        std::scoped_lock execution_lock(graphics.execution_mutex);
        gles::AngleFrame* release_frame{};
        {
            std::scoped_lock lock(mutex_);
            const auto found = threads_.find(tid);
            if (found != threads_.end() && found->second.context != 0U) {
                auto& context = contexts_.at(found->second.context);
                const auto frame = context.frames.find(
                    context.current_bound_surface);
                if (frame != context.frames.end()) release_frame = frame->second.get();
            }
        }
        if (release_frame != nullptr) release_frame->ReleaseCurrent();
        else if (graphics.managed_surface && graphics.gl_owner.has_value())
            graphics.ReleaseManagedSurfaceFromCallingThread();
        {
            std::scoped_lock lock(mutex_);
            const auto found = threads_.find(tid);
            if (found != threads_.end() && found->second.context != 0U) {
                const auto context_handle = found->second.context;
                auto& context = contexts_.at(context_handle);
                context.current_thread.reset();
                context.current_host_thread.reset();
                context.current_draw_surface = 0U;
                context.current_bound_surface = 0U;
                --surfaces_.at(found->second.draw_surface).current_count;
                if (found->second.read_surface != found->second.draw_surface) --surfaces_.at(found->second.read_surface).current_count;
                if (active_shadow_context_ == context_handle) {
                    active_shadow_context_ = 0U;
                    graphics.ResetGuestGraphics();
                }
            }
            threads_.erase(tid);
            CollectRetiredObjectsLocked();
        }
        context_.api_routing.Release(tid);
        return 1U;
    }
    if constexpr (FunctionId == 23U) {
        if (args[0] != kFakeDisplay) {
            SetError(tid, kEglBadDisplay);
            return 0U;
        }
        const auto interval = std::bit_cast<std::int32_t>(args[1]);
        if (interval < 0 || interval > 1) {
            SetError(tid, kEglBadParameter);
            return 0U;
        }
        std::scoped_lock lock(mutex_);
        if (!initialized_) { threads_[tid].error = kEglNotInitialized; return 0U; }
        const auto current = threads_.find(tid);
        if (current == threads_.end() || current->second.draw_surface == 0U) { threads_[tid].error = kEglBadSurface; return 0U; }
        surfaces_.at(current->second.draw_surface).swap_interval = static_cast<std::uint32_t>(interval);
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
                        (std::numeric_limits<std::int32_t>::max)()) /
                         graphics.layout.factor ||
            height > static_cast<std::uint32_t>(
                         (std::numeric_limits<std::int32_t>::max)()) /
                          graphics.layout.factor) {
            SetError(tid, kEglBadParameter);
            return 0U;
        }
        std::scoped_lock lock(mutex_);
        if (!initialized_) { threads_[tid].error = kEglNotInitialized; return 0U; }
        const auto handle = next_surface_++;
        surfaces_.emplace(handle, SurfaceState{kFakeDisplay, kFakeConfig,
            SurfaceKind::pbuffer, width, height});
        return handle;
    }
    if constexpr (FunctionId == 25U) {
        if (args[0] != kFakeDisplay) { SetError(tid, kEglBadDisplay); return 0U; }
        if (args[1] != kFakeConfig) { SetError(tid, kEglBadConfig); return 0U; }
        if (args[3] != 0U && calls_.address_space.Read32(memory::GuestAddress{args[3]}, tid) != kEglNone) {
            SetError(tid, kEglBadAttribute); return 0U;
        }
        { std::scoped_lock lock(mutex_); if (!initialized_) { threads_[tid].error = kEglNotInitialized; return 0U; } }
        SetError(tid, kEglBadNativePixmap); return 0U;
    }
    if constexpr (FunctionId == 26U) {
        if (args[0] != kFakeDisplay) { SetError(tid, kEglBadDisplay); return 0U; }
        { std::scoped_lock lock(mutex_);
          if (!initialized_) { threads_[tid].error = kEglNotInitialized; return 0U; }
          if (!surfaces_.contains(args[1])) { threads_[tid].error = kEglBadSurface; return 0U; } }
        SetError(tid, kEglBadNativePixmap); return 0U;
    }
    if constexpr (FunctionId == 27U) {
        if (args[0] != kFakeDisplay) { SetError(tid, kEglBadDisplay); return 0U; }
        std::scoped_lock lock(mutex_);
        if (!initialized_) { threads_[tid].error = kEglNotInitialized; return 0U; }
        if (!surfaces_.contains(args[1])) { threads_[tid].error = kEglBadSurface; return 0U; }
        if ((args[2] == kEglSwapBehavior && args[3] == kEglBufferDestroyed) ||
            (args[2] == kEglMultisampleResolve && args[3] == kEglMultisampleResolveDefault)) return 1U;
        threads_[tid].error = (args[2] == kEglSwapBehavior || args[2] == kEglMultisampleResolve)
                                  ? kEglBadMatch : kEglBadAttribute;
        return 0U;
    }
    if constexpr (FunctionId == 28U || FunctionId == 29U) {
        if (args[0] != kFakeDisplay) { SetError(tid, kEglBadDisplay); return 0U; }
        std::scoped_lock lock(mutex_);
        if (!initialized_) { threads_[tid].error = kEglNotInitialized; return 0U; }
        if (!surfaces_.contains(args[1])) { threads_[tid].error = kEglBadSurface; return 0U; }
        if (args[2] != kEglBackBuffer) { threads_[tid].error = kEglBadParameter; return 0U; }
        threads_[tid].error = kEglBadMatch; return 0U;
    }
    if constexpr (FunctionId == 30U || FunctionId == 32U) {
        { std::scoped_lock lock(mutex_); const auto current = threads_.find(tid);
          if (current == threads_.end() || current->second.context == 0U || current->second.draw_surface == 0U) {
              threads_[tid].error = kEglBadCurrentSurface; return 0U;
          } }
        auto* frame = graphics.CurrentFrame();
        if (frame == nullptr) { SetError(tid, kEglBadCurrentSurface); return 0U; }
        frame->Finish(); return 1U;
    }
    if constexpr (FunctionId == 31U) {
        if (args[0] != kEglCoreNativeEngine) { SetError(tid, kEglBadParameter); return 0U; }
        return 1U;
    }
    if constexpr (FunctionId == 33U) {
        if (args[0] != kFakeDisplay) { SetError(tid, kEglBadDisplay); return 0U; }
        if (call.Argument(3) != kFakeConfig) { SetError(tid, kEglBadConfig); return 0U; }
        if (args[1] != kEglOpenVgImage || args[2] == 0U) { SetError(tid, kEglBadParameter); return 0U; }
        const auto attributes = call.Argument(4);
        if (attributes != 0U && calls_.address_space.Read32(memory::GuestAddress{attributes}, tid) != kEglNone) {
            SetError(tid, kEglBadAttribute); return 0U;
        }
        { std::scoped_lock lock(mutex_); if (!initialized_) { threads_[tid].error = kEglNotInitialized; return 0U; } }
        SetError(tid, kEglBadMatch); return 0U;
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
