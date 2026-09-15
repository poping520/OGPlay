#include "runtime/boundary/modules/egl/egl_module.h"

#include <array>
#include <algorithm>
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
#include "ogplay/gles/guest_transfer.h"
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
    PublishedString{kEglClientApis, 2048U, "OpenGL_ES"},
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

EglModule::~EglModule() { RetireExtensionsLocked(); }

void EglModule::RetireExtensionsLocked() noexcept {
    if (!native_display_) return;
    for (const auto& [handle, native] : syncs_) {
        static_cast<void>(handle);
        try { native_display_->DestroySync(native); } catch (...) {}
    }
    for (const auto& [handle, native] : images_) {
        static_cast<void>(handle);
        try { native_display_->DestroyImage(native); } catch (...) {}
    }
    syncs_.clear(); images_.clear();
}

std::vector<std::int32_t> EglModule::ReadAttributes(
    const GuestPtr<std::int32_t> address, const std::uint64_t tid) {
    std::vector<std::int32_t> result;
    if (!address.IsNull()) {
        for (std::size_t index = 0; index < kMaximumAttributeWords; index += 2) {
            const auto name = calls_.address_space.Read32(address.Address().Add(index * 4U), tid);
            if (name == kEglNone) { result.push_back(static_cast<std::int32_t>(kEglNone)); return result; }
            result.push_back(std::bit_cast<std::int32_t>(name));
            result.push_back(std::bit_cast<std::int32_t>(calls_.address_space.Read32(
                address.Address().Add(index * 4U + 4U), tid)));
        }
        throw gles::EglLifecycleError(gles::EglOperation::unavailable, kEglBadAttribute);
    }
    result.push_back(static_cast<std::int32_t>(kEglNone)); return result;
}

std::string EglModule::GuestExtensionsLocked() {
    std::string result = "EGL_KHR_get_all_proc_addresses ";
    if (!native_display_) native_display_ = gles::EglDisplayResources::Create(context_.graphics.backend);
    const auto native = " " + native_display_->Extensions() + " ";
    for (const auto name : {"EGL_KHR_fence_sync", "EGL_KHR_reusable_sync", "EGL_KHR_wait_sync", "EGL_KHR_image_base",
                           "EGL_KHR_gl_texture_2D_image", "EGL_KHR_gl_texture_cubemap_image",
                           "EGL_KHR_gl_renderbuffer_image"}) {
        if (native.find(" " + std::string(name) + " ") != std::string::npos) {
            result += name; result += ' ';
        }
    }
    return result;
}

BoundaryCallServices& EglModule::CallServices() noexcept { return calls_; }

void EglModule::RetireGuestGraphics() noexcept {
    guest_graphics_retired_.store(true, std::memory_order_release);
}

gles::AngleFrame* EglModule::CurrentFrameForHostThread(
    const std::thread::id host_thread, const std::string_view operation) {
    static_cast<void>(operation);
    std::scoped_lock lock(mutex_);
    for (auto& [handle, context] : contexts_) {
        static_cast<void>(handle);
        if (context.current_host_thread == host_thread && context.current_draw_surface != 0U)
            return context.frame.get();
    }
    return nullptr;
}

void EglModule::SaveActiveStateLocked() {
    const auto old = contexts_.find(active_shadow_context_);
    if (old == contexts_.end()) return;
    old->second.guest_state = context_.graphics.gl_context;
    old->second.gles1_state->CopyValuesFrom(context_.gles1_state);
    old->second.gles1_draw = context_.gles1_draw;
    old->second.gles1_legacy = context_.gles1_legacy;
}

void EglModule::RestoreStateLocked(const std::uint32_t handle) {
    if (active_shadow_context_ == handle) return;
    SaveActiveStateLocked();
    auto& state = contexts_.at(handle);
    context_.graphics.gl_context = state.guest_state;
    context_.gles1_state.CopyValuesFrom(*state.gles1_state);
    context_.gles1_draw = state.gles1_draw;
    context_.gles1_legacy = state.gles1_legacy;
    active_shadow_context_ = handle;
}

