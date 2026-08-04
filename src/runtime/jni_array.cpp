#include "ogplay/runtime/jni_array.h"

#include <algorithm>
#include <limits>
#include <map>
#include <mutex>
#include <type_traits>
#include <utility>

#include "ogplay/runtime/jni_object.h"

namespace ogplay::runtime {
namespace {

[[noreturn]] void Fail(const JniArrayErrorReason reason, const char* message) {
    throw JniArrayError(reason, message);
}

[[nodiscard]] std::size_t DataSize(const JniPrimitiveArrayData& data) {
    return std::visit([](const auto& values) { return values.size(); }, data);
}

[[nodiscard]] JniPrimitiveKind DataKind(const JniPrimitiveArrayData& data) {
    return static_cast<JniPrimitiveKind>(data.index());
}

[[nodiscard]] JniPrimitiveArrayData ZeroData(const JniPrimitiveKind kind,
                                              const std::size_t size) {
    switch (kind) {
    case JniPrimitiveKind::boolean: return std::vector<JniBoolean>(size);
    case JniPrimitiveKind::byte: return std::vector<JniByte>(size);
    case JniPrimitiveKind::character: return std::vector<JniChar>(size);
    case JniPrimitiveKind::short_integer: return std::vector<JniShort>(size);
    case JniPrimitiveKind::integer: return std::vector<JniInt>(size);
    case JniPrimitiveKind::long_integer: return std::vector<JniLong>(size);
    case JniPrimitiveKind::float_value: return std::vector<JniFloat>(size);
    case JniPrimitiveKind::double_value: return std::vector<JniDouble>(size);
    }
    throw std::logic_error("unknown JNI primitive array kind");
}

[[nodiscard]] std::pair<std::size_t, std::size_t> ValidateRegion(
    const std::size_t size, const JniSize start, const JniSize length) {
    if (start < 0 || length < 0) {
        Fail(JniArrayErrorReason::invalid_region,
             "JNI array region has a negative bound");
    }
    const auto offset = static_cast<std::size_t>(start);
    const auto count = static_cast<std::size_t>(length);
    if (offset > size || count > size - offset) {
        Fail(JniArrayErrorReason::invalid_region,
             "JNI array region is outside the object");
    }
    return {offset, count};
}

}  // namespace

JniArrayError::JniArrayError(const JniArrayErrorReason reason,
                             const char* message)
    : std::runtime_error(message), reason_(reason) {}

JniArrayErrorReason JniArrayError::Reason() const noexcept { return reason_; }

class JniPrimitiveArrayStore::Impl final {
public:
    [[nodiscard]] JniObjectIdentity New(const JniPrimitiveKind kind,
                                        const JniSize length) {
        if (length < 0) {
            Fail(JniArrayErrorReason::invalid_length,
                 "JNI array length cannot be negative");
        }
        const auto identity = AllocateJniHostObjectIdentity();
        std::scoped_lock lock(mutex_);
        arrays_.emplace(identity.value,
                        ArrayEntry{ZeroData(kind,
                                            static_cast<std::size_t>(length)),
                                   0});
        return identity;
    }

    void Delete(const JniObjectIdentity array) {
        std::scoped_lock lock(mutex_);
        const auto found = Require(array);
        if (found->second.active_accesses != 0) {
            Fail(JniArrayErrorReason::access_still_active,
                 "JNI array still has active elements access");
        }
        arrays_.erase(found);
    }

    [[nodiscard]] JniSize Length(const JniObjectIdentity array) const {
        std::scoped_lock lock(mutex_);
        return static_cast<JniSize>(DataSize(Require(array)->second.data));
    }

    [[nodiscard]] JniPrimitiveKind Kind(
        const JniObjectIdentity array) const {
        std::scoped_lock lock(mutex_);
        return DataKind(Require(array)->second.data);
    }

    [[nodiscard]] JniPrimitiveArrayData Region(
        const JniObjectIdentity array, const JniSize start,
        const JniSize length) const {
        std::scoped_lock lock(mutex_);
        const auto& data = Require(array)->second.data;
        const auto [offset, count] = ValidateRegion(DataSize(data), start, length);
        return std::visit(
            [offset, count](const auto& values) -> JniPrimitiveArrayData {
                using Vector = std::decay_t<decltype(values)>;
                return Vector(
                    values.begin() + static_cast<std::ptrdiff_t>(offset),
                    values.begin() +
                        static_cast<std::ptrdiff_t>(offset + count));
            },
            data);
    }

    void SetRegion(const JniObjectIdentity array, const JniSize start,
                   const JniPrimitiveArrayData& source) {
        std::scoped_lock lock(mutex_);
        auto& target = Require(array)->second.data;
        if (target.index() != source.index()) TypeMismatch();
        const auto count = DataSize(source);
        if (count >
            static_cast<std::size_t>(std::numeric_limits<JniSize>::max())) {
            Fail(JniArrayErrorReason::invalid_region,
                 "JNI array region exceeds jsize range");
        }
        const auto [offset, checked_count] = ValidateRegion(
            DataSize(target), start, static_cast<JniSize>(count));
        static_cast<void>(checked_count);
        std::visit(
            [offset](auto& values, const auto& replacement) {
                using Target = std::decay_t<decltype(values)>;
                using Source = std::decay_t<decltype(replacement)>;
                if constexpr (std::is_same_v<Target, Source>) {
                    std::copy(replacement.begin(), replacement.end(),
                              values.begin() +
                                  static_cast<std::ptrdiff_t>(offset));
                }
            },
            target, source);
    }

