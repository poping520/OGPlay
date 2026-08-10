#include "ogplay/runtime/integration/android_guest_call_session.h"

#include <cstddef>
#include <utility>

#include "ogplay/runtime/jni/jni_invocation.h"
#include "ogplay/runtime/jni/jni_object.h"

namespace ogplay::runtime {
namespace {

constexpr std::size_t kMaximumMovieNameCodeUnits = 4096;

}  // namespace

void AndroidGuestMovieState::Request(
    const std::uint64_t thread_id, const std::span<const JniChar> name) {
    if (thread_id == 0 || name.size() > kMaximumMovieNameCodeUnits) {
        throw AndroidGuestCallSessionError(
            "Android guest movie request is invalid");
    }
    std::scoped_lock lock(mutex_);
    ++request_count_;
    latest_ = AndroidGuestMovieRequest{
        request_count_, thread_id, {name.begin(), name.end()}};
}

std::optional<AndroidGuestMovieRequest>
AndroidGuestMovieState::Latest() const {
    std::scoped_lock lock(mutex_);
    return latest_;
}

std::uint64_t AndroidGuestMovieState::RequestCount() const {
    std::scoped_lock lock(mutex_);
    return request_count_;
}

void BindAndroidGuestJavaMovieHandlers(
    JniInvocationEngine& invocations, JniEnvironment& environment,
    JniStringStore& strings, AndroidGuestMovieState& movie_state) {
    invocations.RegisterHandler(
        "audio.load_movie",
        [&environment, &strings, &movie_state](
            const JniInvocation& invocation) {
            const auto reference =
                std::get<JniReference>(invocation.arguments[0]);
            const auto identity = environment.ResolveObjectForHle(
                invocation.thread_id, reference);
            if (!identity.has_value()) {
                throw AndroidGuestCallSessionError(
                    "Android guest movie name cannot be null");
            }
            const auto length = strings.Length(*identity);
            const auto name = strings.Region(*identity, 0, length);
            movie_state.Request(invocation.thread_id, name);
            return JniValue{std::monostate{}};
        });
}

}  // namespace ogplay::runtime
