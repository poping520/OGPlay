#include "ogplay/runtime/framework/framework_lifecycle.h"

#include <map>
#include <mutex>
#include <utility>

namespace ogplay::runtime {
namespace {

[[noreturn]] void Fail(const FrameworkLifecycleErrorReason reason,
                       std::string message) {
    throw FrameworkLifecycleError(reason, std::move(message));
}

[[nodiscard]] JniValue VoidResult() { return JniValue{std::monostate{}}; }

}  // namespace

FrameworkLifecycleError::FrameworkLifecycleError(
    const FrameworkLifecycleErrorReason reason, std::string message)
    : std::runtime_error(std::move(message)), reason_(reason) {}

FrameworkLifecycleErrorReason FrameworkLifecycleError::Reason() const noexcept {
    return reason_;
}

class FrameworkLifecycleHle::Impl final {
public:
    Impl(JniClassRegistry& classes, JniInvocationEngine& invocations)
        : classes_(&classes), invocations_(&invocations) {}

    [[nodiscard]] FrameworkClassSet Install() {
        if (installed_) {
            Fail(FrameworkLifecycleErrorReason::duplicate_install,
                 "framework lifecycle HLE is already installed");
        }
        const auto object =
            classes_->RegisterClass({"java/lang/Object", {}, {}, {}});
        const auto context = classes_->RegisterClass(
            {"android/content/Context",
             "java/lang/Object",
             {{"getAssets", "()Landroid/content/res/AssetManager;",
               "framework.context.get_assets", false},
              {"getSharedPreferences",
               "(Ljava/lang/String;I)Landroid/content/SharedPreferences;",
               "framework.context.get_shared_preferences", false},
              {"getPackageName", "()Ljava/lang/String;",
               "framework.context.get_package_name", false},
              {"getPackageManager", "()Landroid/content/pm/PackageManager;",
               "framework.context.get_package_manager", false}},
             {}});
        const auto context_wrapper = classes_->RegisterClass(
            {"android/content/ContextWrapper", "android/content/Context", {},
             {}});
        const auto bundle = classes_->RegisterClass(
            {"android/os/Bundle", "java/lang/Object", {}, {}});
        const auto activity = classes_->RegisterClass(
            {"android/app/Activity",
             "android/content/ContextWrapper",
             {{"<init>", "()V", "framework.activity.construct", false},
              {"onCreate", "(Landroid/os/Bundle;)V",
               "framework.activity.create", false},
              {"onStart", "()V", "framework.activity.start", false},
              {"onResume", "()V", "framework.activity.resume", false},
              {"onPause", "()V", "framework.activity.pause", false},
              {"onStop", "()V", "framework.activity.stop", false},
              {"onDestroy", "()V", "framework.activity.destroy", false}},
             {}});

        invocations_->RegisterHandler(
            "framework.activity.construct",
            [this](const JniInvocation& invocation) {
                Construct(invocation);
                return VoidResult();
            });
        BindTransition("framework.activity.create", "onCreate",
                       FrameworkActivityState::constructed,
                       FrameworkActivityState::created);
        BindTransition("framework.activity.start", "onStart",
                       FrameworkActivityState::created,
                       FrameworkActivityState::started);
        BindTransition("framework.activity.resume", "onResume",
                       FrameworkActivityState::started,
                       FrameworkActivityState::resumed);
        BindTransition("framework.activity.pause", "onPause",
                       FrameworkActivityState::resumed,
                       FrameworkActivityState::paused);
        BindTransition("framework.activity.stop", "onStop",
                       FrameworkActivityState::paused,
                       FrameworkActivityState::stopped);
        BindTransition("framework.activity.destroy", "onDestroy",
                       FrameworkActivityState::stopped,
                       FrameworkActivityState::destroyed);
        installed_ = true;
        classes_set_ = {object, context, context_wrapper, bundle, activity};
        return classes_set_;
    }

    [[nodiscard]] FrameworkActivityState State(
        const JniReference receiver) const {
        std::scoped_lock lock(mutex_);
        const auto found = instances_.find(receiver.Value());
        if (found == instances_.end()) Unknown();
        return found->second;
    }

    [[nodiscard]] std::vector<FrameworkLifecycleEvent> Events() const {
        std::scoped_lock lock(mutex_);
        return events_;
    }

private:
    void Construct(const JniInvocation& invocation) {
        std::scoped_lock lock(mutex_);
        if (!instances_
                 .emplace(invocation.receiver.Value(),
                          FrameworkActivityState::constructed)
                 .second) {
            Fail(FrameworkLifecycleErrorReason::duplicate_instance,
                 "Activity constructor was invoked twice for one object");
        }
        Record(invocation, "<init>", FrameworkActivityState::constructed,
               FrameworkActivityState::constructed);
    }

    void BindTransition(const char* implementation, const char* callback,
                        const FrameworkActivityState expected,
                        const FrameworkActivityState next) {
        invocations_->RegisterHandler(
            implementation,
            [this, callback, expected, next](const JniInvocation& invocation) {
                Transition(invocation, callback, expected, next);
                return VoidResult();
            });
    }

    void Transition(const JniInvocation& invocation, const char* callback,
                    const FrameworkActivityState expected,
                    const FrameworkActivityState next) {
        std::scoped_lock lock(mutex_);
        const auto found = instances_.find(invocation.receiver.Value());
        if (found == instances_.end()) Unknown();
        if (found->second != expected) {
            Fail(FrameworkLifecycleErrorReason::invalid_transition,
                 std::string("invalid Activity lifecycle transition: ") +
                     callback);
        }
        const auto before = found->second;
        found->second = next;
        Record(invocation, callback, before, next);
    }

    void Record(const JniInvocation& invocation, std::string callback,
                const FrameworkActivityState before,
                const FrameworkActivityState after) {
        events_.push_back({next_sequence_++, invocation.thread_id,
                           invocation.receiver, std::move(callback), before,
                           after});
    }

    [[noreturn]] static void Unknown() {
        Fail(FrameworkLifecycleErrorReason::unknown_instance,
             "Activity object is not constructed in lifecycle HLE");
    }

    JniClassRegistry* classes_{};
    JniInvocationEngine* invocations_{};
    bool installed_{};
    FrameworkClassSet classes_set_{};
    mutable std::mutex mutex_;
    std::map<std::uint32_t, FrameworkActivityState> instances_;
    std::vector<FrameworkLifecycleEvent> events_;
    std::uint64_t next_sequence_{1};
};

FrameworkLifecycleHle::FrameworkLifecycleHle(
    JniClassRegistry& classes, JniInvocationEngine& invocations)
    : impl_(std::make_unique<Impl>(classes, invocations)) {}
FrameworkLifecycleHle::~FrameworkLifecycleHle() = default;
FrameworkLifecycleHle::FrameworkLifecycleHle(
    FrameworkLifecycleHle&&) noexcept = default;
FrameworkLifecycleHle& FrameworkLifecycleHle::operator=(
    FrameworkLifecycleHle&&) noexcept = default;

FrameworkClassSet FrameworkLifecycleHle::Install() { return impl_->Install(); }
FrameworkActivityState FrameworkLifecycleHle::State(
    const JniReference receiver) const {
    return impl_->State(receiver);
}
std::vector<FrameworkLifecycleEvent> FrameworkLifecycleHle::Events() const {
    return impl_->Events();
}

}  // namespace ogplay::runtime
