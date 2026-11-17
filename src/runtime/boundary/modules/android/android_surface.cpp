#include "runtime/boundary/modules/android/android_module.h"
#include <stdexcept>

#include "ogplay/video/video_player.h"

namespace ogplay::runtime {
void AndroidModule::SetSurfaceHooks(AndroidSurfaceHooks hooks) {
    std::scoped_lock guard(surface_hook_mutex_, surface_publish_mutex_);
    surface_hooks_ = hooks;
    std::scoped_lock lock(mutex_);
    if (!hooks.publish) {
        for (auto& [_, window] : windows_) {
            window.cpu_locked = false;
            window.cpu_buffer.reset();
        }
    }
    if (!hooks.from_java) {
        surface_geometries_.clear();
        std::erase_if(windows_, [](const auto& entry) { return !entry.second.surface_identity.IsNull(); });
    }
}

void AndroidModule::RetireSurfaceWindow(memory::GuestAddress identity) {
    // fromSurface holds the hook mutex while acquiring the VM lock. Retirement
    // already owns that VM lock, so serialize pixels with a separate mutex.
    std::scoped_lock guard(surface_publish_mutex_);
    std::scoped_lock lock(mutex_);
    auto& geometry = surface_geometries_[identity.Value()];
    geometry = {identity, 0, 0, geometry.generation + 1U};
    for (auto it = windows_.begin(); it != windows_.end();) {
        auto& window = it->second;
        if (window.surface_identity == identity && window.owned) {
            window.active = false;
            window.owned = false;
            window.cpu_locked = false;
            window.cpu_buffer.reset();
            if (--window.references == 0) {
                it = windows_.erase(it);
                continue;
            }
        }
        ++it;
    }
}

BoundaryResult AndroidModule::ANativeWindow_fromSurface(const A32CallFrame& call) {
    std::scoped_lock guard(surface_hook_mutex_);
    if (!surface_hooks_.from_java) throw std::runtime_error("NDK Surface JNI backend is unavailable");
    return surface_hooks_
        .from_java(surface_hooks_.owner, call.ThreadId(), call.Pointer<void>(0).Address(),
                   SurfaceJavaReference{call.Argument(1)})
        .Value();
}

void AndroidModule::UpdateSurfaceWindow(const AndroidSurfaceWindow& surface) {
    std::scoped_lock lock(mutex_);
    auto& geometry = surface_geometries_[surface.identity.Value()];
    geometry = {surface.identity, surface.width, surface.height, geometry.generation};
    for (auto& [_, window] : windows_) {
        if (window.surface_identity == surface.identity && window.active) {
            window.width = surface.width;
            window.height = surface.height;
        }
    }
}

memory::GuestAddress AndroidModule::AcquireSurfaceWindow(const AndroidSurfaceWindow& surface) {
    if (surface.identity.IsNull()) return memory::GuestAddress{};
    if (surface.width == 0 || surface.height == 0) throw std::invalid_argument("Surface has no managed geometry");
    std::scoped_lock lock(mutex_);
    for (auto& [handle, window] : windows_) {
        if (window.surface_identity == surface.identity && window.active) {
            if (window.references == UINT32_MAX) throw std::overflow_error("ANativeWindow reference overflow");
            window.width = surface.width;
            window.height = surface.height;
            ++window.references;
            return memory::GuestAddress{handle};
        }
    }
    if (windows_.size() >= 4096 || next_window_.Value() >= kNativeActivityWindowHandleEnd.Value())
        throw std::length_error("Surface window handle budget exhausted");
    const auto handle = next_window_;
    // One holder-owned reference, plus the acquire performed by fromSurface.
    windows_.emplace(handle.Value(), WindowResource{surface.width, surface.height, 2, true, true, surface.identity});
    next_window_ = handle.Add(4);
    return handle;
}
}  // namespace ogplay::runtime

