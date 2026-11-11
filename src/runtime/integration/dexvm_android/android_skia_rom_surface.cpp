#include "android_skia.h"
#include "rom_window.h"
#include "ogplay/runtime/integration/android_guest_call_session.h"

#include <limits>
#include <cmath>

namespace ogplay::runtime::android_intrinsics {
namespace {
using namespace skia;
using V = dx::VmValue;

[[noreturn]] void UnsupportedSurface(dx::IntrinsicContext& c, const char* signature, const char* message) {
    if (auto* ledger = c.vm.Ledger()) ledger->RecordUnimplemented(signature, 0);
    throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;", message};
}

template <class F>
dx::IntrinsicHandler SurfaceNative(F fn) {
    return [fn](dx::IntrinsicContext& c) -> V {
        try {
            return fn(c);
        } catch (const std::invalid_argument& e) {
            throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;", e.what()};
        } catch (const std::out_of_range& e) {
            throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;", e.what()};
        } catch (const std::bad_alloc&) {
            throw dx::VmJavaThrow{"Landroid/view/Surface$OutOfResourcesException;", "software Surface allocation"};
        }
    };
}

RomWindowState::Surface& Surface(RomWindowState& s, int token, bool live = true) {
    const auto found = s.surfaces.find(token);
    if (found == s.surfaces.end() || (live && !found->second.valid))
        throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;", "invalid ROM Surface native handle"};
    return found->second;
}

std::int32_t Handle(RomWindowState& s) {
    if (s.next_handle == 0 || s.next_handle >= 0x7fffffff)
        throw dx::VmJavaThrow{"Landroid/view/Surface$OutOfResourcesException;", "window handle space exhausted"};
    return s.next_handle++;
}

WindowId ControlWindow(RomWindowState& s, int handle) {
    const auto found = s.controls.find(handle);
    if (found == s.controls.end()) throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;", "invalid SurfaceControl"};
    return Surface(s, found->second).window;
}

void UpdateSurface(dx::IntrinsicContext& c, const Context& context, SurfaceUpdate update) {
    auto& s = RomWindows(context);
    update.window = ControlWindow(s, c.arguments[0].AsInt());
    if (s.transaction_depth[c.vm.CurrentContextToken()] > 0)
        s.transactions[c.vm.CurrentContextToken()].push_back(std::move(update));
    else
        s.system.UpdateSurfaces(std::array{std::move(update)});
}

void Release(const Context& context, int token) {
    auto& s = RomWindows(context);
    const auto found = s.surfaces.find(token);
    if (found == s.surfaces.end()) return;
    if (--found->second.refs == 0) {
        if (found->second.valid && found->second.window.value && s.system.Exists(found->second.window)) {
            if (context->session)
                context->session->Process().RetireSurfaceWindow(memory::GuestAddress{found->second.window.value});
            s.system.Remove(found->second.window);
        }
        if (found->second.canvas_token)
            context->skia_objects->canvases.Retire(NativeToken<raster::Canvas>(found->second.canvas_token));
        s.surfaces.erase(found);
    }
}
}  // namespace

void ReleaseRomSurfaceReference(const Context& context, int token) {
    Release(context, token);
}

std::vector<WindowRect> ReadWindowRegion(dx::Interpreter& vm, const Context& context, dx::VmObjectRef region) {
    std::vector<WindowRect> result;
    if (!region.IsValid()) return result;
    auto& native = Require(context->skia_objects->regions, IntField(vm, region, "mNativeRegion"));
    for (SkRegion::Iterator it(native); !it.done(); it.next()) {
        const auto& r = it.rect();
        result.push_back({r.left(), r.top(), r.right(), r.bottom()});
    }
    return result;
}

std::optional<AndroidSurfaceWindow> ResolveRomSurfaceWindow(dx::Interpreter& vm, const Context& context,
                                                            dx::VmObjectRef surface) {
    if (!surface.IsValid() || !context->rom_windows) return std::nullopt;
    auto& state = *context->rom_windows;
    const auto native = static_cast<int>(WindowField(vm, surface, "mNativeObject", "I").bits);
    const auto found = state.surfaces.find(native);
    if (found == state.surfaces.end() || !found->second.valid || !found->second.window.value) return std::nullopt;
    const auto& data = found->second;
    const auto frame = state.system.Frame(data.window);
    const auto width = data.buffer_width > 0 ? data.buffer_width : frame.Width();
    const auto height = data.buffer_height > 0 ? data.buffer_height : frame.Height();
    if (width <= 0 || height <= 0) return std::nullopt;
    return AndroidSurfaceWindow{memory::GuestAddress{data.window.value}, static_cast<unsigned>(width),
                                static_cast<unsigned>(height)};
}

void AddRomSurfaceNatives(dx::IntrinsicClassBuilder& b, const Context& context) {
    b.StaticMethod("nativeCreateFromSurfaceControl", "(I)I", SurfaceNative([context](dx::IntrinsicContext& c) {
                       auto& s = RomWindows(context);
                       const auto found = s.controls.find(c.arguments[0].AsInt());
                       if (found == s.controls.end())
                           throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "invalid SurfaceControl"};
                       ++Surface(s, found->second).refs;
                       return V::Int(found->second);
                   }),
                   kStaticNative);
    b.StaticMethod("nativeCreateFromSurfaceTexture", "(Landroid/graphics/SurfaceTexture;)I",
                   SurfaceNative([context](dx::IntrinsicContext& c) {
                       auto& s = RomWindows(context);
                       const auto texture = context->surface_textures.find(c.arguments[0].ref.Value());
                       if (texture == context->surface_textures.end() || !texture->second.stream->Live())
                           throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "invalid SurfaceTexture"};
                       const auto id = Handle(s);
                       RomWindowState::Surface surface;
                       surface.stream = texture->second.stream;
                       s.surfaces.emplace(id, std::move(surface));
                       return V::Int(id);
                   }),
                   kStaticNative);
    b.StaticMethod("nativeIsValid", "(I)Z", SurfaceNative([context](dx::IntrinsicContext& c) {
                       auto& s = RomWindows(context);
                       const auto found = s.surfaces.find(c.arguments[0].AsInt());
                       return V::Int(found != s.surfaces.end() && found->second.valid &&
                                     (!found->second.stream || found->second.stream->Live()));
                   }),
                   kStaticNative);
    b.StaticMethod("nativeRelease", "(I)V", SurfaceNative([context](dx::IntrinsicContext& c) {
                       Release(context, c.arguments[0].AsInt());
                       return V::Void();
                   }),
                   kStaticNative);
    b.StaticMethod("nativeIsConsumerRunningBehind", "(I)Z", SurfaceNative([context](dx::IntrinsicContext& c) {
                       Surface(RomWindows(context), c.arguments[0].AsInt());
                       return V::Int(0);
                   }),
                   kStaticNative);
    b.StaticMethod(
        "nativeLockCanvas", "(ILandroid/graphics/Canvas;Landroid/graphics/Rect;)I",
        SurfaceNative([context](dx::IntrinsicContext& c) {
            auto& native = Surface(RomWindows(context), c.arguments[0].AsInt());
            if (native.locked)
                throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "Surface is already locked"};
            const auto canvas = dx::IntrinsicCall(c).NonNullRef(1, "canvas");
            const auto dimensions =
                native.stream
                    ? native.stream->DefaultSize()
                    : std::array{static_cast<unsigned>(native.buffer_width > 0
                                                           ? native.buffer_width
                                                           : RomWindows(context).system.Frame(native.window).Width()),
                                 static_cast<unsigned>(native.buffer_height > 0
                                                           ? native.buffer_height
                                                           : RomWindows(context).system.Frame(native.window).Height())};
            auto& objects = context->skia_objects->canvases;
            auto* raster = objects.Find(NativeToken<raster::Canvas>(native.canvas_token));
            const bool resized = !raster || raster->Width() != static_cast<int>(dimensions[0]) ||
                                 raster->Height() != static_cast<int>(dimensions[1]) ||
                                 native.canvas_format != native.format;
            if (resized) {
                objects.Retire(NativeToken<raster::Canvas>(native.canvas_token));
                // Readback is straight RGBA; WMS applies the window format at composition.
                native.canvas_token = JavaToken(objects.Create(dimensions[0], dimensions[1], native.format)).AsInt();
                native.canvas_format = native.format;
                raster = objects.Find(NativeToken<raster::Canvas>(native.canvas_token));
            }
            while (raster->SaveCount() > 1)
                raster->Restore();
            raster->Matrix().reset();
            auto dirty = SkIRect::MakeWH(static_cast<int>(dimensions[0]), static_cast<int>(dimensions[1]));
            if (!resized && c.arguments[2].ref.IsValid()) {
                const auto requested = ReadRect(c.vm, c.arguments[2].ref);
                if (!dirty.intersect(requested)) dirty.setEmpty();
            }
            if (c.arguments[2].ref.IsValid()) WriteRect(c.vm, c.arguments[2].ref, dirty);
            raster->ClipRect(SkRect::Make(dirty), SkRegion::kReplace_Op);
            native.previous_canvas_token = IntField(c.vm, canvas, "mNativeCanvas");
            WindowField(c.vm, canvas, "mNativeCanvas", "I") = {static_cast<std::uint32_t>(native.canvas_token),
                                                               dx::SlotTag::cat1};
            native.locked_canvas = canvas;
            native.locked = true;
            ++native.refs;
            return c.arguments[0];
        }),
        kStaticNative);
    b.StaticMethod(
        "nativeUnlockCanvasAndPost", "(ILandroid/graphics/Canvas;)V", SurfaceNative([context](dx::IntrinsicContext& c) {
            auto& s = RomWindows(context);
            auto& native = Surface(s, c.arguments[0].AsInt());
            if (!native.locked || native.locked_canvas != c.arguments[1].ref)
                throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "Canvas is not locked by this Surface"};
            auto& canvas = Require(context->skia_objects->canvases, native.canvas_token);
            while (canvas.SaveCount() > 1)
                canvas.Restore();
            auto rgba = canvas.ReadSurfacePixels();
            bool transparent = AndroidPixelFormatHasAlpha(native.format);
            if (transparent)
                for (std::size_t i = 3; i < rgba.size(); i += 4)
                    if (rgba[i] != 0) {
                        transparent = false;
                        break;
                    }
            if (native.stream) {
                const auto lease = native.stream->Connect();
                try {
                    SurfaceStreamFrame frame;
                    frame.width = static_cast<std::uint32_t>(canvas.Width());
                    frame.height = static_cast<std::uint32_t>(canvas.Height());
                    frame.rgba = std::move(rgba);
                    frame.timestamp_ns = context->uptime_millis.load() * 1'000'000;
                    native.stream->Submit(lease, std::move(frame));
                    native.stream->Disconnect(lease);
                } catch (...) {
                    native.stream->Disconnect(lease);
                    throw;
                }
            } else
                s.system.Post(native.window,
                              std::make_shared<const video::VideoFrame>(video::VideoFrame{
                                  context->uptime_millis.load(), static_cast<unsigned>(canvas.Width()),
                                  static_cast<unsigned>(canvas.Height()), std::move(rgba)}),
                              false, canvas.IsOpaque(), transparent);
            WindowField(c.vm, native.locked_canvas, "mNativeCanvas",
                        "I") = {static_cast<std::uint32_t>(native.previous_canvas_token), dx::SlotTag::cat1};
            native.locked_canvas = dx::VmObjectRef{};
            native.locked = false;
            return V::Void();
        }),
        kStaticNative);
}

