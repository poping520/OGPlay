#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>

#include "ogplay/runtime/jni/jni.h"
#include "ogplay/runtime/jni/jni_class_registry.h"

namespace ogplay::runtime {

struct JniObjectValue final {
    JniObjectIdentity object;
    JniObjectIdentity java_class;

    bool operator==(const JniObjectValue&) const = default;
};

enum class JniObjectArrayErrorReason : std::uint8_t {
    unknown_array,
    invalid_length,
    invalid_index,
    invalid_element_class,
    invalid_value,
    incompatible_element,
};

class JniObjectArrayError final : public std::runtime_error {
public:
    JniObjectArrayError(JniObjectArrayErrorReason reason, const char* message);
    [[nodiscard]] JniObjectArrayErrorReason Reason() const noexcept;

private:
    JniObjectArrayErrorReason reason_;
};

class JniObjectArrayStore final {
public:
    explicit JniObjectArrayStore(const JniClassRegistry& classes);
    ~JniObjectArrayStore();
    JniObjectArrayStore(const JniObjectArrayStore&) = delete;
    JniObjectArrayStore& operator=(const JniObjectArrayStore&) = delete;
    JniObjectArrayStore(JniObjectArrayStore&&) noexcept;
    JniObjectArrayStore& operator=(JniObjectArrayStore&&) noexcept;

    [[nodiscard]] JniObjectIdentity New(
        JniObjectIdentity element_class, JniSize length,
        std::optional<JniObjectValue> initial = std::nullopt);
    void Delete(JniObjectIdentity array);
    [[nodiscard]] JniSize Length(JniObjectIdentity array) const;
    [[nodiscard]] JniObjectIdentity ElementClass(
        JniObjectIdentity array) const;
    [[nodiscard]] std::optional<JniObjectValue> Get(
        JniObjectIdentity array, JniSize index) const;
    void Set(JniObjectIdentity array, JniSize index,
             std::optional<JniObjectValue> value);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace ogplay::runtime
