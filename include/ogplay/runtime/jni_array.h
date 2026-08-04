#pragma once

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <variant>
#include <vector>

#include "ogplay/runtime/jni.h"

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

enum class JniArrayAccessKind : std::uint8_t { elements, critical };
enum class JniArrayReleaseMode : std::uint8_t {
    copy_back_and_release,
    commit,
    abort,
};

struct JniArrayAccess final {
    std::uint64_t token{};
    JniArrayAccessKind kind{JniArrayAccessKind::elements};
    JniPrimitiveArrayData data;
    bool is_copy{true};
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
    JniPrimitiveArrayStore();
    ~JniPrimitiveArrayStore();
    JniPrimitiveArrayStore(const JniPrimitiveArrayStore&) = delete;
    JniPrimitiveArrayStore& operator=(const JniPrimitiveArrayStore&) = delete;
    JniPrimitiveArrayStore(JniPrimitiveArrayStore&&) noexcept;
    JniPrimitiveArrayStore& operator=(JniPrimitiveArrayStore&&) noexcept;

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
    void Release(JniObjectIdentity array, std::uint64_t token,
                 JniArrayAccessKind kind, const JniPrimitiveArrayData& data,
                 JniArrayReleaseMode mode);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace ogplay::runtime
