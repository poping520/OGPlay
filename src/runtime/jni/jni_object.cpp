#include "ogplay/runtime/jni/jni_object.h"

#include <atomic>

#include "ogplay/runtime/jni/jni_utf.h"

namespace ogplay::runtime {
namespace {

std::atomic<std::uint64_t> g_next_host_object{1};

}  // namespace

JniObjectIdentity AllocateJniHostObjectIdentity() {
    auto current = g_next_host_object.load(std::memory_order_relaxed);
    while (current != 0) {
        const auto next = current + 1;
        if (g_next_host_object.compare_exchange_weak(current, next, std::memory_order_relaxed)) {
            return {JniObjectDomain::host, current};
        }
    }
    throw std::length_error("JNI host object identity space exhausted");
}

JniStringError::JniStringError(const JniStringErrorReason reason, const char* message)
    : std::runtime_error(message), reason_(reason) {}

JniStringErrorReason JniStringError::Reason() const noexcept {
    return reason_;
}

JniObjectIdentity JniStringBackend::CreateModifiedUtf8(std::span<const std::uint8_t> encoded) {
    return Create(DecodeJniModifiedUtf8(encoded));
}

std::vector<JniChar> JniStringBackend::Region(JniObjectIdentity string, JniSize start, JniSize length) const {
    std::vector<JniChar> result;
    ReadRegion(string, start, length,
               [&result](std::span<const JniChar> chars) { result.assign(chars.begin(), chars.end()); });
    return result;
}

}  // namespace ogplay::runtime
