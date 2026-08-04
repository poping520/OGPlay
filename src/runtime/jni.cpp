#include "ogplay/runtime/jni.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace ogplay::runtime {
namespace {

#define OGPLAY_JNI_TRIPLE(name) #name, #name "V", #name "A"
#define OGPLAY_JNI_CALL3(prefix, type) \
    #prefix #type "Method", #prefix #type "MethodV", #prefix #type "MethodA"
#define OGPLAY_JNI_FIELD(prefix, type) #prefix #type "Field"
#define OGPLAY_JNI_ARRAY(prefix, type, suffix) #prefix #type #suffix

constexpr std::array<std::string_view, kJniNativeInterfaceSlotCount> kSlotNames{
    "reserved0",
    "reserved1",
    "reserved2",
    "reserved3",
    "GetVersion",
    "DefineClass",
    "FindClass",
    "FromReflectedMethod",
    "FromReflectedField",
    "ToReflectedMethod",
    "GetSuperclass",
    "IsAssignableFrom",
    "ToReflectedField",
    "Throw",
    "ThrowNew",
    "ExceptionOccurred",
    "ExceptionDescribe",
    "ExceptionClear",
    "FatalError",
    "PushLocalFrame",
    "PopLocalFrame",
    "NewGlobalRef",
    "DeleteGlobalRef",
    "DeleteLocalRef",
    "IsSameObject",
    "NewLocalRef",
    "EnsureLocalCapacity",
    "AllocObject",
    OGPLAY_JNI_TRIPLE(NewObject),
    "GetObjectClass",
    "IsInstanceOf",
    "GetMethodID",
    OGPLAY_JNI_CALL3(Call, Object),
    OGPLAY_JNI_CALL3(Call, Boolean),
    OGPLAY_JNI_CALL3(Call, Byte),
    OGPLAY_JNI_CALL3(Call, Char),
    OGPLAY_JNI_CALL3(Call, Short),
    OGPLAY_JNI_CALL3(Call, Int),
    OGPLAY_JNI_CALL3(Call, Long),
    OGPLAY_JNI_CALL3(Call, Float),
    OGPLAY_JNI_CALL3(Call, Double),
    OGPLAY_JNI_CALL3(Call, Void),
    OGPLAY_JNI_CALL3(CallNonvirtual, Object),
    OGPLAY_JNI_CALL3(CallNonvirtual, Boolean),
    OGPLAY_JNI_CALL3(CallNonvirtual, Byte),
    OGPLAY_JNI_CALL3(CallNonvirtual, Char),
    OGPLAY_JNI_CALL3(CallNonvirtual, Short),
    OGPLAY_JNI_CALL3(CallNonvirtual, Int),
    OGPLAY_JNI_CALL3(CallNonvirtual, Long),
    OGPLAY_JNI_CALL3(CallNonvirtual, Float),
    OGPLAY_JNI_CALL3(CallNonvirtual, Double),
    OGPLAY_JNI_CALL3(CallNonvirtual, Void),
    "GetFieldID",
    OGPLAY_JNI_FIELD(Get, Object),
    OGPLAY_JNI_FIELD(Get, Boolean),
    OGPLAY_JNI_FIELD(Get, Byte),
    OGPLAY_JNI_FIELD(Get, Char),
    OGPLAY_JNI_FIELD(Get, Short),
    OGPLAY_JNI_FIELD(Get, Int),
    OGPLAY_JNI_FIELD(Get, Long),
    OGPLAY_JNI_FIELD(Get, Float),
    OGPLAY_JNI_FIELD(Get, Double),
    OGPLAY_JNI_FIELD(Set, Object),
    OGPLAY_JNI_FIELD(Set, Boolean),
    OGPLAY_JNI_FIELD(Set, Byte),
    OGPLAY_JNI_FIELD(Set, Char),
    OGPLAY_JNI_FIELD(Set, Short),
    OGPLAY_JNI_FIELD(Set, Int),
    OGPLAY_JNI_FIELD(Set, Long),
    OGPLAY_JNI_FIELD(Set, Float),
    OGPLAY_JNI_FIELD(Set, Double),
    "GetStaticMethodID",
    OGPLAY_JNI_CALL3(CallStatic, Object),
    OGPLAY_JNI_CALL3(CallStatic, Boolean),
    OGPLAY_JNI_CALL3(CallStatic, Byte),
    OGPLAY_JNI_CALL3(CallStatic, Char),
    OGPLAY_JNI_CALL3(CallStatic, Short),
    OGPLAY_JNI_CALL3(CallStatic, Int),
    OGPLAY_JNI_CALL3(CallStatic, Long),
    OGPLAY_JNI_CALL3(CallStatic, Float),
    OGPLAY_JNI_CALL3(CallStatic, Double),
    OGPLAY_JNI_CALL3(CallStatic, Void),
    "GetStaticFieldID",
    OGPLAY_JNI_FIELD(GetStatic, Object),
    OGPLAY_JNI_FIELD(GetStatic, Boolean),
    OGPLAY_JNI_FIELD(GetStatic, Byte),
    OGPLAY_JNI_FIELD(GetStatic, Char),
    OGPLAY_JNI_FIELD(GetStatic, Short),
    OGPLAY_JNI_FIELD(GetStatic, Int),
    OGPLAY_JNI_FIELD(GetStatic, Long),
    OGPLAY_JNI_FIELD(GetStatic, Float),
    OGPLAY_JNI_FIELD(GetStatic, Double),
    OGPLAY_JNI_FIELD(SetStatic, Object),
    OGPLAY_JNI_FIELD(SetStatic, Boolean),
    OGPLAY_JNI_FIELD(SetStatic, Byte),
    OGPLAY_JNI_FIELD(SetStatic, Char),
    OGPLAY_JNI_FIELD(SetStatic, Short),
    OGPLAY_JNI_FIELD(SetStatic, Int),
    OGPLAY_JNI_FIELD(SetStatic, Long),
    OGPLAY_JNI_FIELD(SetStatic, Float),
    OGPLAY_JNI_FIELD(SetStatic, Double),
    "NewString",
    "GetStringLength",
    "GetStringChars",
    "ReleaseStringChars",
    "NewStringUTF",
    "GetStringUTFLength",
    "GetStringUTFChars",
    "ReleaseStringUTFChars",
    "GetArrayLength",
    "NewObjectArray",
    "GetObjectArrayElement",
    "SetObjectArrayElement",
    OGPLAY_JNI_ARRAY(New, Boolean, Array),
    OGPLAY_JNI_ARRAY(New, Byte, Array),
    OGPLAY_JNI_ARRAY(New, Char, Array),
    OGPLAY_JNI_ARRAY(New, Short, Array),
    OGPLAY_JNI_ARRAY(New, Int, Array),
    OGPLAY_JNI_ARRAY(New, Long, Array),
    OGPLAY_JNI_ARRAY(New, Float, Array),
    OGPLAY_JNI_ARRAY(New, Double, Array),
    OGPLAY_JNI_ARRAY(Get, Boolean, ArrayElements),
    OGPLAY_JNI_ARRAY(Get, Byte, ArrayElements),
    OGPLAY_JNI_ARRAY(Get, Char, ArrayElements),
    OGPLAY_JNI_ARRAY(Get, Short, ArrayElements),
    OGPLAY_JNI_ARRAY(Get, Int, ArrayElements),
    OGPLAY_JNI_ARRAY(Get, Long, ArrayElements),
    OGPLAY_JNI_ARRAY(Get, Float, ArrayElements),
    OGPLAY_JNI_ARRAY(Get, Double, ArrayElements),
    OGPLAY_JNI_ARRAY(Release, Boolean, ArrayElements),
    OGPLAY_JNI_ARRAY(Release, Byte, ArrayElements),
    OGPLAY_JNI_ARRAY(Release, Char, ArrayElements),
    OGPLAY_JNI_ARRAY(Release, Short, ArrayElements),
    OGPLAY_JNI_ARRAY(Release, Int, ArrayElements),
    OGPLAY_JNI_ARRAY(Release, Long, ArrayElements),
    OGPLAY_JNI_ARRAY(Release, Float, ArrayElements),
    OGPLAY_JNI_ARRAY(Release, Double, ArrayElements),
    OGPLAY_JNI_ARRAY(Get, Boolean, ArrayRegion),
    OGPLAY_JNI_ARRAY(Get, Byte, ArrayRegion),
    OGPLAY_JNI_ARRAY(Get, Char, ArrayRegion),
    OGPLAY_JNI_ARRAY(Get, Short, ArrayRegion),
    OGPLAY_JNI_ARRAY(Get, Int, ArrayRegion),
    OGPLAY_JNI_ARRAY(Get, Long, ArrayRegion),
    OGPLAY_JNI_ARRAY(Get, Float, ArrayRegion),
    OGPLAY_JNI_ARRAY(Get, Double, ArrayRegion),
    OGPLAY_JNI_ARRAY(Set, Boolean, ArrayRegion),
    OGPLAY_JNI_ARRAY(Set, Byte, ArrayRegion),
    OGPLAY_JNI_ARRAY(Set, Char, ArrayRegion),
    OGPLAY_JNI_ARRAY(Set, Short, ArrayRegion),
    OGPLAY_JNI_ARRAY(Set, Int, ArrayRegion),
    OGPLAY_JNI_ARRAY(Set, Long, ArrayRegion),
    OGPLAY_JNI_ARRAY(Set, Float, ArrayRegion),
    OGPLAY_JNI_ARRAY(Set, Double, ArrayRegion),
    "RegisterNatives",
    "UnregisterNatives",
    "MonitorEnter",
    "MonitorExit",
    "GetJavaVM",
    "GetStringRegion",
    "GetStringUTFRegion",
    "GetPrimitiveArrayCritical",
    "ReleasePrimitiveArrayCritical",
    "GetStringCritical",
    "ReleaseStringCritical",
    "NewWeakGlobalRef",
    "DeleteWeakGlobalRef",
    "ExceptionCheck",
    "NewDirectByteBuffer",
    "GetDirectBufferAddress",
    "GetDirectBufferCapacity",
    "GetObjectRefType",
};

#undef OGPLAY_JNI_ARRAY
#undef OGPLAY_JNI_FIELD
#undef OGPLAY_JNI_CALL3
#undef OGPLAY_JNI_TRIPLE

[[nodiscard]] std::size_t SlotIndex(const JniSlot slot) {
    const auto index = static_cast<std::size_t>(slot.Value());
    if (index >= kSlotNames.size()) {
        throw std::invalid_argument("JNI slot index is outside JNINativeInterface");
    }
    return index;
}

[[nodiscard]] bool IsReserved(const std::size_t index) noexcept {
    return index < kJniReservedSlotCount;
}

}  // namespace

