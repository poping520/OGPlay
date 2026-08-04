#include "ogplay/runtime/jni/jni_object.h"

#include <atomic>
#include <limits>
#include <map>
#include <mutex>
#include <utility>

#include "ogplay/runtime/jni/jni_utf.h"

namespace ogplay::runtime {
namespace {

std::atomic<std::uint64_t> g_next_host_object{1};

[[noreturn]] void Fail(const JniStringErrorReason reason,
                       const char* message) {
    throw JniStringError(reason, message);
}

[[nodiscard]] std::size_t CheckedSize(const std::size_t value) {
    if (value > static_cast<std::size_t>(std::numeric_limits<JniSize>::max())) {
        throw std::length_error("JNI string exceeds jsize range");
    }
    return value;
}

}  // namespace

JniObjectIdentity AllocateJniHostObjectIdentity() {
    auto current = g_next_host_object.load(std::memory_order_relaxed);
    while (current != 0) {
        const auto next = current + 1;
        if (g_next_host_object.compare_exchange_weak(
                current, next, std::memory_order_relaxed)) {
            return {JniObjectDomain::host, current};
        }
    }
    throw std::length_error("JNI host object identity space exhausted");
}

JniStringError::JniStringError(const JniStringErrorReason reason,
                               const char* message)
    : std::runtime_error(message), reason_(reason) {}

JniStringErrorReason JniStringError::Reason() const noexcept { return reason_; }

class JniStringStore::Impl final {
public:
    [[nodiscard]] JniObjectIdentity Create(
        const std::span<const JniChar> utf16) {
        static_cast<void>(CheckedSize(utf16.size()));
        const auto identity = AllocateJniHostObjectIdentity();
        std::scoped_lock lock(mutex_);
        strings_.emplace(identity.value,
                         StringEntry{{utf16.begin(), utf16.end()}, 0});
        return identity;
    }

    void Delete(const JniObjectIdentity string) {
        std::scoped_lock lock(mutex_);
        const auto found = RequireString(string);
        if (found->second.active_accesses != 0) {
            Fail(JniStringErrorReason::access_still_active,
                 "JNI string still has active chars access");
        }
        strings_.erase(found);
    }

    [[nodiscard]] JniSize Length(const JniObjectIdentity string) const {
        std::scoped_lock lock(mutex_);
        return static_cast<JniSize>(CheckedSize(RequireString(string)->second
                                                    .utf16.size()));
    }

    [[nodiscard]] JniSize ModifiedLength(
        const JniObjectIdentity string) const {
        std::scoped_lock lock(mutex_);
        return static_cast<JniSize>(CheckedSize(JniModifiedUtf8Length(
            RequireString(string)->second.utf16)));
    }

    [[nodiscard]] std::vector<JniChar> Region(
        const JniObjectIdentity string, const JniSize start,
        const JniSize length) const {
        std::scoped_lock lock(mutex_);
        const auto& utf16 = RequireString(string)->second.utf16;
        const auto [offset, count] = ValidateRegion(utf16.size(), start, length);
        return {utf16.begin() + static_cast<std::ptrdiff_t>(offset),
                utf16.begin() + static_cast<std::ptrdiff_t>(offset + count)};
    }

    [[nodiscard]] JniStringAccess Acquire(const JniObjectIdentity string,
                                          const JniStringAccessKind kind) {
        std::scoped_lock lock(mutex_);
        auto found = RequireString(string);
        if (next_token_ == 0) {
            throw std::length_error("JNI string access token space exhausted");
        }
        const auto token = next_token_++;
        accesses_.emplace(token, AccessEntry{string.value, kind});
        ++found->second.active_accesses;

        JniStringAccess access{token, kind, {}, {}, true};
        if (kind == JniStringAccessKind::modified_utf8) {
            access.modified_utf8 = EncodeJniModifiedUtf8(found->second.utf16);
        } else {
            access.chars = found->second.utf16;
        }
        return access;
    }

