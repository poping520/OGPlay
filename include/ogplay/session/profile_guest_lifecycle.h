#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <vector>

#include "ogplay/hal/clock.h"
#include "ogplay/runtime/integration/android_boundary_hle.h"
#include "ogplay/runtime/jni/jni_class_registry.h"
#include "ogplay/runtime/jni/jni_environment.h"
#include "ogplay/session/lifecycle.h"
#include "ogplay/session/profile_native_execution.h"

namespace ogplay::session {

struct ProfileGuestLifecycleBindings final {
    memory::GuestAddress environment;
    runtime::JniEnvironment* jni_environment{};
    runtime::JniClassRegistry* classes{};
    ProfileNativeFrameExecutor execute;
    std::function<void()> open_surface;
    std::function<void()> present_surface;
    std::function<void()> finalize_guest;
    std::function<void()> close_surface;
    std::function<void(const runtime::AndroidBoundaryInput&)> push_boundary_input;
};

class ProfileGuestLifecycleError final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class ProfileGuestLifecycle final {
public:
    [[nodiscard]] static std::unique_ptr<ProfileGuestLifecycle> Create(
        const TitleProfile& profile,
        std::span<const ProfileNativeCallTarget> targets,
        ProfileGuestLifecycleBindings bindings,
        std::uint64_t ticks_per_frame = 1'000,
        std::uint64_t ticks_per_second = 60'000);
    ~ProfileGuestLifecycle();
    ProfileGuestLifecycle(const ProfileGuestLifecycle&) = delete;
    ProfileGuestLifecycle& operator=(const ProfileGuestLifecycle&) = delete;

    [[nodiscard]] LifecycleFrameState Start();
    [[nodiscard]] LifecycleFrameState StepFrame();
    void QueueInput(const runtime::AndroidBoundaryInput& input);
    [[nodiscard]] LifecycleFrameState Stop();
    [[nodiscard]] LifecycleFrameState State() const;

private:
    ProfileGuestLifecycle(
        const TitleProfile& profile,
        std::span<const ProfileNativeCallTarget> targets,
        ProfileGuestLifecycleBindings bindings,
        std::uint64_t ticks_per_frame,
        std::uint64_t ticks_per_second);

    void ExecutePhase(
        ProfileNativeCallPhase phase,
        const std::optional<ProfileNativeInputArguments>& input = std::nullopt);

    const TitleProfile* profile_{};
    std::vector<ProfileNativeCallTarget> targets_;
    ProfileGuestLifecycleBindings bindings_;
    std::vector<ProfileNativeClassReference> class_references_;
    std::vector<runtime::AndroidBoundaryInput> pending_input_;
    hal::FixedStepClock clock_;
    LifecycleRunState state_{LifecycleRunState::ready};
    std::uint64_t frame_{};
    bool surface_open_{};
    bool startup_completed_{};
    bool guest_finalized_{};
};

}  // namespace ogplay::session