Decl Declare_android_view_SurfaceSession(const Context& context) {
    auto b = dx::IntrinsicClassBuilder::Class("Landroid/view/SurfaceSession;");
    b.MergesBootClass();
    b.StaticMethod("nativeCreate", "()I", SurfaceNative([context](dx::IntrinsicContext&) {
                       auto& s = RomWindows(context);
                       const auto id = Handle(s);
                       s.surface_sessions.emplace(id, std::vector<std::int32_t>{});
                       return V::Int(id);
                   }),
                   kStaticNative);
    for (bool kill : {false, true})
        b.StaticMethod(kill ? "nativeKill" : "nativeDestroy", "(I)V",
                       SurfaceNative([context, kill](dx::IntrinsicContext& c) {
                           auto& s = RomWindows(context);
                           const auto found = s.surface_sessions.find(c.arguments[0].AsInt());
                           if (found != s.surface_sessions.end()) {
                               for (const auto control : found->second)
                                   if (auto i = s.controls.find(control); kill && i != s.controls.end()) {
                                       auto& surface = Surface(s, i->second, false);
                                       surface.valid = false;
                                       s.system.Remove(surface.window);
                                       Release(context, i->second);
                                       s.controls.erase(i);
                                   }
                               s.surface_sessions.erase(found);
                           }
                           return V::Void();
                       }),
                       kStaticNative);
    return std::move(b).Build();
}