    void Release(const JniObjectIdentity string, const std::uint64_t token,
                 const JniStringAccessKind kind) {
        std::scoped_lock lock(mutex_);
        auto owner = RequireString(string);
        const auto access = accesses_.find(token);
        if (access == accesses_.end() ||
            access->second.string_identity != string.value) {
            Fail(JniStringErrorReason::invalid_access,
                 "JNI string access token is invalid for this object");
        }
        if (access->second.kind != kind) {
            Fail(JniStringErrorReason::access_kind_mismatch,
                 "JNI string access release kind does not match acquire");
        }
        accesses_.erase(access);
        --owner->second.active_accesses;
    }

private:
    struct StringEntry final {
        std::vector<JniChar> utf16;
        std::size_t active_accesses{};
    };

    struct AccessEntry final {
        std::uint64_t string_identity{};
        JniStringAccessKind kind{JniStringAccessKind::chars};
    };

    using StringIterator = std::map<std::uint64_t, StringEntry>::iterator;
    using ConstStringIterator =
        std::map<std::uint64_t, StringEntry>::const_iterator;

    [[nodiscard]] StringIterator RequireString(
        const JniObjectIdentity string) {
        if (string.domain != JniObjectDomain::host) Unknown();
        const auto found = strings_.find(string.value);
        if (found == strings_.end()) Unknown();
        return found;
    }

    [[nodiscard]] ConstStringIterator RequireString(
        const JniObjectIdentity string) const {
        if (string.domain != JniObjectDomain::host) Unknown();
        const auto found = strings_.find(string.value);
        if (found == strings_.end()) Unknown();
        return found;
    }

    [[noreturn]] static void Unknown() {
        Fail(JniStringErrorReason::unknown_object,
             "JNI object is not a string in this store");
    }

    [[nodiscard]] static std::pair<std::size_t, std::size_t> ValidateRegion(
        const std::size_t size, const JniSize start, const JniSize length) {
        if (start < 0 || length < 0) InvalidRegion();
        const auto offset = static_cast<std::size_t>(start);
        const auto count = static_cast<std::size_t>(length);
        if (offset > size || count > size - offset) InvalidRegion();
        return {offset, count};
    }

    [[noreturn]] static void InvalidRegion() {
        Fail(JniStringErrorReason::invalid_region,
             "JNI string region is outside the object");
    }

    mutable std::mutex mutex_;
    std::map<std::uint64_t, StringEntry> strings_;
    std::map<std::uint64_t, AccessEntry> accesses_;
    std::uint64_t next_token_{1};
};

JniStringStore::JniStringStore() : impl_(std::make_unique<Impl>()) {}
JniStringStore::~JniStringStore() = default;
JniStringStore::JniStringStore(JniStringStore&&) noexcept = default;
JniStringStore& JniStringStore::operator=(JniStringStore&&) noexcept = default;

JniObjectIdentity JniStringStore::Create(const std::span<const JniChar> utf16) {
    return impl_->Create(utf16);
}

JniObjectIdentity JniStringStore::CreateModifiedUtf8(
    const std::span<const std::uint8_t> encoded) {
    const auto utf16 = DecodeJniModifiedUtf8(encoded);
    return impl_->Create(utf16);
}

void JniStringStore::Delete(const JniObjectIdentity string) {
    impl_->Delete(string);
}

JniSize JniStringStore::Length(const JniObjectIdentity string) const {
    return impl_->Length(string);
}

JniSize JniStringStore::ModifiedUtf8Length(
    const JniObjectIdentity string) const {
    return impl_->ModifiedLength(string);
}

std::vector<JniChar> JniStringStore::Region(const JniObjectIdentity string,
                                            const JniSize start,
                                            const JniSize length) const {
    return impl_->Region(string, start, length);
}

std::vector<std::uint8_t> JniStringStore::ModifiedUtf8Region(
    const JniObjectIdentity string, const JniSize start,
    const JniSize length) const {
    return EncodeJniModifiedUtf8(impl_->Region(string, start, length));
}

JniStringAccess JniStringStore::Acquire(const JniObjectIdentity string,
                                        const JniStringAccessKind kind) {
    return impl_->Acquire(string, kind);
}

void JniStringStore::Release(const JniObjectIdentity string,
                             const std::uint64_t token,
                             const JniStringAccessKind kind) {
    impl_->Release(string, token, kind);
}

}  // namespace ogplay::runtime
