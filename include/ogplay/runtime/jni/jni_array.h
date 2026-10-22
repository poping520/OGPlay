#pragma once

#include <cstdint>
#include <array>
#include <functional>
#include <optional>
#include "ogplay/runtime/jni/java_payload.h"
#include <memory>
#include <stdexcept>
#include <variant>
#include <vector>

#include "ogplay/runtime/jni/jni.h"

namespace ogplay::runtime {

enum class JniPrimitiveKind : std::uint8_t {
    boolean,
    byte,
    character,
    short_integer,
    integer,
    long_integer,
    float_value,
    double_value,
};

using JniPrimitiveArrayData =
    std::variant<std::vector<JniBoolean>, std::vector<JniByte>,
                 std::vector<JniChar>, std::vector<JniShort>,
                 std::vector<JniInt>, std::vector<JniLong>,
                 std::vector<JniFloat>, std::vector<JniDouble>>;

using JniPrimitiveArrayView =
    std::variant<JavaPayloadAccessor<JniBoolean>, JavaPayloadAccessor<JniByte>, JavaPayloadAccessor<JniChar>,
                 JavaPayloadAccessor<JniShort>, JavaPayloadAccessor<JniInt>, JavaPayloadAccessor<JniLong>,
                 JavaPayloadAccessor<JniFloat>, JavaPayloadAccessor<JniDouble>>;

struct JniArrayAllocationContext final {
    std::function<std::shared_ptr<void>()> enter;
    // The returned action rolls back an uncommitted charge while enter is held.
    std::function<std::function<void()>(std::uint64_t)> reserve;
    std::function<void(std::uint64_t)> refund;
    std::function<bool()> emergency;
};

struct JniArrayAllocationStatistics final {
    // Length buckets: 0, 1..16, 17..64, 65..256, 257..1024, 1025..8192, >8192.
    std::array<std::array<std::uint64_t, 7>, 8> allocations{};
    std::uint64_t live_bytes{}, peak_live_bytes{}, allocation_failures{};
    std::uint64_t pin_count{}, pinned_arrays{}, pinned_bytes{};
};

enum class JniArrayAccessKind : std::uint8_t { elements, critical };
enum class JniArrayReleaseMode : std::uint8_t {
    copy_back_and_release,
    commit,
    abort,
};

struct JniArrayAccess final {
    memory::GuestAddress address;
    std::size_t pin_count{};
    bool warn_threshold{};
    bool is_copy{false};
};

enum class JniArrayErrorReason : std::uint8_t {
    unknown_object,
    invalid_length,
    invalid_region,
    type_mismatch,
    invalid_access,
    access_kind_mismatch,
    data_size_mismatch,
    access_still_active,
};

class JniArrayError final : public std::runtime_error {
public:
    JniArrayError(JniArrayErrorReason reason, const char* message);
    [[nodiscard]] JniArrayErrorReason Reason() const noexcept;

private:
    JniArrayErrorReason reason_;
};

class JniPrimitiveArrayStore final {
public:
    explicit JniPrimitiveArrayStore(JavaPayloadProvider& provider);
    ~JniPrimitiveArrayStore();
    JniPrimitiveArrayStore(const JniPrimitiveArrayStore&) = delete;
    JniPrimitiveArrayStore& operator=(const JniPrimitiveArrayStore&) = delete;
    JniPrimitiveArrayStore(JniPrimitiveArrayStore&&) noexcept;
    JniPrimitiveArrayStore& operator=(JniPrimitiveArrayStore&&) noexcept;

    // Context changes only while callers are quiescent; callbacks run outside store locks.
    void SetAllocationContext(JniArrayAllocationContext context);
    void ConfigurePayloadHeap(std::uint64_t maximum, std::uint64_t growth);
    void EnsureCharged(JniObjectIdentity array);
    void VisitAccessRoots(const std::function<void(JniObjectIdentity)>& visit) const;
    [[nodiscard]] JniPrimitiveArrayView View(JniObjectIdentity array) const;
    [[nodiscard]] JniArrayAllocationStatistics AllocationStatistics() const;
    void Copy(JniObjectIdentity source, JniSize from, JniObjectIdentity target, JniSize to, JniSize length);
    void CopyWithin(JniObjectIdentity array, JniSize from, JniSize to, JniSize length);
    [[nodiscard]] JniObjectIdentity New(JniPrimitiveKind kind,
                                        JniSize length);
    void Delete(JniObjectIdentity array);
    [[nodiscard]] JniSize Length(JniObjectIdentity array) const;
    [[nodiscard]] JniPrimitiveKind Kind(JniObjectIdentity array) const;
    [[nodiscard]] JniPrimitiveArrayData Region(JniObjectIdentity array,
                                                JniSize start,
                                                JniSize length) const;
    void SetRegion(JniObjectIdentity array, JniSize start,
                   const JniPrimitiveArrayData& data);

    [[nodiscard]] JniArrayAccess Acquire(JniObjectIdentity array,
                                         JniArrayAccessKind kind);
    // Returns false only for an unmatched unpin. COMMIT is a no-op.
    bool Release(JniObjectIdentity array, JniArrayReleaseMode mode);
    [[nodiscard]] std::size_t PinCount(JniObjectIdentity array) const;
    void SetPinTeardownObserver(std::function<void(std::uint64_t, std::uint64_t)> observer);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace ogplay::runtime
