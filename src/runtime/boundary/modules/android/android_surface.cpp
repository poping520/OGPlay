#include "runtime/boundary/modules/android/android_module.h"
#include <stdexcept>

namespace ogplay::runtime {
void AndroidModule::SetSurfaceHooks(AndroidSurfaceHooks hooks) {
    std::scoped_lock guard(surface_hook_mutex_);
    surface_hooks_ = hooks;
    if (!hooks.from_java) {
        std::scoped_lock lock(mutex_);
        surface_geometries_.clear();
        std::erase_if(windows_, [](const auto& entry) { return !entry.second.surface_identity.IsNull(); });
    }
}

void AndroidModule::RetireSurfaceWindow(memory::GuestAddress identity) {
    // fromSurface holds the hook mutex while acquiring the VM lock. Retirement
    // already owns that VM lock, so serialize pixels with a separate mutex.
    std::scoped_lock guard(surface_publish_mutex_);
    std::scoped_lock lock(mutex_);
    surface_geometries_[identity.Value()] = {identity, 0, 0};
    for (auto it = windows_.begin(); it != windows_.end();) {
        auto& window = it->second;
        if (window.surface_identity == identity && window.owned) {
            window.active = false;
            window.owned = false;
            if (--window.references == 0) {
                it = windows_.erase(it);
                continue;
            }
        }
        ++it;
    }
}

BoundaryResult AndroidModule::SurfaceFromJava(const A32CallFrame& call) {
    std::scoped_lock guard(surface_hook_mutex_);
    if (!surface_hooks_.from_java) throw std::runtime_error("NDK Surface JNI backend is unavailable");
    return surface_hooks_
        .from_java(surface_hooks_.owner, call.ThreadId(), call.Pointer<void>(0).Address(),
                   SurfaceJavaReference{call.Argument(1)})
        .Value();
}

void AndroidModule::UpdateSurfaceWindow(const AndroidSurfaceWindow& surface) {
    std::scoped_lock lock(mutex_);
    surface_geometries_[surface.identity.Value()] = surface;
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
    const auto found = windows_.find(window.Value());
    if (found == windows_.end()) {
        const auto alias = surface_geometries_.find(window.Value());
        if (alias != surface_geometries_.end() && alias->second.width && alias->second.height) return alias->second;
        return std::nullopt;
    }
    if (!found->second.active) return std::nullopt;
    return AndroidSurfaceWindow{found->second.surface_identity, found->second.width, found->second.height};
}

bool AndroidModule::PublishSurfacePixels(memory::GuestAddress window, std::uint32_t width, std::uint32_t height,
                                         std::vector<std::uint8_t> pixels) {
    std::scoped_lock guard(surface_hook_mutex_);
    std::scoped_lock publish(surface_publish_mutex_);
    const auto geometry = NativeWindowGeometry(window);
    if (!geometry || geometry->identity.IsNull() || !surface_hooks_.publish) return false;
    surface_hooks_.publish(surface_hooks_.owner, geometry->identity, width, height, std::move(pixels));
    return true;
}
}  // namespace ogplay::runtime
