#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <stdexcept>
#include <vector>

#include "ogplay/runtime/jni.h"

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
    std::vector<JniChar> chars;
    std::vector<std::uint8_t> modified_utf8;
    bool is_copy{true};
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

class JniStringStore final {
public:
    JniStringStore();
    ~JniStringStore();
    JniStringStore(const JniStringStore&) = delete;
    JniStringStore& operator=(const JniStringStore&) = delete;
    JniStringStore(JniStringStore&&) noexcept;
    JniStringStore& operator=(JniStringStore&&) noexcept;

    [[nodiscard]] JniObjectIdentity Create(std::span<const JniChar> utf16);
    [[nodiscard]] JniObjectIdentity CreateModifiedUtf8(
        std::span<const std::uint8_t> encoded);
    void Delete(JniObjectIdentity string);

    [[nodiscard]] JniSize Length(JniObjectIdentity string) const;
    [[nodiscard]] JniSize ModifiedUtf8Length(JniObjectIdentity string) const;
    [[nodiscard]] std::vector<JniChar> Region(JniObjectIdentity string,
                                               JniSize start,
                                               JniSize length) const;
    [[nodiscard]] std::vector<std::uint8_t> ModifiedUtf8Region(
        JniObjectIdentity string, JniSize start, JniSize length) const;

    [[nodiscard]] JniStringAccess Acquire(JniObjectIdentity string,
                                          JniStringAccessKind kind);
    void Release(JniObjectIdentity string, std::uint64_t token,
                 JniStringAccessKind kind);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace ogplay::runtime
