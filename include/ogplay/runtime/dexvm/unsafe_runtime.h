#pragma once

#include <cstdint>
#include <vector>

#include "ogplay/runtime/dexvm/dexvm_types.h"

namespace ogplay::runtime::dexvm {
class Interpreter;
struct VmValue;

enum class UnsafeValueKind : std::uint8_t { integer, long_integer, reference };

// Logical locations in the existing Java heap, never native addresses.
// All operations require the interpreter execution lock; no guest callbacks
// or blocking are allowed between a CAS read and write.
class UnsafeRuntime final {
public:
    explicit UnsafeRuntime(Interpreter& vm) : vm_(&vm) {}
    [[nodiscard]] std::int64_t ObjectFieldOffset(VmObjectRef field);
    [[nodiscard]] std::int32_t ArrayBaseOffset(VmObjectRef java_class);
    [[nodiscard]] std::int32_t ArrayIndexScale(VmObjectRef java_class);
    [[nodiscard]] VmValue Get(VmObjectRef object, std::int64_t offset,
                              UnsafeValueKind kind);
    void Put(VmObjectRef object, std::int64_t offset, UnsafeValueKind kind,
             const VmValue& value);
    [[nodiscard]] bool CompareAndSwap(VmObjectRef object, std::int64_t offset,
                                     UnsafeValueKind kind,
                                     const VmValue& expected,
                                     const VmValue& value);
private:
    struct Location;
    [[nodiscard]] Location Resolve(VmObjectRef object, std::int64_t offset,
                                    UnsafeValueKind kind);
    void CheckReference(const Location& location, const VmValue& value);
    Interpreter* vm_;
    // Metadata only: no object roots, no field-wrapper lifetime dependency.
    std::vector<VmFieldId> fields_;
};
}  // namespace ogplay::runtime::dexvm
