// SurfaceTexture native boundary; release debug bitmap data is from AOSP
// frameworks/native/libs/gui/GLConsumer.cpp, Android 4.4.4, Copyright 2010
// The Android Open Source Project, licensed under Apache-2.0.
#include "catalog.h"
#include "ogplay/runtime/integration/android_guest_call_session.h"
#include "ogplay/gles/angle_frame.h"

#include <bit>
#include <limits>

namespace ogplay::runtime {
namespace {
namespace dx = dexvm;
using State = DexVmAndroidContext::SurfaceTextureState;
std::uint32_t Token(DexVmAndroidContext& context) {
    if (context.next_surface_token >= static_cast<std::uint32_t>(INT32_MAX))
        throw dx::VmJavaThrow{"Landroid/view/Surface$OutOfResourcesException;", "surface token budget exhausted"};
    return context.next_surface_token++;
}
auto& Field(dx::Interpreter& vm, dx::VmObjectRef object, const char* name) {
    const auto field = vm.Linker().FindFieldRecursive(vm.Model().ObjectClass(object), name, "I");
    if (!field) throw std::logic_error("surface native field missing");
    return vm.Model().InstanceSlots(object)[vm.Linker().Field(*field).slot];
}
State& Texture(dx::IntrinsicContext& call, const android_intrinsics::Context& context, bool require_live = true) {
    const auto found = context->surface_textures.find(call.receiver.Value());
    if (found == context->surface_textures.end())
        throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;", "SurfaceTexture has been finalized"};
    if (require_live && !found->second.stream->Live())
        throw dx::VmJavaThrow{"Ljava/lang/RuntimeException;", "SurfaceTexture is abandoned"};
    return found->second;
}
gles::ExternalTextureContext Graphics(dx::IntrinsicContext& call, const android_intrinsics::Context& context,
                                        const gles::ExternalTextureCall& operation) {
    try {
        if (context->surface_texture_graphics) return context->surface_texture_graphics(operation);
        if (!context->session) throw gles::ExternalTextureError(gles::ExternalTextureError::Reason::no_context, "no graphics session");
        return context->session->Process().UseExternalTexture(operation);
    } catch (const gles::ExternalTextureError& error) {
        if (error.reason == gles::ExternalTextureError::Reason::unsupported) {
            if (auto* ledger = call.vm.Ledger()) ledger->RecordUnimplemented("dexvm.surface_texture.external_backend", 0);
            throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;", error.what()};
        }
        throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;", error.what()};
    } catch (const gles::GlesApiError& error) {
        throw dx::VmJavaThrow{"Ljava/lang/RuntimeException;", error.what()};
    }
}
void CheckAttached(const State& state) {
    if (!state.attached) throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;", "SurfaceTexture is detached"};
}
SurfaceStream::Snapshot ReleasedImage() {
    // AOSP GLConsumer::getDebugTexImageBuffer, immutable released-slot image.
    static constexpr char pattern[] =
        "___________________________________XX_XX_______X_X_____X_X____X_XXXXXXX_X____XXXXXXXXXXX__"
        "___XX_XXX_XX_______XXXXXXX_________X___X_________X_____X__________________________________";
    static_assert(sizeof(pattern) == 181);
    SurfaceStreamFrame debug; debug.width = 15; debug.height = 12; debug.rgba.resize(15 * 12 * 4);
    for (std::size_t i = 0; i < 15 * 12; ++i) {
        const auto color = static_cast<std::uint8_t>(pattern[i] == 'X' ? 0 : 255);
        for (std::size_t c = 0; c < 3; ++c) debug.rgba[i * 4 + c] = color;
        debug.rgba[i * 4 + 3] = 255;
    }
    return {1, 0, std::make_shared<const SurfaceStreamFrame>(std::move(debug))};
}
void Bind(dx::IntrinsicContext& call, const android_intrinsics::Context& context, State& state,
          const SurfaceStream::Snapshot& image) {
    gles::ExternalTextureCall operation;
    operation.expected = state.binding; operation.texture = state.texture;
    operation.operation = image.frame ? gles::ExternalTextureCall::Operation::upload : gles::ExternalTextureCall::Operation::bind;
    if (image.frame) {
        operation.width = image.frame->width; operation.height = image.frame->height; operation.rgba = image.frame->rgba;
    }
    const auto binding = Graphics(call, context, operation);
    state.binding = binding;
}
void ReleaseSurface(DexVmAndroidContext& context, const dx::VmObjectRef surface, const std::uint32_t token) {
    const auto found = context.stream_surface_owners.find(surface.Value());
    if (found == context.stream_surface_owners.end() || found->second != token)
        throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;", "invalid Surface endpoint"};
    context.stream_surfaces.erase(token);
    context.managed_surface_tokens.erase(token);
    context.stream_surface_owners.erase(found);
}
}

void RegisterAndroidSurfaceStreamStateTable(dx::Interpreter& vm, const std::shared_ptr<DexVmAndroidContext>& context) {
    if (!context) return;
    vm.RegisterIntrinsicStateTable({"android.surface-stream",
        [context](dx::VmObjectRef owner, const dx::VmRootVisitor& visit) {
            if (const auto found = context->surface_textures.find(owner.Value()); found != context->surface_textures.end())
                if (found->second.weak_self.IsValid()) visit(found->second.weak_self);
        },
        [context](dx::VmObjectRef owner) {
            if (const auto found = context->surface_textures.find(owner.Value()); found != context->surface_textures.end()) {
                found->second.stream->Abandon(); context->surface_textures.erase(found);
            }
            if (const auto found = context->stream_surface_owners.find(owner.Value()); found != context->stream_surface_owners.end())
                ReleaseSurface(*context, owner, found->second);
        }, {}});
}
void ReleaseAndroidSurfaceStreams(DexVmAndroidContext& context) {
    for (auto& [_, state] : context.surface_textures) state.stream->Abandon();
    context.surface_textures.clear(); context.stream_surfaces.clear();
    context.stream_surface_owners.clear(); context.managed_surface_tokens.clear();
}
void DispatchAndroidSurfaceStreamEvents(dx::Interpreter& vm, DexVmAndroidContext& context) {
    std::vector<dx::VmObjectRef> events;
    for (auto& [_, state] : context.surface_textures)
        if (state.stream->TakeNotification()) events.push_back(state.weak_self);
    if (events.empty()) return;
    const auto roots = vm.ProtectReferences(events);
    const auto owner = vm.Linker().ResolveDescriptor("Landroid/graphics/SurfaceTexture;");
    const auto method = vm.Linker().FindDirectMethod(owner, "postEventFromNative", "(Ljava/lang/Object;)V");
    if (!method) throw std::logic_error("SurfaceTexture event method missing");
    for (auto event : events) {
        const auto result = vm.Call(*method, std::array{dx::VmValue::Ref(event)});
        if (result.exception.IsValid()) throw dx::VmJavaThrow{vm.Linker().Class(result.exception_class).descriptor, result.exception_message, result.exception};
    }
}
void InitializeManagedJavaSurface(dx::Interpreter& vm, DexVmAndroidContext& context, dx::VmObjectRef surface, std::uint32_t holder) {
    if (context.stream_surface_owners.size() >= 256 || context.process_stopping.load())
        throw dx::VmJavaThrow{"Landroid/view/Surface$OutOfResourcesException;", "Surface endpoint budget exhausted"};
    const auto type = vm.Linker().ResolveDescriptor("Landroid/view/Surface;");
    const auto constructor = vm.Linker().FindDirectMethod(type, "<init>", "()V");
    if (!constructor) throw std::logic_error("Surface constructor missing");
    const auto result = vm.Call(*constructor, std::array{dx::VmValue::Ref(surface)});
    if (result.exception.IsValid()) throw dx::VmJavaThrow{vm.Linker().Class(result.exception_class).descriptor, result.exception_message, result.exception};
    const auto token = Token(context);
    context.managed_surface_tokens.emplace(token, holder);
    context.stream_surface_owners.emplace(surface.Value(), token);
    const auto setter = vm.Linker().FindDirectMethod(type, "setNativeObjectLocked", "(I)V");
    if (!setter) throw std::logic_error("Surface ownership setter missing");
    const auto attached = vm.Call(*setter, std::array{dx::VmValue::Ref(surface), dx::VmValue::Int(static_cast<std::int32_t>(token))});
    if (attached.exception.IsValid()) throw dx::VmJavaThrow{vm.Linker().Class(attached.exception_class).descriptor, attached.exception_message, attached.exception};
}
std::shared_ptr<SurfaceStream> ResolveAndroidSurfaceStream(dx::Interpreter& vm, DexVmAndroidContext& context, dx::VmObjectRef surface) {
    if (!surface.IsValid()) return {};
    const auto token = Field(vm, surface, "mNativeObject").bits;
    const auto owner = context.stream_surface_owners.find(surface.Value());
    if (owner == context.stream_surface_owners.end() || owner->second != token) return {};
    const auto found = context.stream_surfaces.find(token);
    if (found == context.stream_surfaces.end() || !found->second->Live()) return {};
    return found->second;
}

namespace android_intrinsics {
Decl Declare_android_graphics_SurfaceTexture(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/graphics/SurfaceTexture;", "Ljava/lang/Object;");
    builder.StaticMethod("nativeClassInit", "()V", [](dx::IntrinsicContext& call) {
        const auto type = call.vm.Linker().ResolveDescriptor("Landroid/graphics/SurfaceTexture;");
        for (const auto* name : {"mSurfaceTexture", "mBufferQueue", "mFrameAvailableListener"})
            if (!call.vm.Linker().FindFieldRecursive(type, name, "I")) throw std::logic_error("SurfaceTexture metadata mismatch");
        if (!call.vm.Linker().FindDirectMethod(type, "postEventFromNative", "(Ljava/lang/Object;)V"))
            throw std::logic_error("SurfaceTexture callback metadata mismatch");
        return dx::VmValue::Void();
    }, dx::kAccPrivate | dx::kAccStatic | dx::kAccNative);
    constexpr auto native = dx::kAccPrivate | dx::kAccNative;
    builder.DirectMethod("nativeInit", "(IZLjava/lang/Object;)V", [context](dx::IntrinsicContext& call) {
        if (context->surface_textures.size() >= 64 || context->process_stopping.load())
            throw dx::VmJavaThrow{"Landroid/view/Surface$OutOfResourcesException;", "SurfaceTexture resource budget exhausted"};
        if (context->surface_textures.contains(call.receiver.Value()))
            throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;", "SurfaceTexture already initialized"};
        State state;
        state.stream = std::make_shared<SurfaceStream>(call.arguments[1].AsInt() != 0, context->surface_stream_budget);
        state.weak_self = call.arguments[2].ref; state.token = Token(*context);
        state.texture = static_cast<std::uint32_t>(call.arguments[0].AsInt());
        context->surface_textures.emplace(call.receiver.Value(), state);
        for (const auto* name : {"mSurfaceTexture", "mBufferQueue", "mFrameAvailableListener"})
            Field(call.vm, call.receiver, name) = {state.token, dx::SlotTag::cat1};
        return dx::VmValue::Void();
    }, native);
    builder.DirectMethod("nativeFinalize", "()V", [context](dx::IntrinsicContext& call) {
        if (const auto found = context->surface_textures.find(call.receiver.Value()); found != context->surface_textures.end()) {
            found->second.stream->Abandon(); context->surface_textures.erase(found);
        }
        for (const auto* name : {"mSurfaceTexture", "mBufferQueue", "mFrameAvailableListener"}) Field(call.vm, call.receiver, name) = {0, dx::SlotTag::cat1};
        return dx::VmValue::Void();
    }, native);
    builder.DirectMethod("nativeRelease", "()V", [context](dx::IntrinsicContext& call) {
        const auto found = context->surface_textures.find(call.receiver.Value());
        if (found != context->surface_textures.end()) found->second.stream->Abandon();
        return dx::VmValue::Void();
    }, native);
    builder.DirectMethod("nativeSetDefaultBufferSize", "(II)V", [context](dx::IntrinsicContext& call) {
        auto& state = Texture(call, context);
        state.stream->SetDefaultSize(static_cast<std::uint32_t>(call.arguments[0].AsInt()), static_cast<std::uint32_t>(call.arguments[1].AsInt()));
        return dx::VmValue::Void();
    }, native);
    builder.DirectMethod("nativeUpdateTexImage", "()V", [context](dx::IntrinsicContext& call) {
        auto& state = Texture(call, context); CheckAttached(state);
        const auto pending = state.stream->Acquire();
        try { Bind(call, context, state, pending); }
        catch (...) { state.stream->Abort(pending); throw; }
        if (pending.frame) {
            state.stream->Commit(pending);
            state.released_image = false;
            state.timestamp_ns = pending.frame->timestamp_ns;
            state.transform = pending.frame->transform;
        }
        return dx::VmValue::Void();
    }, native);
    builder.DirectMethod("nativeGetTimestamp", "()J", [context](dx::IntrinsicContext& call) {
        return dx::VmValue::Long(Texture(call, context, false).timestamp_ns);
    }, native);
    builder.DirectMethod("nativeGetTransformMatrix", "([F)V", [context](dx::IntrinsicContext& call) {
        const auto matrix = Texture(call, context, false).transform;
        for (std::int32_t i = 0; i < 16; ++i)
            call.vm.Model().SetPrimitiveElement(call.arguments[0].ref, i, std::bit_cast<std::uint32_t>(matrix[static_cast<std::size_t>(i)]));
        return dx::VmValue::Void();
    }, native);
    builder.DirectMethod("nativeDetachFromGLContext", "()I", [context](dx::IntrinsicContext& call) {
        auto& state = Texture(call, context);
        if (!state.attached) return dx::VmValue::Int(-38);
        try {
            Graphics(call, context, {gles::ExternalTextureCall::Operation::detach, state.binding, state.texture, 0, 0, {}});
        } catch (const dx::VmJavaThrow& error) {
            // Before the first GL access AOSP also permits detaching without a context.
            if (error.descriptor != "Ljava/lang/IllegalStateException;") throw;
            if (state.binding.context) return dx::VmValue::Int(-38);
        }
        state.attached = false; state.binding = {}; return dx::VmValue::Int(0);
    }, native);
    builder.DirectMethod("nativeAttachToGLContext", "(I)I", [context](dx::IntrinsicContext& call) {
        auto& state = Texture(call, context);
        if (state.attached) return dx::VmValue::Int(-38);
        const auto old_texture = state.texture;
        state.texture = static_cast<std::uint32_t>(call.arguments[0].AsInt());
        try { Bind(call, context, state, state.released_image ? ReleasedImage() : state.stream->Current()); }
        catch (const dx::VmJavaThrow& error) {
            state.texture = old_texture;
            if (error.descriptor == "Ljava/lang/IllegalStateException;") return dx::VmValue::Int(-38);
            throw;
        }
        state.attached = true; return dx::VmValue::Int(0);
    }, native);
    builder.DirectMethod("nativeReleaseTexImage", "()V", [context](dx::IntrinsicContext& call) {
        auto& state = Texture(call, context);
        if (state.attached) {
            if (!state.binding.context) throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;", "SurfaceTexture has no associated EGL context"};
            Graphics(call, context, {gles::ExternalTextureCall::Operation::query, state.binding, 0, 0, 0, {}});
        }
        if (state.stream->Current().frame) {
            if (state.attached) Bind(call, context, state, ReleasedImage());
            state.released_image = true;
        }
        state.stream->ReleaseImage(); state.timestamp_ns = 0; return dx::VmValue::Void();
    }, native);
    builder.DirectMethod("nativeGetQueuedCount", "()I", [](dx::IntrinsicContext& call) -> dx::VmValue {
        if (auto* ledger = call.vm.Ledger()) ledger->RecordUnimplemented("dexvm.surface_texture.queued_count", 0);
        throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;", "API19 legacy nativeGetQueuedCount is not registered by AOSP"};
    }, native);
    return std::move(builder).Build();
}

Decl Declare_android_view_Surface(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/view/Surface;", "Ljava/lang/Object;");
    constexpr auto native_static = dx::kAccPrivate | dx::kAccStatic | dx::kAccNative;
    builder.StaticMethod("nativeCreateFromSurfaceTexture", "(Landroid/view/Surface;Landroid/graphics/SurfaceTexture;)I", [context](dx::IntrinsicContext& call) {
        if (!call.arguments[0].ref.IsValid())
            throw dx::VmJavaThrow{"Ljava/lang/NullPointerException;", "Surface owner is null"};
        if (context->stream_surface_owners.contains(call.arguments[0].ref.Value()))
            throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;", "Surface producer already initialized"};
        const auto found = context->surface_textures.find(call.arguments[1].ref.Value());
        if (found == context->surface_textures.end() || !found->second.stream->Live())
            throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "SurfaceTexture is not a live consumer"};
        if (context->stream_surface_owners.size() >= 256)
            throw dx::VmJavaThrow{"Landroid/view/Surface$OutOfResourcesException;", "Surface endpoint budget exhausted"};
        const auto token = Token(*context);
        context->stream_surfaces.emplace(token, found->second.stream);
        context->stream_surface_owners.emplace(call.arguments[0].ref.Value(), token);
        return dx::VmValue::Int(static_cast<std::int32_t>(token));
    }, native_static);
    builder.StaticMethod("nativeRelease", "(Landroid/view/Surface;I)V", [context](dx::IntrinsicContext& call) {
        ReleaseSurface(*context, call.arguments[0].ref, static_cast<std::uint32_t>(call.arguments[1].AsInt()));
        return dx::VmValue::Void();
    }, native_static);
    builder.StaticMethod("nativeIsValid", "(Landroid/view/Surface;I)Z", [context](dx::IntrinsicContext& call) {
        const auto owner = context->stream_surface_owners.find(call.arguments[0].ref.Value());
        const auto token = static_cast<std::uint32_t>(call.arguments[1].AsInt());
        if (owner == context->stream_surface_owners.end() || owner->second != token) return dx::VmValue::Int(0);
        if (const auto stream = context->stream_surfaces.find(token); stream != context->stream_surfaces.end())
            return dx::VmValue::Int(stream->second->Live());
        const auto managed = context->managed_surface_tokens.find(token);
        return dx::VmValue::Int(managed != context->managed_surface_tokens.end() &&
            (context->active_surface_holders.contains(managed->second) || context->destroying_surface_holders.contains(managed->second)));
    }, native_static);
    return std::move(builder).Build();
}
} // namespace android_intrinsics
} // namespace ogplay::runtime