Decl Declare_android_view_SurfaceControl(const Context& context) {
    auto b = dx::IntrinsicClassBuilder::Class("Landroid/view/SurfaceControl;");
    b.MergesBootClass();
    b.StaticMethod("nativeCreate", "(Landroid/view/SurfaceSession;Ljava/lang/String;IIII)I",
                   SurfaceNative([context](dx::IntrinsicContext& c) {
                       auto& s = RomWindows(context);
                       const auto session = IntField(c.vm, c.arguments[0].ref, "mNativeClient");
                       const auto found = s.surface_sessions.find(session);
                       if (found == s.surface_sessions.end())
                           throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "invalid SurfaceSession"};
                       WindowAttributes a;
                       a.flags = 0x200;
                       a.width = c.arguments[2].AsInt();
                       a.height = c.arguments[3].AsInt();
                       a.format = c.arguments[4].AsInt();
                       const auto window = s.system.Add(a, !(c.arguments[5].AsInt() & 4), false);
                       const auto id = Handle(s);
                       RomWindowState::Surface producer{window};
                       producer.format = a.format;
                       s.surfaces.emplace(id, std::move(producer));
                       s.controls.emplace(id, id);
                       found->second.push_back(id);
                       return V::Int(id);
                   }),
                   kStaticNative);
    for (bool destroy : {false, true})
        b.StaticMethod(destroy ? "nativeDestroy" : "nativeRelease", "(I)V",
                       SurfaceNative([context, destroy](dx::IntrinsicContext& c) {
                           auto& s = RomWindows(context);
                           const auto control = s.controls.find(c.arguments[0].AsInt());
                           if (control != s.controls.end()) {
                               auto& surface = Surface(s, control->second, false);
                               if (destroy && surface.valid) {
                                   surface.valid = false;
                                   s.system.Remove(surface.window);
                               }
                               Release(context, control->second);
                               s.controls.erase(control);
                           }
                           return V::Void();
                       }),
                       kStaticNative);
    b.StaticMethod("nativeOpenTransaction", "()V", SurfaceNative([context](dx::IntrinsicContext& c) {
                       ++RomWindows(context).transaction_depth[c.vm.CurrentContextToken()];
                       return V::Void();
                   }),
                   kStaticNative);
    b.StaticMethod("nativeCloseTransaction", "()V", SurfaceNative([context](dx::IntrinsicContext& c) {
                       auto& s = RomWindows(context);
                       auto& depth = s.transaction_depth[c.vm.CurrentContextToken()];
                       if (depth <= 0)
                           throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;", "unbalanced Surface transaction"};
                       if (--depth == 0) {
                           auto updates = std::move(s.transactions[c.vm.CurrentContextToken()]);
                           s.transactions.erase(c.vm.CurrentContextToken());
                           s.system.UpdateSurfaces(updates);
                       }
                       return V::Void();
                   }),
                   kStaticNative);
    b.StaticMethod("nativeSetAnimationTransaction", "()V",
                   SurfaceNative([](dx::IntrinsicContext&) { return V::Void(); }), kStaticNative);
    b.StaticMethod("nativeSetLayer", "(II)V", SurfaceNative([context](dx::IntrinsicContext& c) {
                       SurfaceUpdate u{SurfaceUpdate::Kind::layer, {}};
                       u.a = c.arguments[1].AsInt();
                       UpdateSurface(c, context, std::move(u));
                       return V::Void();
                   }),
                   kStaticNative);
    b.StaticMethod("nativeSetPosition", "(IFF)V", SurfaceNative([context](dx::IntrinsicContext& c) {
                       SurfaceUpdate u{SurfaceUpdate::Kind::position, {}};
                       u.x = c.arguments[1].AsFloat();
                       u.y = c.arguments[2].AsFloat();
                       if (!std::isfinite(u.x) || !std::isfinite(u.y) || std::abs(u.x) > 16384 || std::abs(u.y) > 16384)
                           throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "invalid Surface position"};
                       UpdateSurface(c, context, std::move(u));
                       return V::Void();
                   }),
                   kStaticNative);
    b.StaticMethod("nativeSetSize", "(III)V", SurfaceNative([context](dx::IntrinsicContext& c) {
                       SurfaceUpdate u{SurfaceUpdate::Kind::size, {}};
                       u.a = c.arguments[1].AsInt();
                       u.b = c.arguments[2].AsInt();
                       UpdateSurface(c, context, std::move(u));
                       return V::Void();
                   }),
                   kStaticNative);
    b.StaticMethod("nativeSetAlpha", "(IF)V", SurfaceNative([context](dx::IntrinsicContext& c) {
                       SurfaceUpdate u{SurfaceUpdate::Kind::alpha, {}};
                       u.x = c.arguments[1].AsFloat();
                       UpdateSurface(c, context, std::move(u));
                       return V::Void();
                   }),
                   kStaticNative);
    b.StaticMethod("nativeSetFlags", "(III)V", SurfaceNative([context](dx::IntrinsicContext& c) {
                       const auto flags = c.arguments[1].AsInt(), mask = c.arguments[2].AsInt();
                       if (mask & 1) {
                           SurfaceUpdate u{SurfaceUpdate::Kind::visible, {}};
                           u.a = !(flags & 1);
                           UpdateSurface(c, context, std::move(u));
                       }
                       if (mask & 0x400) {
                           SurfaceUpdate u{SurfaceUpdate::Kind::opaque, {}};
                           u.a = (flags & 0x400) != 0;
                           UpdateSurface(c, context, std::move(u));
                       }
                       return V::Void();
                   }),
                   kStaticNative);
    b.StaticMethod("nativeSetLayerStack", "(II)V", SurfaceNative([](dx::IntrinsicContext& c) {
                       if (c.arguments[1].AsInt() != 0)
                           UnsupportedSurface(c, "android.view.SurfaceControl.nativeSetLayerStack(II)V",
                                              "only the default layer stack exists");
                       return V::Void();
                   }),
                   kStaticNative);
    b.StaticMethod("nativeSetMatrix", "(IFFFF)V", SurfaceNative([](dx::IntrinsicContext& c) {
                       if (c.arguments[1].AsFloat() != 1 || c.arguments[2].AsFloat() != 0 ||
                           c.arguments[3].AsFloat() != 0 || c.arguments[4].AsFloat() != 1)
                           UnsupportedSurface(c, "android.view.SurfaceControl.nativeSetMatrix(IFFFF)V",
                                              "SurfaceControl matrix transform");
                       return V::Void();
                   }),
                   kStaticNative);
    b.StaticMethod("nativeSetTransparentRegionHint", "(ILandroid/graphics/Region;)V",
                   SurfaceNative([context](dx::IntrinsicContext& c) {
                       SurfaceUpdate u{SurfaceUpdate::Kind::transparent, {}};
                       u.region = ReadWindowRegion(c.vm, context, c.arguments[1].ref);
                       UpdateSurface(c, context, std::move(u));
                       return V::Void();
                   }),
                   kStaticNative);
    b.StaticMethod("nativeSetWindowCrop", "(IIIII)V", SurfaceNative([context](dx::IntrinsicContext& c) {
                       SurfaceUpdate u{SurfaceUpdate::Kind::crop, {}};
                       u.rect = {c.arguments[1].AsInt(), c.arguments[2].AsInt(), c.arguments[3].AsInt(),
                                 c.arguments[4].AsInt()};
                       UpdateSurface(c, context, std::move(u));
                       return V::Void();
                   }),
                   kStaticNative);
    return std::move(b).Build();
}
}  // namespace ogplay::runtime::android_intrinsics
