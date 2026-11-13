#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <stdexcept>
#include <vector>

#include "ogplay/runtime/jni/jni.h"
#include "ogplay/runtime/jni/java_payload.h"

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
    access_still_active,
};

class JniStringError final : public std::runtime_error {
public:
    JniStringError(JniStringErrorReason reason, const char* message);
    [[nodiscard]] JniStringErrorReason Reason() const noexcept;

private:
    JniStringErrorReason reason_;
};

// Operations on process-local Java String identities. Lock covers compound
// operations such as creating and publishing a JNI local reference.
class JniStringBackend {
public:
    using RegionReader = std::function<void(std::span<const JniChar>)>;

    virtual ~JniStringBackend() = default;

    [[nodiscard]] virtual std::shared_ptr<void> Lock() const { return {}; }

    [[nodiscard]] virtual JniObjectIdentity Create(std::span<const JniChar> utf16) = 0;
    [[nodiscard]] JniObjectIdentity CreateModifiedUtf8(std::span<const std::uint8_t> encoded);
    [[nodiscard]] virtual bool Contains(JniObjectIdentity string) const = 0;
    [[nodiscard]] virtual JniSize Length(JniObjectIdentity string) const = 0;
    [[nodiscard]] virtual JniSize ModifiedUtf8Length(JniObjectIdentity string) const = 0;
    [[nodiscard]] std::vector<JniChar> Region(JniObjectIdentity string, JniSize start, JniSize length) const;
    // The span can only be read synchronously inside the callback.
    virtual void ReadRegion(JniObjectIdentity string, JniSize start, JniSize length,
                            const RegionReader& read) const = 0;
    [[nodiscard]] virtual std::vector<std::uint8_t> ModifiedUtf8Region(JniObjectIdentity string, JniSize start,
                                                                       JniSize length) const = 0;
    [[nodiscard]] virtual JniStringAccess Acquire(JniObjectIdentity string, JniStringAccessKind kind) = 0;
    virtual bool Release(JniObjectIdentity string, std::uint64_t token, JniStringAccessKind kind) = 0;
};

}  // namespace ogplay::runtime
