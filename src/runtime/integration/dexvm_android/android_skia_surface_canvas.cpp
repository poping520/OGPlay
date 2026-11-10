#include "android_skia.h"
#include "ogplay/runtime/integration/android_guest_call_session.h"

#include <array>

namespace ogplay::runtime::android_intrinsics {
namespace {
using namespace skia;
using V = dx::VmValue;
using Canvas = raster::Canvas;

std::uint32_t ActiveHolder(dx::IntrinsicContext& c, DexVmAndroidContext& context) {
    const auto owner = c.arguments[0].ref;
    const auto token = static_cast<std::uint32_t>(c.arguments[1].AsInt());
    const auto endpoint = context.stream_surface_owners.find(owner.Value());
    const auto holder = context.managed_surface_tokens.find(token);
    if (endpoint == context.stream_surface_owners.end() || endpoint->second != token)
        throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;", "invalid Surface endpoint"};
    if (holder == context.managed_surface_tokens.end()) {
        if (auto* ledger = c.vm.Ledger()) ledger->RecordUnimplemented("dexvm.surface_canvas", 0);
        throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;", "Surface has no managed software producer"};
    }
    if (!context.active_surface_holders.contains(holder->second))
        throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;", "SurfaceHolder is not active"};
    return holder->second;
}

void PaceCanvasPost(dx::IntrinsicContext& call, DexVmAndroidContext& context, std::uint32_t holder) {
    if (context.frame_period_ns == 0 || std::this_thread::get_id() == context.clock_driver) return;
    auto& pacer = context.canvas_pacers.try_emplace(holder, context.frame_period_ns).first->second;
    const auto wait = pacer.Reserve(hal::Clock::SteadyTimestampNs());
    if (wait == 0) return;
    auto& execution_lock = call.vm.ExecutionLock();
    const auto depth = execution_lock.ReleaseForBlocking();
    try {
        hal::SleepPreciseNs(wait);
    } catch (...) {
        execution_lock.ReacquireAfterBlocking(depth);
        throw;
    }
    execution_lock.ReacquireAfterBlocking(depth);
}
}  // namespace

void ResetManagedCanvas(DexVmAndroidContext& context, std::uint32_t holder) {
    const auto found = context.holder_canvases.find(holder);
    if (found == context.holder_canvases.end()) return;
    context.skia_objects->canvases.Retire(NativeToken<Canvas>(found->second.locked_token));
    context.holder_canvases.erase(found);
    context.canvas_pacers.erase(holder);
}

void AddManagedCanvasNatives(dx::IntrinsicClassBuilder& builder, const Context& context) {
    builder.StaticMethod(
        "nativeLockCanvas", "(Landroid/view/Surface;ILandroid/graphics/Rect;)Landroid/graphics/Canvas;",
        [context](dx::IntrinsicContext& c) {
            const auto holder = ActiveHolder(c, *context);
            const auto existing = context->holder_canvases.find(holder);
            if (existing != context->holder_canvases.end() && existing->second.locked_token)
                throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;", "Surface canvas is already locked"};
            const auto size = ManagedSurfaceBufferSize(c.vm, *context, dx::VmObjectRef(holder));
            const auto format = context->surface_active_formats.find(holder);
            auto& canvases = context->skia_objects->canvases;
            raster::Token<Canvas> token;
            try {
                token = canvases.Create(size.width, size.height,
                                        format == context->surface_active_formats.end() ? 1 : format->second);
            } catch (const std::invalid_argument& error) {
                if (auto* ledger = c.vm.Ledger()) ledger->RecordUnimplemented("dexvm.surface_canvas_format", 0);
                throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;", error.what()};
            } catch (const std::bad_alloc&) {
                throw dx::VmJavaThrow{"Landroid/view/Surface$OutOfResourcesException;", "software Surface buffer"};
            }
            try {
                const auto canvas = c.vm.NewIntrinsicInstance("Landroid/graphics/Canvas;");
                const auto root = c.vm.ProtectReferences(std::array{canvas});
                const auto ctor = c.vm.Linker().FindDirectMethod(c.vm.Model().ObjectClass(canvas), "<init>", "(I)V");
                const auto created = c.vm.Call(*ctor, std::array{V::Ref(canvas), JavaToken(token)});
                if (created.exception.IsValid()) throw dx::VmJavaThrow{"", "", created.exception};
                // The current protocol redraws the full producer buffer on every lock.
                if (c.arguments[2].ref.IsValid())
                    WriteRect(c.vm, c.arguments[2].ref, SkIRect::MakeWH(size.width, size.height));
                context->holder_canvases.insert_or_assign(
                    holder, DexVmAndroidContext::HolderCanvas{canvas, JavaToken(token).AsInt()});
                return V::Ref(canvas);
            } catch (...) {
                canvases.Retire(token);
                throw;
            }
        },
        kStaticNative);
    builder.StaticMethod(
        "nativeUnlockCanvasAndPost", "(Landroid/view/Surface;ILandroid/graphics/Canvas;)V",
        [context](dx::IntrinsicContext& c) {
            const auto holder = ActiveHolder(c, *context);
            const auto found = context->holder_canvases.find(holder);
            if (found == context->holder_canvases.end() || !found->second.locked_token ||
                c.arguments[2].ref != found->second.object)
                throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "Canvas is not locked by this Surface"};
            if (!context->session)
                throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;", "SurfaceHolder has no guest session"};
            auto& canvases = context->skia_objects->canvases;
            const auto token = found->second.locked_token;
            auto* canvas = canvases.Find(NativeToken<Canvas>(token));
            if (!canvas || IntField(c.vm, found->second.object, "mNativeCanvas") != token) {
                ResetManagedCanvas(*context, holder);
                throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;",
                                      "locked Surface canvas was replaced or released"};
            }
            while (canvas->SaveCount() > 1)
                canvas->Restore();
            auto rgba = canvas->ReadSurfacePixels();
            const bool window = dx::VmObjectRef(holder) == context->window_surface_holder;
            {
                std::scoped_lock lock(context->surface_pixels_mutex);
                if (!window)
                    context->surface_pixels[holder] = std::make_shared<const video::VideoFrame>(
                        video::VideoFrame{0, static_cast<std::uint32_t>(canvas->Width()),
                                          static_cast<std::uint32_t>(canvas->Height()), std::move(rgba)});
                context->surface_buffer_queued[holder] = true;
            }
            found->second = {};
            canvases.Retire(NativeToken<Canvas>(token));
            if (window)
                context->session->PublishSoftwareFrame(std::move(rgba));
            else
                context->session->PublishBlankFrame(0U);
            PaceCanvasPost(c, *context, holder);
            return V::Void();
        },
        kStaticNative);
}

}  // namespace ogplay::runtime::android_intrinsics
