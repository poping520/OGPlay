// VideoView handlers: real decoded playback through the injected
// VideoPlayer factory (ADR-0021). The guest video pump publishes frames and
// fires onCompletion from the guest thread; without a decoder the honest
// fallback (warn + immediate completion) is preserved.

#include <algorithm>

#include "ogplay/runtime/vfs/vfs.h"
#include "ogplay/video/rgba_canvas.h"

#include "dexvm_android_internal.h"

namespace ogplay::runtime {
namespace android_intrinsics {
namespace {

[[nodiscard]] DexVmAndroidContext::VideoViewState* VideoStateOf(
    const Context& context, const std::uint64_t handle) {
    const auto found = context->video_views.find(handle);
    return found == context->video_views.end() ? nullptr : &found->second;
}

[[nodiscard]] std::int64_t PositionOf(
    const DexVmAndroidContext::VideoViewState& state,
    const std::int64_t uptime_ms) {
    if (!state.playing) return state.base_position_ms;
    const auto elapsed = std::max<std::int64_t>(
        uptime_ms - state.start_uptime_ms, 0);
    return std::min(state.base_position_ms + elapsed, state.duration_ms);
}

// Fires the registered onCompletion listener; the MediaPlayer argument is a
// fresh intrinsic instance, matching device behaviour where VideoView owns
// its internal player. Returns a rendered message on guest exceptions.
[[nodiscard]] std::optional<std::string> InvokeCompletionListener(
    dx::Interpreter& vm, DexVmAndroidContext& context,
    const std::uint64_t handle) {
    const auto found = context.video_completion.find(handle);
    if (found == context.video_completion.end() ||
        !found->second.IsValid()) {
        return std::nullopt;
    }
    auto& linker = vm.Linker();
    const auto listener_class = vm.Model().ObjectClass(found->second);
    const auto index = linker.FindVtableIndex(
        listener_class, "onCompletion", "(Landroid/media/MediaPlayer;)V");
    if (!index.has_value()) {
        return "completion listener has no onCompletion method";
    }
    const auto player =
        vm.NewIntrinsicInstance("Landroid/media/MediaPlayer;");
    const auto outcome = vm.Call(
        linker.Class(listener_class).vtable[*index],
        std::vector<dx::VmValue>{dx::VmValue::Ref(found->second),
                                 dx::VmValue::Ref(player)});
    if (outcome.exception.IsValid()) {
        return "onCompletion raised: " + outcome.exception_message;
    }
    return std::nullopt;
}

}  // namespace

void RegisterVideoViews(dx::IntrinsicRegistry& registry,
                        const Context& context) {
    registry.Register("android.videoview.set_path",
                      [context](dx::IntrinsicContext& call) {
        const auto handle = call.receiver.Value();
        context->video_views.erase(handle);
        const auto path_ref = call.arguments[0].ref;
        if (!path_ref.IsValid()) {
            throw dx::VmJavaThrow{"Ljava/lang/NullPointerException;",
                                  "setVideoPath path is null"};
        }
        const auto guest_path = call.vm.StringUtf8(path_ref);
        if (!context->video_player_factory) {
            GuestLog(call, core::LogLevel::warn,
                     "VideoView.setVideoPath: no video decoder is available; "
                     "playback of " + guest_path +
                         " will complete immediately (recorded gap)");
            return dx::VmValue::Void();
        }
        if (context->vfs == nullptr) {
            GuestLog(call, core::LogLevel::warn,
                     "VideoView.setVideoPath: no guest filesystem is bound; "
                     "playback of " + guest_path + " will complete "
                     "immediately (recorded gap)");
            return dx::VmValue::Void();
        }
        std::optional<std::filesystem::path> host_path;
        try {
            host_path = context->vfs->HostPathFor(guest_path);
        } catch (const VfsError& error) {
            GuestLog(call, core::LogLevel::warn,
                     "VideoView.setVideoPath: " + guest_path +
                         " is not resolvable: " + error.what());
            return dx::VmValue::Void();
        }
        if (!host_path.has_value()) {
            GuestLog(call, core::LogLevel::warn,
                     "VideoView.setVideoPath: " + guest_path +
                         " is not a host-backed file; playback will "
                         "complete immediately (recorded gap)");
            return dx::VmValue::Void();
        }
        try {
            DexVmAndroidContext::VideoViewState state;
            state.player = context->video_player_factory(*host_path);
            state.guest_path = guest_path;
            state.duration_ms = state.player->Metadata().duration_ms;
            context->video_views[handle] = std::move(state);
        } catch (const video::VideoPlayerError& error) {
            GuestLog(call, core::LogLevel::warn,
                     "VideoView.setVideoPath: cannot open " + guest_path +
                         ": " + error.what() +
                         "; playback will complete immediately");
            return dx::VmValue::Void();
        }
        GuestLog(call, core::LogLevel::info,
                 "VideoView.setVideoPath: decoding " + guest_path);
        return dx::VmValue::Void();
    });
    registry.Register("android.videoview.start",
                      [context](dx::IntrinsicContext& call) {
        const auto handle = call.receiver.Value();
        auto* state = VideoStateOf(context, handle);
        if (state == nullptr || state->player == nullptr) {
            // Honest fallback: no decoded playback exists, so completion is
            // reported right away through the registered listener and
            // splash-video activities advance as after a real playback.
            GuestLog(call, core::LogLevel::warn,
                     "VideoView.start: no decoded playback; reporting "
                     "immediate completion");
            const auto error =
                InvokeCompletionListener(call.vm, *context, handle);
            if (error.has_value()) {
                throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;",
                                      *error};
            }
            return dx::VmValue::Void();
        }
        if (state->completed) {
            state->player->SeekTo(0);
            state->base_position_ms = 0;
            state->completed = false;
        }
        state->playing = true;
        state->start_uptime_ms = context->uptime_millis.load();
        return dx::VmValue::Void();
    });
    registry.Register("android.videoview.pause",
                      [context](dx::IntrinsicContext& call) {
        auto* state = VideoStateOf(context, call.receiver.Value());
        if (state != nullptr && state->playing) {
            state->base_position_ms =
                PositionOf(*state, context->uptime_millis.load());
            state->playing = false;
        }
        return dx::VmValue::Void();
    });
    registry.Register("android.videoview.seek_to",
                      [context](dx::IntrinsicContext& call) {
        auto* state = VideoStateOf(context, call.receiver.Value());
        if (state == nullptr || state->player == nullptr) {
            return dx::VmValue::Void();
        }
        const auto requested = static_cast<std::int64_t>(
            call.arguments[0].AsInt());
        const auto position = std::clamp<std::int64_t>(
            requested, 0, state->duration_ms);
        state->player->SeekTo(position);
        state->base_position_ms = position;
        state->start_uptime_ms = context->uptime_millis.load();
        state->completed = false;
        return dx::VmValue::Void();
    });
    registry.Register("android.videoview.stop_playback",
                      [context](dx::IntrinsicContext& call) {
        context->video_views.erase(call.receiver.Value());
        return dx::VmValue::Void();
    });
    registry.Register("android.videoview.get_duration",
                      [context](dx::IntrinsicContext& call) {
        const auto* state = VideoStateOf(context, call.receiver.Value());
        return dx::VmValue::Int(state == nullptr
                                    ? 0
                                    : static_cast<std::int32_t>(
                                          state->duration_ms));
    });
    registry.Register("android.videoview.get_current_position",
                      [context](dx::IntrinsicContext& call) {
        const auto* state = VideoStateOf(context, call.receiver.Value());
        if (state == nullptr) return dx::VmValue::Int(0);
        return dx::VmValue::Int(static_cast<std::int32_t>(
            PositionOf(*state, context->uptime_millis.load())));
    });
    registry.Register("android.videoview.set_completion",
                      [context](dx::IntrinsicContext& call) {
        context->video_completion[call.receiver.Value()] =
            call.arguments[0].ref;
        return dx::VmValue::Void();
    });
}

}  // namespace android_intrinsics