std::span<const std::string_view> JniNativeInterfaceSlots() noexcept {
    return kSlotNames;
}

std::string_view JniSlotName(const JniSlot slot) {
    return kSlotNames[SlotIndex(slot)];
}

std::optional<JniSlot> FindJniSlot(const std::string_view name) noexcept {
    const auto found = std::find(kSlotNames.begin(), kSlotNames.end(), name);
    if (found == kSlotNames.end()) return std::nullopt;
    const auto index = static_cast<std::uint16_t>(found - kSlotNames.begin());
    return JniSlot{index};
}

std::string JniCapabilityId(const JniSlot slot) {
    return "runtime.jni.slot." + std::string(JniSlotName(slot));
}

JniUnimplementedCall::JniUnimplementedCall(
    const JniSlot slot, const std::uint64_t link_register)
    : std::runtime_error("unimplemented JNI slot: " +
                         std::string(JniSlotName(slot))),
      slot_(slot),
      link_register_(link_register) {}

JniFunctionTable::JniFunctionTable(core::CapabilityLedger& ledger) noexcept
    : ledger_(&ledger) {}

void JniFunctionTable::Bind(const JniSlot slot,
                            const memory::GuestAddress target) {
    if (sealed_) throw std::logic_error("JNI function table is sealed");
    const auto index = SlotIndex(slot);
    if (IsReserved(index)) {
        throw std::invalid_argument("reserved JNI slots cannot be bound");
    }
    if (target.IsNull()) {
        throw std::invalid_argument("JNI function target cannot be null");
    }
    if (targets_[index].has_value()) {
        throw std::logic_error("JNI function slot is already bound");
    }
    targets_[index] = target;
}

void JniFunctionTable::Seal() {
    if (sealed_) throw std::logic_error("JNI function table is already sealed");
    sealed_ = true;
}

bool JniFunctionTable::IsBound(const JniSlot slot) const {
    return targets_[SlotIndex(slot)].has_value();
}

memory::GuestAddress JniFunctionTable::Resolve(
    const JniSlot slot, const std::uint64_t link_register) const {
    if (!sealed_) throw std::logic_error("JNI function table is not sealed");
    const auto index = SlotIndex(slot);
    if (IsReserved(index)) {
        throw std::invalid_argument("reserved JNI slots are not callable");
    }
    if (!targets_[index].has_value()) {
        ledger_->RecordUnimplemented(JniCapabilityId(slot), link_register);
        throw JniUnimplementedCall(slot, link_register);
    }
    return *targets_[index];
}

}  // namespace ogplay::runtime
