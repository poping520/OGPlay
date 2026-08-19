#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>

#include "ogplay/runtime/dexvm/dexvm_types.h"
#include "ogplay/runtime/jni/jni.h"
#include "ogplay/runtime/jni/jni_array.h"
#include "ogplay/runtime/jni/jni_object.h"

namespace ogplay::runtime::dexvm {

// Session-level unified Java object model (02 §6). Four identity forms share
// one register-width handle space (VmObjectRef; 0 = null):
//   - VM instances: interpreter-owned field slot boards
//   - host-backed instances: intrinsic classes with an opaque host state
//   - strings / primitive arrays: delegated to the session's existing JNI
//     stores so native and interpreted code observe the same object
//   - object arrays and class objects: interpreter-owned
// Objects never move; handles stay valid for the session lifetime (GC-A is
// a budgeted arena without collection, 04 §5).

enum class SlotTag : std::uint8_t {
    uninit,
    cat1,
    wide_lo,
    wide_hi,
    ref,
};

struct Slot final {
    std::uint32_t bits{};
    SlotTag tag{SlotTag::uninit};
};

enum class VmObjectKind : std::uint8_t {
    vm_instance,
    host_backed,
    string,
    primitive_array,
    object_array,
    class_object,
    external,
};

struct JavaObjectModelConfig final {
    std::uint64_t heap_budget_bytes{64ULL * 1024ULL * 1024ULL};
};

class JavaObjectModel final {
public:
    JavaObjectModel(JniStringStore& strings, JniPrimitiveArrayStore& arrays,
                    JavaObjectModelConfig config = {});
    ~JavaObjectModel();
    JavaObjectModel(const JavaObjectModel&) = delete;
    JavaObjectModel& operator=(const JavaObjectModel&) = delete;

    // Core class ids used for objects created without explicit class
    // context (strings flowing in from JNI, class objects).
    void SetCoreClasses(DexClassId string_class, DexClassId class_class);

    [[nodiscard]] VmObjectRef FromIdentity(JniObjectIdentity identity);
    [[nodiscard]] JniObjectIdentity ToIdentity(VmObjectRef ref) const;
    [[nodiscard]] VmObjectKind Kind(VmObjectRef ref) const;
    [[nodiscard]] DexClassId ObjectClass(VmObjectRef ref) const;
    [[nodiscard]] bool IsValidRef(VmObjectRef ref) const noexcept;

    [[nodiscard]] VmObjectRef NewInstance(DexClassId java_class,
                                          std::uint16_t slot_count);
    [[nodiscard]] std::span<Slot> InstanceSlots(VmObjectRef ref);

    [[nodiscard]] VmObjectRef NewHostBacked(DexClassId java_class,
                                            std::uint64_t host_state);
    [[nodiscard]] std::uint64_t HostState(VmObjectRef ref) const;
    void SetHostState(VmObjectRef ref, std::uint64_t host_state);

    [[nodiscard]] VmObjectRef NewString(std::u16string_view value);
    [[nodiscard]] VmObjectRef InternString(std::u16string_view value);
    [[nodiscard]] std::u16string StringValue(VmObjectRef ref) const;
    // Converts a freshly allocated vm_instance of the string class into a
    // real string record in place (same handle, new store-backed identity).
    // This is how interpreted `new-instance String` + `String.<init>`
    // constructors publish their value without moving the object.
    void BindString(VmObjectRef ref, std::u16string_view value);

    [[nodiscard]] VmObjectRef NewPrimitiveArray(DexClassId array_class,
                                                JniPrimitiveKind kind,
                                                JniSize length);
    [[nodiscard]] JniPrimitiveKind PrimitiveArrayKind(VmObjectRef ref) const;
    [[nodiscard]] std::uint64_t GetPrimitiveElement(VmObjectRef ref,
                                                    JniSize index) const;
    void SetPrimitiveElement(VmObjectRef ref, JniSize index,
                             std::uint64_t bits);

    [[nodiscard]] VmObjectRef NewObjectArray(DexClassId array_class,
                                             DexClassId element_class,
                                             JniSize length);
    [[nodiscard]] DexClassId ObjectArrayElementClass(VmObjectRef ref) const;
    [[nodiscard]] VmObjectRef GetObjectElement(VmObjectRef ref,
                                               JniSize index) const;
    void SetObjectElement(VmObjectRef ref, JniSize index, VmObjectRef value);

    [[nodiscard]] JniSize ArrayLength(VmObjectRef ref) const;

    // Object.clone payload copy (AOSP dvmCloneObject): new identity and
    // header, shallow-copied instance slots or array elements. Supports
    // vm_instance, primitive_array, and object_array; other kinds fail.
    [[nodiscard]] VmObjectRef CloneObject(VmObjectRef source);

    // Bulk byte-array transfer (intrinsic IO paths).
    void WriteByteRegion(VmObjectRef ref, JniSize start,
                         std::span<const std::byte> bytes);
    [[nodiscard]] std::vector<std::byte> ReadByteRegion(VmObjectRef ref,
                                                        JniSize start,
                                                        JniSize length) const;

    [[nodiscard]] VmObjectRef ClassObject(DexClassId java_class);
    [[nodiscard]] DexClassId ClassOfClassObject(VmObjectRef ref) const;

    // Emergency reserve: lets the interpreter materialize the
    // OutOfMemoryError throwable itself after the budget is exhausted.
    void SetEmergencyReserve(bool enabled) noexcept;

    [[nodiscard]] std::uint64_t AllocatedBytes() const noexcept;
    [[nodiscard]] std::uint64_t ObjectCount() const noexcept;
    [[nodiscard]] std::uint64_t HeapBudgetBytes() const noexcept;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace ogplay::runtime::dexvm
