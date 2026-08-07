#include "ogplay/session/profile_guest_lifecycle.h"

#include <cmath>
#include <cstdint>
#include <exception>
#include <limits>
#include <optional>
#include <set>
#include <string>
#include <utility>

namespace ogplay::session {
namespace {

constexpr std::uint64_t kProfileObjectIdentityBase =
    UINT64_C(0x50524f4600000000);
constexpr std::uint64_t kRootThreadId = 1;

[[nodiscard]] ProfileNativeCallPhase InputPhase(
    const runtime::AndroidBoundaryInput& input) {
    switch (input.type) {
    case runtime::AndroidBoundaryInputType::key:
        return input.pressed ? ProfileNativeCallPhase::key_down
                             : ProfileNativeCallPhase::key_up;
    case runtime::AndroidBoundaryInputType::pointer_motion:
        return ProfileNativeCallPhase::pointer_move;
    case runtime::AndroidBoundaryInputType::pointer_button:
        return input.pressed ? ProfileNativeCallPhase::pointer_down
                             : ProfileNativeCallPhase::pointer_up;
    }
    throw ProfileGuestLifecycleError("profile guest input type is invalid");
}

[[nodiscard]] ProfileNativeInputArguments InputArguments(
    const runtime::AndroidBoundaryInput& input,
    const ProfileSurface surface) {
    if (input.code < 0 || !std::isfinite(input.x) ||
        !std::isfinite(input.y) || input.x < 0.0F || input.y < 0.0F ||
        input.x >= static_cast<float>(surface.width) ||
        input.y >= static_cast<float>(surface.height)) {
        throw ProfileGuestLifecycleError(
            "profile guest input is outside its validated surface");
    }
    return {
        static_cast<std::uint32_t>(input.x),
        static_cast<std::uint32_t>(input.y),
        static_cast<std::uint32_t>(input.code),
        static_cast<std::uint32_t>(input.code),
    };
}

}  // namespace

std::unique_ptr<ProfileGuestLifecycle> ProfileGuestLifecycle::Create(
    const TitleProfile& profile,
    const std::span<const ProfileNativeCallTarget> targets,
    ProfileGuestLifecycleBindings bindings,
    const std::uint64_t ticks_per_frame,
    const std::uint64_t ticks_per_second) {
    try {
        return std::unique_ptr<ProfileGuestLifecycle>(
            new ProfileGuestLifecycle(
                profile, targets, std::move(bindings),
                ticks_per_frame, ticks_per_second));
    } catch (const ProfileGuestLifecycleError&) {
        throw;
    } catch (const std::exception& error) {
        throw ProfileGuestLifecycleError(
            "profile guest lifecycle creation failed: " +
            std::string(error.what()));
    }
}

ProfileGuestLifecycle::ProfileGuestLifecycle(
    const TitleProfile& profile,
    const std::span<const ProfileNativeCallTarget> targets,
    ProfileGuestLifecycleBindings bindings,
    const std::uint64_t ticks_per_frame,
    const std::uint64_t ticks_per_second)
    : profile_(&profile), targets_(targets.begin(), targets.end()),
      bindings_(std::move(bindings)),
      clock_(ticks_per_frame, ticks_per_second) {
    if (profile.runtime.lifecycle != ProfileLifecycle::gl_surface_view ||
        profile.runtime.native_calls.empty() ||
        bindings_.environment.IsNull() ||
        bindings_.jni_environment == nullptr || !bindings_.execute ||
        !bindings_.open_surface || !bindings_.present_surface ||
        !bindings_.close_surface || !bindings_.push_boundary_input) {
        throw ProfileGuestLifecycleError(
            "profile guest lifecycle request is incomplete or unsupported");
    }
    std::set<std::string, std::less<>> classes;
    std::uint64_t identity = kProfileObjectIdentityBase;
    for (const auto& call : profile.runtime.native_calls) {
        if (!classes.insert(call.class_name).second) continue;
        class_references_.push_back({
            call.class_name,
            bindings_.jni_environment->PublishGlobalObjectForHle(
                kRootThreadId,
                {runtime::JniObjectDomain::host, identity++}),
            bindings_.jni_environment->PublishGlobalObjectForHle(
                kRootThreadId,
                {runtime::JniObjectDomain::host, identity++}),
        });
    }
    const ProfileNativeInvocationContext context{
        bindings_.environment, profile.runtime.surface,
        ProfileNativeInputArguments{}};
    static_cast<void>(BuildProfileNativeInvocations(
        profile.runtime.native_calls, targets_, ProfileNativeCallPhase::startup,
        class_references_, context));
}

ProfileGuestLifecycle::~ProfileGuestLifecycle() {
    if (state_ != LifecycleRunState::running &&
        state_ != LifecycleRunState::failed) {
        return;
    }
    try {
        static_cast<void>(Stop());
    } catch (const std::exception&) {
    }
}

LifecycleFrameState ProfileGuestLifecycle::Start() {
    if (state_ != LifecycleRunState::ready) {
        throw ProfileGuestLifecycleError(
            "profile guest lifecycle cannot start in its current state");
    }
    try {
        bindings_.open_surface();
        surface_open_ = true;
        ExecutePhase(ProfileNativeCallPhase::startup);
        startup_completed_ = true;
        ExecutePhase(ProfileNativeCallPhase::resume);
        state_ = LifecycleRunState::running;
    } catch (...) {
        state_ = LifecycleRunState::failed;
        throw;
    }
    return State();
}

LifecycleFrameState ProfileGuestLifecycle::StepFrame() {
    if (state_ != LifecycleRunState::running) {
        throw ProfileGuestLifecycleError(
            "profile guest lifecycle cannot step in its current state");
    }
    if (frame_ == std::numeric_limits<std::uint64_t>::max()) {
        throw ProfileGuestLifecycleError(
            "profile guest lifecycle frame counter overflow");
    }
    try {
        auto input = std::move(pending_input_);
        pending_input_.clear();
        for (const auto& event : input) {
            bindings_.push_boundary_input(event);
            ExecutePhase(InputPhase(event),
                         InputArguments(event, profile_->runtime.surface));
        }
        ExecutePhase(ProfileNativeCallPhase::frame);
        bindings_.present_surface();
        clock_.AdvanceFrames(1);
        ++frame_;
    } catch (...) {
        state_ = LifecycleRunState::failed;
        throw;
    }
    return State();
}

void ProfileGuestLifecycle::QueueInput(
    const runtime::AndroidBoundaryInput& input) {
    if (state_ != LifecycleRunState::running) {
        throw ProfileGuestLifecycleError(
            "profile guest lifecycle cannot queue input in its current state");
    }
    static_cast<void>(InputPhase(input));
    static_cast<void>(InputArguments(input, profile_->runtime.surface));
    pending_input_.push_back(input);
}

LifecycleFrameState ProfileGuestLifecycle::Stop() {
    if (state_ != LifecycleRunState::running &&
        state_ != LifecycleRunState::failed) {
        throw ProfileGuestLifecycleError(
            "profile guest lifecycle cannot stop in its current state");
    }
    std::exception_ptr failure;
    if (startup_completed_) {
        try {
            ExecutePhase(ProfileNativeCallPhase::pause);
            ExecutePhase(ProfileNativeCallPhase::shutdown);
        } catch (...) {
            failure = std::current_exception();
        }
    }
    if (surface_open_) {
        try {
            bindings_.close_surface();
            surface_open_ = false;
        } catch (...) {
            if (!failure) failure = std::current_exception();
        }
    }
    if (failure) {
        state_ = LifecycleRunState::failed;
        std::rethrow_exception(failure);
    }
    state_ = LifecycleRunState::stopped;
    return State();
}

LifecycleFrameState ProfileGuestLifecycle::State() const {
    return {state_, frame_, clock_.Ticks()};
}

void ProfileGuestLifecycle::ExecutePhase(
    const ProfileNativeCallPhase phase,
    const std::optional<ProfileNativeInputArguments>& input) {
    const auto invocations = BuildProfileNativeInvocations(
        profile_->runtime.native_calls, targets_, phase,
        class_references_,
        {bindings_.environment, profile_->runtime.surface, input});
    static_cast<void>(
        ExecuteProfileNativeInvocations(invocations, bindings_.execute));
}

}  // namespace ogplay::session