    [[nodiscard]] JniArrayAccess Acquire(const JniObjectIdentity array,
                                         const JniArrayAccessKind kind) {
        std::scoped_lock lock(mutex_);
        auto found = Require(array);
        if (next_token_ == 0) {
            throw std::length_error("JNI array access token space exhausted");
        }
        const auto token = next_token_++;
        accesses_.emplace(token, AccessEntry{array.value, kind});
        ++found->second.active_accesses;
        return {token, kind, found->second.data, true};
    }

    void Release(const JniObjectIdentity array, const std::uint64_t token,
                 const JniArrayAccessKind kind,
                 const JniPrimitiveArrayData& data,
                 const JniArrayReleaseMode mode) {
        std::scoped_lock lock(mutex_);
        auto owner = Require(array);
        const auto access = accesses_.find(token);
        if (access == accesses_.end() ||
            access->second.array_identity != array.value) {
            Fail(JniArrayErrorReason::invalid_access,
                 "JNI array access token is invalid for this object");
        }
        if (access->second.kind != kind) {
            Fail(JniArrayErrorReason::access_kind_mismatch,
                 "JNI array release kind does not match acquire");
        }
        if (owner->second.data.index() != data.index()) TypeMismatch();
        if (DataSize(owner->second.data) != DataSize(data)) {
            Fail(JniArrayErrorReason::data_size_mismatch,
                 "JNI array release data size changed");
        }
        if (mode != JniArrayReleaseMode::abort) owner->second.data = data;
        if (mode != JniArrayReleaseMode::commit) {
            accesses_.erase(access);
            --owner->second.active_accesses;
        }
    }

private:
    struct ArrayEntry final {
        JniPrimitiveArrayData data;
        std::size_t active_accesses{};
    };

    struct AccessEntry final {
        std::uint64_t array_identity{};
        JniArrayAccessKind kind{JniArrayAccessKind::elements};
    };

    using Iterator = std::map<std::uint64_t, ArrayEntry>::iterator;
    using ConstIterator = std::map<std::uint64_t, ArrayEntry>::const_iterator;

    [[nodiscard]] Iterator Require(const JniObjectIdentity array) {
        if (array.domain != JniObjectDomain::host) Unknown();
        const auto found = arrays_.find(array.value);
        if (found == arrays_.end()) Unknown();
        return found;
    }

    [[nodiscard]] ConstIterator Require(const JniObjectIdentity array) const {
        if (array.domain != JniObjectDomain::host) Unknown();
        const auto found = arrays_.find(array.value);
        if (found == arrays_.end()) Unknown();
        return found;
    }

    [[noreturn]] static void Unknown() {
        Fail(JniArrayErrorReason::unknown_object,
             "JNI object is not a primitive array in this store");
    }

    [[noreturn]] static void TypeMismatch() {
        Fail(JniArrayErrorReason::type_mismatch,
             "JNI primitive array type does not match the operation");
    }

    mutable std::mutex mutex_;
    std::map<std::uint64_t, ArrayEntry> arrays_;
    std::map<std::uint64_t, AccessEntry> accesses_;
    std::uint64_t next_token_{1};
};

JniPrimitiveArrayStore::JniPrimitiveArrayStore()
    : impl_(std::make_unique<Impl>()) {}
JniPrimitiveArrayStore::~JniPrimitiveArrayStore() = default;
JniPrimitiveArrayStore::JniPrimitiveArrayStore(
    JniPrimitiveArrayStore&&) noexcept = default;
JniPrimitiveArrayStore& JniPrimitiveArrayStore::operator=(
    JniPrimitiveArrayStore&&) noexcept = default;

JniObjectIdentity JniPrimitiveArrayStore::New(const JniPrimitiveKind kind,
                                              const JniSize length) {
    return impl_->New(kind, length);
}
void JniPrimitiveArrayStore::Delete(const JniObjectIdentity array) {
    impl_->Delete(array);
}
JniSize JniPrimitiveArrayStore::Length(const JniObjectIdentity array) const {
    return impl_->Length(array);
}
JniPrimitiveKind JniPrimitiveArrayStore::Kind(
    const JniObjectIdentity array) const {
    return impl_->Kind(array);
}
JniPrimitiveArrayData JniPrimitiveArrayStore::Region(
    const JniObjectIdentity array, const JniSize start,
    const JniSize length) const {
    return impl_->Region(array, start, length);
}
void JniPrimitiveArrayStore::SetRegion(
    const JniObjectIdentity array, const JniSize start,
    const JniPrimitiveArrayData& data) {
    impl_->SetRegion(array, start, data);
}
JniArrayAccess JniPrimitiveArrayStore::Acquire(
    const JniObjectIdentity array, const JniArrayAccessKind kind) {
    return impl_->Acquire(array, kind);
}
void JniPrimitiveArrayStore::Release(
    const JniObjectIdentity array, const std::uint64_t token,
    const JniArrayAccessKind kind, const JniPrimitiveArrayData& data,
    const JniArrayReleaseMode mode) {
    impl_->Release(array, token, kind, data, mode);
}

}  // namespace ogplay::runtime