namespace ogplay::runtime {
std::optional<AndroidSurfaceWindow> AndroidModule::NativeWindowGeometry(memory::GuestAddress window) {
    std::scoped_lock lock(mutex_);
    return NativeWindowGeometryLocked(window);
}

std::optional<AndroidSurfaceWindow> AndroidModule::NativeWindowGeometryLocked(memory::GuestAddress window) const {
    const auto found = windows_.find(window.Value());
    if (found == windows_.end()) {
        const auto alias = surface_geometries_.find(window.Value());
        if (alias != surface_geometries_.end()) {
            if (alias->second.width && alias->second.height) return alias->second;
            return std::nullopt;
        }
        // Unregistered runner identities use the session layout. Reserved
        // NativeActivity handles must never fall back after retirement.
        if (window.Value() >= kNativeActivityWindowHandleBegin.Value() &&
            window.Value() < kNativeActivityWindowHandleEnd.Value())
            return std::nullopt;
        return AndroidSurfaceWindow{};
    }
    if (!found->second.active) return std::nullopt;
    const auto identity = found->second.surface_identity;
    const auto alias = surface_geometries_.find(identity.Value());
    const auto generation = alias == surface_geometries_.end() ? 0 : alias->second.generation;
    return AndroidSurfaceWindow{identity, found->second.width, found->second.height, generation};
}

AndroidModule::WindowConnectionKey AndroidModule::ProducerKey(memory::GuestAddress window,
                                                              const AndroidSurfaceWindow& geometry) {
    return {geometry.identity.IsNull() ? window : geometry.identity, geometry.generation};
}

bool AndroidModule::ConnectEglWindow(memory::GuestAddress window, const AndroidSurfaceWindow& expected) {
    std::scoped_lock lock(mutex_);
    const auto geometry = NativeWindowGeometryLocked(window);
    if (!geometry || ProducerKey(window, *geometry) != ProducerKey(window, expected)) return false;
    // Synthetic standalone EGL surfaces have no corresponding ANativeWindow.
    if (!geometry->width || !geometry->height) return true;
    const auto key = ProducerKey(window, *geometry);
    for (const auto& [handle, item] : windows_) {
        if (!item.active || !item.cpu_buffer) continue;
        if (ProducerKey(memory::GuestAddress{handle}, *NativeWindowGeometryLocked(memory::GuestAddress{handle})) == key)
            return false;
    }
    return egl_windows_.insert(key).second;
}

void AndroidModule::DisconnectEglWindow(memory::GuestAddress window, const AndroidSurfaceWindow& geometry) {
    if (!geometry.width || !geometry.height) return;
    std::scoped_lock lock(mutex_);
    egl_windows_.erase(ProducerKey(window, geometry));
}

namespace {
constexpr std::uint32_t kInvalidOperation = static_cast<std::uint32_t>(-38);
constexpr std::uint32_t kInvalidValue = static_cast<std::uint32_t>(-22);
constexpr std::uint32_t kNoInit = static_cast<std::uint32_t>(-19);
constexpr std::uint64_t kCpuBufferLimit = 32 * 1024 * 1024;

template <std::size_t Size>
std::array<std::byte, Size * 4> EncodeWords(const std::array<std::uint32_t, Size>& words) {
    std::array<std::byte, Size * 4> bytes{};
    for (std::size_t i = 0; i < Size; ++i)
        for (std::size_t j = 0; j < 4; ++j)
            bytes[i * 4 + j] = static_cast<std::byte>(words[i] >> (j * 8));
    return bytes;
}
}  // namespace

BoundaryResult AndroidModule::ANativeWindow_lock(const A32CallFrame& call) {
    const auto handle = call.Pointer<void>(0).Address();
    const auto output = call.Pointer<void>(1);
    const auto dirty = call.Pointer<void>(2);
    const auto thread = call.ThreadId();
    if (output.IsNull()) throw std::invalid_argument("ANativeWindow_lock requires an output buffer");
    std::scoped_lock publish(surface_publish_mutex_);
    std::scoped_lock lock(mutex_);
    auto& window = Window(handle.Value());
    if (window.cpu_locked) return kInvalidOperation;
    const auto geometry = NativeWindowGeometryLocked(handle);
    if (!geometry) return kNoInit;
    if (egl_windows_.contains(ProducerKey(handle, *geometry))) return kInvalidValue;
    if (!surface_hooks_.publish || geometry->identity.IsNull()) return kNoInit;
    // A second wrapper for the same holder must not become a second producer.
    for (const auto& [other_handle, item] : windows_)
        if (other_handle != handle.Value() && item.active && item.surface_identity == window.surface_identity &&
            item.cpu_buffer)
            return kInvalidValue;

    const auto width = window.width;
    const auto height = window.height;
    const auto format = window.format;
    if (format != 1 && format != 2 && format != 4) return kInvalidValue;
    const auto pixel_count = std::uint64_t{width} * height;
    if (pixel_count == 0 || pixel_count > kCpuBufferLimit / 4) return kInvalidValue;
    const auto length = pixel_count * (format == 4 ? 2 : 4);
    auto& space = services_.address_space;
    const auto out_write = space.PreflightWrite({output.Address(), 44}, thread);
    const auto dirty_write =
        dirty.IsNull() ? std::nullopt : std::optional{space.PreflightWrite({dirty.Address(), 16}, thread)};

    std::unique_ptr<GuestBuffer> replacement;
    auto* buffer = window.cpu_buffer.get();
    if (!buffer || window.buffer_width != width || window.buffer_height != height || window.buffer_format != format) {
        const auto page = space.PageSize();
        replacement = std::make_unique<GuestBuffer>(space, (length + page - 1) / page * page);
        buffer = replacement.get();
    }
    // API 19 ARMv7: four int32 fields, guest bits pointer, six reserved words.
    space.WritePrevalidated(out_write, EncodeWords(std::array<std::uint32_t, 11>{width, height, width, format,
                                                                                 buffer->range.Start().Value()}));
    // Always request a full redraw. The stable mapping is reused at identical
    // geometry, but there is no BufferQueue slot history or copy-back contract.
    if (dirty_write)
        space.WritePrevalidated(*dirty_write, EncodeWords(std::array<std::uint32_t, 4>{0, 0, width, height}));
    if (replacement) window.cpu_buffer = std::move(replacement);
    window.buffer_width = width;
    window.buffer_height = height;
    window.buffer_format = format;
    window.cpu_locked = true;
    return 0;
}

BoundaryResult AndroidModule::ANativeWindow_unlockAndPost(const A32CallFrame& call) {
    const auto handle = call.Pointer<void>(0).Address();
    std::scoped_lock publish(surface_publish_mutex_);
    std::unique_lock lock(mutex_);
    auto& window = Window(handle.Value());
    if (!window.cpu_locked) return kInvalidOperation;
    window.cpu_locked = false;
    const auto identity = window.surface_identity;
    auto frame = std::make_shared<video::VideoFrame>();
    frame->width = window.buffer_width;
    frame->height = window.buffer_height;
    const auto pixel_count = static_cast<std::size_t>(frame->width) * frame->height;
    frame->rgba8.resize(pixel_count * 4);
    const auto base = window.cpu_buffer->range.Start();
    if (window.buffer_format == 4) {
        std::vector<std::byte> rgb565(pixel_count * 2);
        services_.address_space.Read(base, rgb565, call.ThreadId());
        for (std::size_t i = 0; i < pixel_count; ++i) {
            const auto value =
                std::to_integer<unsigned>(rgb565[i * 2]) | (std::to_integer<unsigned>(rgb565[i * 2 + 1]) << 8);
            const auto r = value >> 11;
            const auto g = (value >> 5) & 63U;
            const auto b = value & 31U;
            frame->rgba8[i * 4] = static_cast<std::uint8_t>((r << 3) | (r >> 2));
            frame->rgba8[i * 4 + 1] = static_cast<std::uint8_t>((g << 2) | (g >> 4));
            frame->rgba8[i * 4 + 2] = static_cast<std::uint8_t>((b << 3) | (b >> 2));
            frame->rgba8[i * 4 + 3] = 255;
        }
    } else {
        services_.address_space.Read(base, std::as_writable_bytes(std::span(frame->rgba8)), call.ThreadId());
        if (window.buffer_format == 2)
            for (std::size_t i = 3; i < frame->rgba8.size(); i += 4)
                frame->rgba8[i] = 255;
    }
    lock.unlock();
    surface_hooks_.publish(surface_hooks_.owner, identity, std::move(frame));
    return 0;
}

void AndroidModule::NotifySurfaceBufferQueued(memory::GuestAddress window, const AndroidSurfaceWindow& expected) {
    std::scoped_lock publish(surface_publish_mutex_);
    const auto geometry = NativeWindowGeometry(window);
    if (geometry && geometry->identity == expected.identity && geometry->generation == expected.generation &&
        surface_hooks_.buffer_queued)
        surface_hooks_.buffer_queued(surface_hooks_.owner, expected.identity);
}

bool AndroidModule::PublishSurfacePixels(memory::GuestAddress window, const AndroidSurfaceWindow& expected,
                                         std::shared_ptr<video::VideoFrame> pixels) {
    std::scoped_lock publish(surface_publish_mutex_);
    const auto geometry = NativeWindowGeometry(window);
    if (!geometry || geometry->identity != expected.identity || geometry->generation != expected.generation ||
        !surface_hooks_.publish)
        return false;
    surface_hooks_.publish(surface_hooks_.owner, expected.identity, std::move(pixels));
    return true;
}

BoundaryResult AndroidModule::ANativeWindow_setBuffersGeometry(const A32CallFrame& call) {
    const auto args = call.RegisterArguments();
    std::scoped_lock lock(mutex_);
    if (!managed_activity_seen_ && !windows_.contains(args[0])) return 0;  // legacy standalone runner
    auto& item = Window(args[0]);
    // The managed window's dimensions remain fixed. CPU producers can select
    // the packing of a buffer at those dimensions or reset it to RGBA8888.
    const auto format = args[3] == 0 ? 1U : args[3];
    const bool same_size = (args[1] == 0 && args[2] == 0) || (args[1] == item.width && args[2] == item.height);
    if (!item.active || !same_size || (format != 1 && format != 2 && format != 4)) return kInvalidValue;
    item.format = format;
    return 0;
}

BoundaryResult AndroidModule::ANativeWindow_getWidth(const A32CallFrame& call) {
    std::scoped_lock lock(mutex_);
    return Window(call.RegisterArguments()[0]).width;
}

BoundaryResult AndroidModule::ANativeWindow_getHeight(const A32CallFrame& call) {
    std::scoped_lock lock(mutex_);
    return Window(call.RegisterArguments()[0]).height;
}

BoundaryResult AndroidModule::ANativeWindow_getFormat(const A32CallFrame& call) {
    std::scoped_lock lock(mutex_);
    return Window(call.RegisterArguments()[0]).format;
}

BoundaryResult AndroidModule::ANativeWindow_acquire(const A32CallFrame& call) {
    std::scoped_lock lock(mutex_);
    auto& item = Window(call.RegisterArguments()[0]);
    if (item.references == UINT32_MAX) throw std::overflow_error("ANativeWindow reference overflow");
    ++item.references;
    return 0;
}

BoundaryResult AndroidModule::ANativeWindow_release(const A32CallFrame& call) {
    std::scoped_lock lock(mutex_);
    const auto handle = call.RegisterArguments()[0];
    auto& item = Window(handle);
    if (item.references <= (item.owned ? 1U : 0U)) throw std::invalid_argument("unbalanced ANativeWindow_release");
    if (--item.references == 0) windows_.erase(handle);
    return 0;
}

BoundaryResult AndroidModule::ANativeActivity_finish(const A32CallFrame& call) {
    return PostNativeActivityCommand(call, {NativeActivityCommandType::finish});
}

BoundaryResult AndroidModule::ANativeActivity_setWindowFormat(const A32CallFrame& call) {
    return PostNativeActivityCommand(call, {NativeActivityCommandType::set_window_format, call.Argument(1)});
}

BoundaryResult AndroidModule::ANativeActivity_setWindowFlags(const A32CallFrame& call) {
    const auto add = call.Argument(1);
    return PostNativeActivityCommand(call, {NativeActivityCommandType::set_window_flags, add, add | call.Argument(2)});
}

BoundaryResult AndroidModule::ANativeActivity_showSoftInput(const A32CallFrame& call) {
    return PostNativeActivityCommand(call, {NativeActivityCommandType::show_ime, call.Argument(1)});
}

BoundaryResult AndroidModule::ANativeActivity_hideSoftInput(const A32CallFrame& call) {
    return PostNativeActivityCommand(call, {NativeActivityCommandType::hide_ime, call.Argument(1)});
}

}  // namespace ogplay::runtime
