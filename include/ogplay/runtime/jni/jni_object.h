#pragma once

#include <cstddef>
#include <array>
#include "ogplay/runtime/jni/java_payload.h"
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <stdexcept>
#include <vector>

#include "ogplay/runtime/jni/jni.h"

namespace ogplay::runtime {

[[nodiscard]] JniObjectIdentity AllocateJniHostObjectIdentity();

enum class JniStringAccessKind : std::uint8_t {
    chars,
    modified_utf8,
    critical,
};

struct JniStringAccess final {
    std::uint64_t token{};
    JniStringAccessKind kind{JniStringAccessKind::chars};
    std::vector<std::uint8_t> modified_utf8;
    bool is_copy{true};
    memory::GuestAddress address;
    std::size_t pin_count{};
    bool warn_threshold{};
};

enum class JniStringErrorReason : std::uint8_t {
    unknown_object,
    invalid_region,
    invalid_access,
    access_kind_mismatch,
    access_still_active,
};

class JniStringError final : public std::runtime_error {
public:
    JniStringError(JniStringErrorReason reason, const char* message);
    [[nodiscard]] JniStringErrorReason Reason() const noexcept;

private:
    JniStringErrorReason reason_;
};

struct JniStringAllocationStatistics final {
    std::array<std::uint64_t, 7> allocations{};
    std::uint64_t live_bytes{}, peak_live_bytes{}, allocation_failures{};
    std::uint64_t pin_count{}, pinned_strings{}, pinned_bytes{};
};

class JniStringStore final {
public:
    explicit JniStringStore(JavaPayloadProvider& provider);
    void SetAllocationContext(JavaPayloadAllocationContext context);
    void ConfigurePayloadHeap(std::uint64_t maximum, std::uint64_t growth);
    void EnsureCharged(JniObjectIdentity string);
    [[nodiscard]] JniStringAllocationStatistics AllocationStatistics() const;
    [[nodiscard]] std::size_t PinCount(JniObjectIdentity string) const;
    void SetPinTeardownObserver(std::function<void(std::uint64_t, std::uint64_t)> observer);
    ~JniStringStore();
    JniStringStore(const JniStringStore&) = delete;
    JniStringStore& operator=(const JniStringStore&) = delete;
    JniStringStore(JniStringStore&&) noexcept;
    JniStringStore& operator=(JniStringStore&&) noexcept;

    [[nodiscard]] JniObjectIdentity Create(std::span<const JniChar> utf16, bool include_header = true);
    [[nodiscard]] JniObjectIdentity CreateModifiedUtf8(
        std::span<const std::uint8_t> encoded);
    std::uint64_t Delete(JniObjectIdentity string);
    [[nodiscard]] bool Contains(JniObjectIdentity string) const noexcept;

    void VisitAccessRoots(const std::function<void(JniObjectIdentity)>& visit) const;
    [[nodiscard]] JniSize Length(JniObjectIdentity string) const;
    [[nodiscard]] JniSize ModifiedUtf8Length(JniObjectIdentity string) const;
    [[nodiscard]] std::vector<JniChar> Region(JniObjectIdentity string,
                                               JniSize start,
                                               JniSize length) const;
    [[nodiscard]] std::vector<std::uint8_t> ModifiedUtf8Region(
        JniObjectIdentity string, JniSize start, JniSize length) const;

    [[nodiscard]] JniStringAccess Acquire(JniObjectIdentity string,
                                          JniStringAccessKind kind);
    bool Release(JniObjectIdentity string, std::uint64_t token, JniStringAccessKind kind);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace ogplay::runtime