std::optional<std::string> PumpVideoViews(
    dexvm::Interpreter& vm, DexVmAndroidContext& context,
    const std::function<void(std::vector<std::uint8_t> rgba8)>& publish) {
    // Handle list first: onCompletion may mutate video_views (stopPlayback,
    // replay), which must not invalidate the iteration.
    std::vector<std::uint64_t> handles;
    handles.reserve(context.video_views.size());
    for (const auto& [handle, state] : context.video_views) {
        if (state.player != nullptr && state.playing) {
            handles.push_back(handle);
        }
    }
    const auto uptime = context.uptime_millis.load();
    for (const auto handle : handles) {
        const auto found = context.video_views.find(handle);
        if (found == context.video_views.end()) continue;
        auto& state = found->second;
        if (state.player == nullptr || !state.playing) continue;
        const auto position = android_intrinsics::PositionOf(state, uptime);
        try {
            auto frame = state.player->TakeFrame(position);
            if (frame.has_value() && publish) {
                publish(video::ComposeRgbaOnCanvas(
                    *frame, context.surface_width, context.surface_height));
            }
        } catch (const video::VideoPlayerError& error) {
            return "video decode failed for " + state.guest_path + ": " +
                   error.what();
        }
        if (position >= state.duration_ms && !state.completed) {
            state.playing = false;
            state.base_position_ms = state.duration_ms;
            state.completed = true;
            const auto error = android_intrinsics::InvokeCompletionListener(
                vm, context, handle);
            if (error.has_value()) return error;
        }
    }
    return std::nullopt;
}

}  // namespace ogplay::runtime
