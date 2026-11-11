#pragma once

#include "catalog.h"
#include "window_system.h"

namespace ogplay::runtime {

// VM-owned service identities are accessed only under the VM execution lock.
// WindowSystem itself is safe for concurrent buffer producers and host input.
struct RomWindowState {
    explicit RomWindowState(int width, int height) : system(width, height) {}

    android_intrinsics::WindowSystem system;

    struct Client {
        android_intrinsics::WindowId window;
        dexvm::VmObjectRef binder{}, attrs{};
        std::int32_t surface{}, channel{};
        bool drawn{};
        android_intrinsics::WindowAttributes attributes{};
    };

    struct Surface {
        android_intrinsics::WindowId window;
        std::uint32_t refs{1};
        std::int32_t canvas_token{};
        std::int32_t previous_canvas_token{};
        int format{1}, canvas_format{};
        int buffer_width{}, buffer_height{};
        dexvm::VmObjectRef locked_canvas{};
        bool locked{}, valid{true};
        std::shared_ptr<SurfaceStream> stream{};
    };

    std::map<std::uint32_t, Client> clients;
    std::map<std::int32_t, Surface> surfaces;
    std::map<std::int32_t, std::int32_t> controls;
    std::map<std::int32_t, std::vector<std::int32_t>> surface_sessions;
    std::map<std::uint64_t, std::vector<android_intrinsics::SurfaceUpdate>> transactions;
    std::map<std::uint64_t, int> transaction_depth;
    std::int32_t next_handle{1};
    dexvm::VmObjectRef session;
};

namespace android_intrinsics {
RomWindowState& RomWindows(const Context& context);
WindowAttributes ReadWindowAttributes(dx::Interpreter& vm, const Context& context, dx::VmObjectRef attrs);
std::vector<WindowRect> ReadWindowRegion(dx::Interpreter& vm, const Context& context, dx::VmObjectRef region);
void WriteWindowRect(dx::Interpreter& vm, dx::VmObjectRef object, WindowRect rect);
dx::Slot& WindowField(dx::Interpreter& vm, dx::VmObjectRef object, const char* name, const char* type);
void AddWindowManagerMethods(dx::IntrinsicClassBuilder& builder, const Context& context);
std::optional<AndroidSurfaceWindow> ResolveRomSurfaceWindow(dx::Interpreter& vm, const Context& context,
                                                            dx::VmObjectRef surface);
void AddRomSurfaceNatives(dx::IntrinsicClassBuilder& builder, const Context& context);
void ReleaseRomSurfaceReference(const Context& context, int token);
void SetRomApplicationVisibility(dx::Interpreter& vm, const Context& context, dx::VmObjectRef token, bool visible);
}  // namespace android_intrinsics
}  // namespace ogplay::runtime
