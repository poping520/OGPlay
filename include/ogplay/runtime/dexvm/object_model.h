#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include "ogplay/runtime/dexvm/dexvm_types.h"
#include "ogplay/runtime/jni/jni.h"
#include "ogplay/runtime/jni/jni_array.h"
#include "ogplay/runtime/jni/jni_object.h"
#include "ogplay/runtime/jni/jni_object_array.h"

namespace ogplay::runtime::dexvm {

// Session-level unified Java object model (02 §6). Four identity forms share
// one register-width handle space (VmObjectRef; 0 = null):
//   - VM instances: interpreter-owned field slot boards
//   - host-backed instances: intrinsic classes with an opaque host state
//   - strings / primitive arrays: delegated to the session's existing JNI
//     stores so native and interpreted code observe the same object
//   - object arrays and class objects: interpreter-owned
// Objects never move while live. GC may reuse an internal handle after the
// object dies, so guest-visible identity is carried separately from handles.

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
    std::uint32_t gc_watermark_percent{75};
};

// Session-owned JNI object services used to keep interpreted and native
// code on one identity/storage path. Class callbacks are supplied by the
// integration layer because dexvm must not depend on the JNI class registry.
struct JavaObjectInterop final {
    JniObjectArrayStore* object_arrays{};
    std::function<JniObjectIdentity(DexClassId)> publish_class;
    std::function<DexClassId(JniObjectIdentity)> resolve_class;
    std::function<DexClassId(JniObjectIdentity)> resolve_object_class;
    std::function<std::pair<DexClassId, DexClassId>(JniObjectIdentity)>
        resolve_object_array_class;
    std::function<std::optional<std::uint16_t>(DexClassId)>
        resolve_instance_slots;
};

struct GcMarkResult final {
    std::vector<bool> marked;
    std::uint64_t live_bytes{};
    std::uint64_t garbage_bytes{};
    std::uint64_t live_objects{};
    std::uint64_t garbage_objects{};

    [[nodiscard]] bool IsMarked(VmObjectRef ref) const noexcept {
        return ref.IsValid() && ref.Value() <= marked.size() &&
               marked[ref.Value() - 1U];
    }
};

struct GcSweepHooks final {
    std::function<bool(VmObjectRef, VmObjectKind, DexClassId, std::uint64_t)>
        before_release;
    std::function<void(VmObjectRef, JniObjectIdentity)> release_external_state;
};

struct GcSweepResult final {
    std::uint64_t freed_bytes{};
    std::uint64_t freed_objects{};
    std::uint64_t host_destructors_run{};
};

class JavaObjectModel final {
public:
    using RootVisitor = std::function<void(VmObjectRef)>;
    JavaObjectModel(JniStringStore& strings, JniPrimitiveArrayStore& arrays,
                    JavaObjectModelConfig config = {},
                    JavaObjectInterop interop = {});
    ~JavaObjectModel();
    JavaObjectModel(const JavaObjectModel&) = delete;
    JavaObjectModel& operator=(const JavaObjectModel&) = delete;

    // Core class ids used for objects created without explicit class
    // context (strings flowing in from JNI, class objects).
    void SetCoreClasses(DexClassId string_class, DexClassId class_class);
    // Class handles and linker ids depend on catalog assembly order. Supplying
    // the descriptor resolver lets class-object identity remain stable when
    // platform shapes are inserted or reordered.
    void SetClassDescriptorResolver(
        std::function<std::string(DexClassId)> resolver);

    [[nodiscard]] VmObjectRef FromIdentity(JniObjectIdentity identity);
    [[nodiscard]] JniObjectIdentity ToIdentity(VmObjectRef ref) const;
    [[nodiscard]] VmObjectKind Kind(VmObjectRef ref) const;
    [[nodiscard]] DexClassId ObjectClass(VmObjectRef ref) const;
    // Java identity hash: 0 for null, stable for the lifetime of a live
    // object, and intentionally independent from VmObjectRef/storage reuse.
    [[nodiscard]] std::int32_t IdentityHashCode(VmObjectRef ref) const;
    [[nodiscard]] bool IsValidRef(VmObjectRef ref) const noexcept;
    [[nodiscard]] VmObjectRef FindIdentity(JniObjectIdentity identity) const noexcept;
    void VisitPermanentRoots(const RootVisitor& visitor) const;
    [[nodiscard]] GcMarkResult MarkReachable(
        const std::vector<VmObjectRef>& roots,
        const std::function<void(VmObjectRef, const RootVisitor&)>&
            trace_host_edges);
    [[nodiscard]] GcSweepResult Sweep(const GcMarkResult& mark,
                                      const GcSweepHooks& hooks);

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
                                               JniSize index);
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
    [[nodiscard]] std::uint32_t GcWatermarkPercent() const noexcept;
    [[nodiscard]] bool ShouldCollectFor(std::uint64_t request_bytes) const noexcept;

    [[nodiscard]] static std::uint64_t EstimateInstanceBytes(
        std::uint16_t slot_count) noexcept;
    [[nodiscard]] static std::uint64_t EstimateStringBytes(
        std::size_t code_units) noexcept;
    [[nodiscard]] static std::uint64_t EstimatePrimitiveArrayBytes(
        JniPrimitiveKind kind, JniSize length);
    [[nodiscard]] static std::uint64_t EstimateObjectArrayBytes(
        JniSize length);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace ogplay::runtime::dexvm