void EglModule::ActivateStateForHostThread(const std::thread::id host_thread) {
    std::scoped_lock lock(mutex_);
    for (const auto& [handle, state] : contexts_) {
        if (state.current_host_thread == host_thread) { RestoreStateLocked(handle); return; }
    }
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
    std::vector<std::uint32_t> groups;
    for (const auto& [handle, state] : contexts_) {
        static_cast<void>(handle);
        if (state.destroy_pending && !state.current_thread.has_value()) groups.push_back(state.share_group);
    }
    std::erase_if(contexts_, [](const auto& entry) {
        return entry.second.destroy_pending &&
               !entry.second.current_thread.has_value();
    });
    std::erase_if(surfaces_, [](const auto& entry) {
        return entry.second.destroy_pending && entry.second.current_count == 0U;
    });
    for (const auto group : groups) {
        const bool alive = std::ranges::any_of(contexts_, [group](const auto& entry) { return entry.second.share_group == group; });
        if (!alive && context_.graphics.retire_share_group != nullptr)
            context_.graphics.retire_share_group(context_.graphics.owner, group);
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
            const auto value = entry.name == kEglExtensions ? GuestExtensionsLocked() : std::string(entry.value);
            std::vector<std::byte> bytes(value.size() + 1U);
            for (std::size_t index = 0; index < value.size(); ++index) {
                bytes[index] = static_cast<std::byte>(
                    static_cast<unsigned char>(value[index]));
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
    if constexpr (FunctionId == 0xF100U || FunctionId == 0xF101U) {
        std::scoped_lock execution_lock(context_.graphics.execution_mutex);
        context_.graphics.ActivateCurrentContext();
        gles::EglHandle native{};
        {
            std::scoped_lock lock(mutex_);
            const auto image = images_.find(call.Argument(1));
            if (image == images_.end()) {
                context_.graphics.gl_context.Shared().SetGuestError(0x0501U); return 0U;
            }
            native = image->second;
        }
        try { context_.graphics.RequireFrame("glEGLImageTarget").BindEglImage(
            call.Argument(0), native, FunctionId == 0xF101U); }
        catch (const gles::GlesApiError& error) {
            context_.graphics.gl_context.Shared().SetGuestError(error.Code());
        }
        return 0U;
    }
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
                } else if (attribute == 0x3039U || attribute == 0x303AU) {
                    if (value != kEglDontCare && value > 1U) { SetError(tid, kEglBadAttribute); return 0U; }
                    std::scoped_lock lock(mutex_);
                    if (!native_display_) native_display_ = gles::EglDisplayResources::Create(graphics.backend);
                    matches &= value == kEglDontCare || value == static_cast<std::uint32_t>(native_display_->ConfigAttribute(attribute));
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
        case 0x3039U: case 0x303AU: {
            std::scoped_lock lock(mutex_);
            try {
                if (!native_display_) native_display_ = gles::EglDisplayResources::Create(graphics.backend);
                value = static_cast<std::uint32_t>(native_display_->ConfigAttribute(args[2]));
            } catch (const gles::EglLifecycleError& error) { threads_[tid].error = error.NativeError(); return 0U; }
            break;
        }
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
        std::scoped_lock execution_lock(graphics.execution_mutex);
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
        SaveActiveStateLocked();
        const auto handle = next_context_++;
        ContextState state;
        state.display = kFakeDisplay;
        state.config = kFakeConfig;
        state.client_version = version;
        state.share_context = args[2];
        state.share_group = args[2] == 0U ? handle : contexts_.at(args[2]).share_group;
        state.guest_state = GuestGlContext{handle};
        state.guest_state.SetShareGroup(state.share_group);
        state.gles1_state = std::make_unique<detail::AndroidBoundaryGles1State>();
        state.gles1_state->CopyValuesFrom(context_.gles1_state);
        state.gles1_state->Reset();
        if (args[2] != 0U) {
            const auto& source = contexts_.at(args[2]);
            state.guest_state.Shared().ShareObjectsFrom(source.guest_state.Shared());
            state.gles1_state->ShareBuffersFrom(*source.gles1_state);
        }
        state.gles1_draw = context_.gles1_draw;
        state.gles1_draw.Reset();
        try {
            if (!native_display_) native_display_ = gles::EglDisplayResources::Create(graphics.backend);
            const auto share_native = args[2] == 0U ? 0U : contexts_.at(args[2]).frame->NativeContext();
            state.frame = std::make_unique<gles::AngleFrame>(gles::AngleFrame::CreateContext(
                native_display_, static_cast<int>(version == 1U ? 2U : version), share_native));
        } catch (const gles::EglLifecycleError& error) {
            threads_[tid].error = error.NativeError(); return 0U;
        }
        contexts_.emplace(handle, std::move(state));
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
        {
            std::scoped_lock lock(mutex_);
            auto& thread = threads_[tid];
            // A surface cannot simultaneously be current on another guest thread.
            if (!release) for (const auto& [other_tid, other] : threads_) {
                if (other_tid == tid) continue;
                if (other.draw_surface == args[1] || other.draw_surface == args[2] ||
                    other.read_surface == args[1] || other.read_surface == args[2]) {
                    threads_[tid].error = kEglBadAccess; return 0U;
                }
            }
            if (!release) {
                auto& target = contexts_.at(args[3]);
                try {
                    for (const auto handle : {args[1], args[2]}) {
                        auto& surface = surfaces_.at(handle);
                        if (!surface.backing) surface.backing = gles::EglSurfaceResources::Create(
                            native_display_, surface.width * graphics.layout.factor,
                            surface.height * graphics.layout.factor, surface.texture_format, surface.mipmap);
                    }
                    target.frame->BindSurfaces(surfaces_.at(args[1]).backing,
                                               surfaces_.at(args[2]).backing);
                } catch (const gles::EglLifecycleError& error) {
                    threads_[tid].error = error.NativeError(); return 0U;
                }
                if (!target.viewport_initialized) {
                    const auto& surface = surfaces_.at(args[1]);
                    const std::array<std::int32_t, 4> logical{0, 0,
                        static_cast<std::int32_t>(surface.width), static_cast<std::int32_t>(surface.height)};
                    target.guest_state.Shared().SetViewport(logical);
                    target.guest_state.Shared().SetScissor(logical);
                    target.viewport_initialized = true;
                }
                graphics.frames.SetRenderTargetReady(true);
            } else if (thread.context != 0U) {
                contexts_.at(thread.context).frame->ReleaseCurrent();
            }
            if (thread.context != 0U && thread.context != args[3]) {
                auto& old = contexts_.at(thread.context);
                // eglMakeCurrent already switched the native binding. Do not
                // unbind here: that would unbind the new Context as well.
                old.frame->MarkNotCurrent();
                old.current_host_thread.reset();
                old.current_draw_surface = 0U;
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
        if (!release) RestoreStateLocked(args[3]);
        else if (active_shadow_context_ == previous_context) {
            SaveActiveStateLocked(); active_shadow_context_ = 0U;
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
            case kEglLargestPbuffer: value = 0U; break;
            case kEglMipmapTexture: value = surface->second.mipmap ? 1U : 0U; break;
            case kEglMipmapLevel: value = surface->second.mipmap_level; break;
            case kEglTextureFormat: value = surface->second.texture_format; break;
            case kEglTextureTarget: value = surface->second.texture_format == 0x305CU ? 0x305CU : 0x305FU; break;
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
        std::scoped_lock execution_lock(graphics.execution_mutex);
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
        if (kind == SurfaceKind::window) {
            std::shared_ptr<gles::EglSurfaceResources> draw, read;
            { std::scoped_lock lock(mutex_);
              draw = surfaces_.at(args[1]).backing;
              read = surfaces_.at(threads_.at(tid).read_surface).backing; }
            const auto target = frame->ClientVersion() >= 3 ? 0x8CA8U : 0x8D40U;
            const auto binding = static_cast<std::uint32_t>(frame->GetIntegers(
                frame->ClientVersion() >= 3 ? 0x8CAAU : 0x8CA6U, 1U).front());
            const auto restore = [&] { frame->BindFramebuffer(target, binding); frame->BindSurfaces(draw, read); };
            frame->BindSurfaces(draw, draw);
            try { frame->BindFramebuffer(target, 0U); graphics.PublishFrame(); }
            catch (...) { restore(); throw; }
            restore();
        } else frame->Finish();
        { std::scoped_lock lock(mutex_);
          try { surfaces_.at(args[1]).backing->SwapBuffers(); }
          catch (const gles::EglLifecycleError& error) { threads_[tid].error = error.NativeError(); return 0U; } }
        return 1U;
    }
    if constexpr (FunctionId == 9U) {
        std::scoped_lock execution_lock(graphics.execution_mutex);
        if (args[0] != kFakeDisplay) { SetError(tid, kEglBadDisplay); return 0U; }
        std::scoped_lock lock(mutex_);
        if (!initialized_) { threads_[tid].error = kEglNotInitialized; return 0U; }
        const auto found = contexts_.find(args[1]);
        if (found == contexts_.end()) { threads_[tid].error = kEglBadContext; return 0U; }
        found->second.destroy_pending = true; CollectRetiredObjectsLocked(); return 1U;
    }
    if constexpr (FunctionId == 10U) {
        std::scoped_lock execution_lock(graphics.execution_mutex);
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
        std::scoped_lock lock(mutex_);
        // EGL 1.4 keeps resources that are current to any thread alive until
        // they are subsequently released. Termination does not unbind the
        // caller, invalidate another thread's current route, or fail merely
        // because this display was already terminated.
        if (!initialized_) return 1U;
        initialized_ = false;
        RetireExtensionsLocked();
        for (auto& [handle, context] : contexts_) {
            static_cast<void>(handle);
            context.destroy_pending = true;
        }
        for (auto& [handle, surface] : surfaces_) {
            static_cast<void>(handle);
            surface.destroy_pending = true;
        }
        CollectRetiredObjectsLocked();
        if (active_shadow_context_ != 0U &&
            !contexts_.contains(active_shadow_context_)) {
            active_shadow_context_ = 0U;
            graphics.ResetGuestGraphics();
        }
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
                release_frame = context.frame.get();
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
                    SaveActiveStateLocked();
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
        const auto interval = std::clamp(call.Scalar<std::int32_t>(1), 0, 1);
        std::scoped_lock lock(mutex_);
        if (!initialized_) { threads_[tid].error = kEglNotInitialized; return 0U; }
        const auto current = threads_.find(tid);
        if (current == threads_.end() || current->second.draw_surface == 0U) { threads_[tid].error = kEglBadSurface; return 0U; }
        try { native_display_->SwapInterval(interval); }
        catch (const gles::EglLifecycleError& error) { threads_[tid].error = error.NativeError(); return 0U; }
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
        std::uint32_t texture_format{0x305CU}, texture_target{0x305CU};
        bool mipmap{};
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
                else if (attribute == kEglTextureFormat) texture_format = value;
                else if (attribute == kEglTextureTarget) texture_target = value;
                else if (attribute == kEglMipmapTexture && value <= 1U) mipmap = value != 0U;
                else if (attribute == kEglLargestPbuffer && value <= 1U) {}
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
        if ((texture_format != 0x305CU && texture_format != 0x305DU && texture_format != 0x305EU) ||
            (texture_target != 0x305CU && texture_target != 0x305FU)) {
            SetError(tid, kEglBadAttribute); return 0U;
        }
        if ((texture_format == 0x305CU) != (texture_target == 0x305CU)) {
            SetError(tid, kEglBadMatch); return 0U;
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
        SurfaceState surface{kFakeDisplay, kFakeConfig, SurfaceKind::pbuffer, width, height};
        surface.texture_format = texture_format; surface.mipmap = mipmap;
        try {
            if (!native_display_) native_display_ = gles::EglDisplayResources::Create(graphics.backend);
            surface.backing = gles::EglSurfaceResources::Create(native_display_,
                width * graphics.layout.factor, height * graphics.layout.factor, texture_format, mipmap);
        } catch (const gles::EglLifecycleError& error) { threads_[tid].error = error.NativeError(); return 0U; }
        surfaces_.emplace(handle, std::move(surface));
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
        if (args[2] == kEglMipmapLevel) {
            auto& surface = surfaces_.at(args[1]);
            if (surface.kind != SurfaceKind::pbuffer || !surface.mipmap) {
                threads_[tid].error = kEglBadMatch; return 0U;
            }
            try { surface.backing->SetAttribute(args[2], std::bit_cast<std::int32_t>(args[3])); }
            catch (const gles::EglLifecycleError& error) { threads_[tid].error = error.NativeError(); return 0U; }
            surface.mipmap_level = args[3]; return 1U;
        }
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
        auto& surface = surfaces_.at(args[1]);
        if (surface.kind != SurfaceKind::pbuffer || surface.texture_format == 0x305CU) {
            threads_[tid].error = kEglBadMatch; return 0U;
        }
        try { surface.backing->BindTexture(FunctionId == 28U); return 1U; }
        catch (const gles::EglLifecycleError& error) { threads_[tid].error = error.NativeError(); return 0U; }

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
    if constexpr (FunctionId >= 34U && FunctionId <= 41U) {
        std::unique_lock execution_lock(graphics.execution_mutex);
        graphics.ActivateCurrentContext();
        if (args[0] != kFakeDisplay) { SetError(tid, kEglBadDisplay); return 0U; }
        std::unique_lock lock(mutex_);
        if (!initialized_) { threads_[tid].error = kEglNotInitialized; return 0U; }
        try {
            if (!native_display_) native_display_ = gles::EglDisplayResources::Create(graphics.backend);
            if constexpr (FunctionId == 34U) {
                if (args[1] != 0x30F9U && args[1] != 0x30FAU) { threads_[tid].error = kEglBadAttribute; return 0U; }
                const auto attributes = ReadAttributes(call.Pointer<std::int32_t>(2), tid);
                const auto native = native_display_->CreateSync(args[1], attributes);
                const auto handle = next_sync_++; syncs_.emplace(handle, native); return handle;
            } else if constexpr (FunctionId == 39U) {
                const auto context = contexts_.find(args[1]);
                if (context == contexts_.end()) { threads_[tid].error = kEglBadContext; return 0U; }
                // Only GL object names cross this bridge. A native-buffer/pixmap
                // guest address must never be reinterpreted as a host pointer.
                if (args[2] != 0x30B1U && args[2] != 0x30B9U &&
                    !(args[2] >= 0x30B3U && args[2] <= 0x30B8U)) {
                    threads_[tid].error = kEglBadParameter; return 0U;
                }
                const auto attributes = ReadAttributes(call.Pointer<std::int32_t>(4), tid);
                const auto native = native_display_->CreateImage(context->second.frame->NativeContext(),
                    args[2], args[3], attributes);
                const auto handle = next_image_++; images_.emplace(handle, native); return handle;
            } else if constexpr (FunctionId == 40U) {
                const auto image = images_.find(args[1]);
                if (image == images_.end()) { threads_[tid].error = kEglBadParameter; return 0U; }
                native_display_->DestroyImage(image->second); images_.erase(image); return 1U;
            } else {
                const auto sync = syncs_.find(args[1]);
                if (sync == syncs_.end()) { threads_[tid].error = kEglBadParameter; return 0U; }
                if constexpr (FunctionId == 35U) {
                    native_display_->DestroySync(sync->second); syncs_.erase(sync); return 1U;
                } else if constexpr (FunctionId == 36U) {
                    const auto timeout = static_cast<std::uint64_t>(call.Argument(4)) |
                                         (static_cast<std::uint64_t>(call.Argument(5)) << 32U);
                    const auto native = sync->second;
                    auto display = native_display_;
                    lock.unlock(); execution_lock.unlock();
                    try { return display->ClientWaitSync(native, args[2], timeout); }
                    catch (const gles::EglLifecycleError& error) { SetError(tid, error.NativeError()); return 0U; }
                } else if constexpr (FunctionId == 37U) {
                    auto output = gles::GuestBuffer::Prepare(calls_.address_space,
                        memory::GuestAddress{args[3]}, 4U, gles::GuestTransferDirection::output, false, tid);
                    const auto value = native_display_->SyncAttribute(sync->second, args[2]);
                    const auto word = std::bit_cast<std::uint32_t>(value);
                    for (std::size_t index = 0; index < 4; ++index)
                        output.WritableBytes()[index] = static_cast<std::byte>(word >> (index * 8U));
                    output.Commit(); return 1U;
                } else if constexpr (FunctionId == 41U) {
                    native_display_->SignalSync(sync->second, args[2]); return 1U;
                } else {
                    native_display_->WaitSync(sync->second, args[2]); return 1U;
                }
            }
        } catch (const gles::EglLifecycleError& error) { threads_[tid].error = error.NativeError(); return 0U; }
    }
    throw std::logic_error("unbound concrete libEGL export");
}

#define OGPLAY_DEFINE_EGL(name, id, count, method)                             \
    std::uint32_t EglModule::method(const A32CallFrame& call) {                \
        return ExecuteExport<id>(call);                                        \
    }
OGPLAY_EGL_BOUNDARY_EXPORTS(OGPLAY_DEFINE_EGL)
OGPLAY_GLES_IMAGE_EXPORTS(OGPLAY_DEFINE_EGL)
#undef OGPLAY_DEFINE_EGL
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

}  // namespace ogplay::runtime
