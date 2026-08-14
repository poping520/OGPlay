#include "ogplay/runtime/jni/jni.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

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

JniReferenceError::JniReferenceError(const JniReferenceErrorReason reason,
                                     std::string message)
    : std::runtime_error(std::move(message)), reason_(reason) {}

class JniReferenceTable::Impl final {
public:
    explicit Impl(const JniReferenceLimits limits) : limits_(limits) {
        if (limits_.local_per_thread == 0) {
            throw std::invalid_argument("JNI local reference limit must be positive");
        }
    }

    void AttachThread(const std::uint64_t thread_id,
                      const std::size_t initial_capacity) {
        if (thread_id == 0) Throw(JniReferenceErrorReason::invalid_thread,
                                  "JNI thread id cannot be zero");
        if (initial_capacity > limits_.local_per_thread) {
            Throw(JniReferenceErrorReason::capacity_exceeded,
                  "initial JNI local capacity exceeds the thread limit");
        }
        std::scoped_lock lock(mutex_);
        if (threads_.contains(thread_id)) {
            Throw(JniReferenceErrorReason::duplicate_thread,
                  "JNI thread is already attached");
        }
        threads_.emplace(thread_id,
                         ThreadState{{LocalFrame{initial_capacity, {}}}});
    }

    void DetachThread(const std::uint64_t thread_id) {
        std::scoped_lock lock(mutex_);
        const auto found = threads_.find(thread_id);
        if (found == threads_.end()) InvalidThread();
        for (const auto& frame : found->second.frames) {
            for (const auto handle : frame.handles) entries_.erase(handle);
        }
        threads_.erase(found);
    }

    [[nodiscard]] bool IsThreadAttached(const std::uint64_t thread_id) const {
        std::scoped_lock lock(mutex_);
        return threads_.contains(thread_id);
    }

    void EnsureLocalCapacity(const std::uint64_t thread_id,
                             const std::size_t additional) {
        std::scoped_lock lock(mutex_);
        const auto& thread = Thread(thread_id);
        const auto used = LocalCountLocked(thread);
        auto& frame = CurrentFrame(thread_id);
        if (additional > limits_.local_per_thread - used) {
            Capacity("JNI local reference capacity exceeded");
        }
        const auto required = frame.handles.size() + additional;
        if (required > frame.capacity) frame.capacity = required;
    }

    void PushLocalFrame(const std::uint64_t thread_id,
                        const std::size_t capacity) {
        std::scoped_lock lock(mutex_);
        auto& thread = Thread(thread_id);
        if (capacity > limits_.local_per_thread - LocalCountLocked(thread)) {
            Capacity("JNI local frame capacity exceeds the thread limit");
        }
        thread.frames.push_back({capacity, {}});
    }

    [[nodiscard]] JniReference PopLocalFrame(const std::uint64_t thread_id,
                                             const JniReference result) {
        std::scoped_lock lock(mutex_);
        auto& thread = Thread(thread_id);
        if (thread.frames.size() == 1) {
            Throw(JniReferenceErrorReason::frame_underflow,
                  "base JNI local frame cannot be popped");
        }
        const auto object = ResolveLocked(thread_id, result);
        auto& parent = thread.frames[thread.frames.size() - 2];
        if (object.has_value() &&
            parent.handles.size() >= limits_.local_per_thread) {
            Capacity("JNI parent local frame cannot preserve the result");
        }

        for (const auto handle : thread.frames.back().handles) {
            entries_.erase(handle);
        }
        thread.frames.pop_back();
        if (!object.has_value()) return JniReference{};
        if (parent.handles.size() == parent.capacity) ++parent.capacity;
        return AddLocalLocked(thread_id, *object);
    }

    [[nodiscard]] JniReference NewLocal(const std::uint64_t thread_id,
                                        const JniObjectIdentity object) {
        ValidateObject(object);
        std::scoped_lock lock(mutex_);
        return AddLocalLocked(thread_id, object);
    }

    [[nodiscard]] JniReference NewGlobal(const JniObjectIdentity object) {
        ValidateObject(object);
        std::scoped_lock lock(mutex_);
        if (global_count_ >= limits_.global) {
            Capacity("JNI global reference capacity exceeded");
        }
        const auto reference = AddEntry(JniReferenceKind::global, object, 0);
        ++global_count_;
        return reference;
    }

    [[nodiscard]] JniReference NewWeakGlobal(const JniObjectIdentity object) {
        ValidateObject(object);
        std::scoped_lock lock(mutex_);
        if (weak_count_ >= limits_.weak_global) {
            Capacity("JNI weak global reference capacity exceeded");
        }
        const auto reference = AddEntry(JniReferenceKind::weak_global, object, 0);
        ++weak_count_;
        return reference;
    }

    void DeleteLocal(const std::uint64_t thread_id,
                     const JniReference reference) {
        if (reference.IsNull()) return;
        std::scoped_lock lock(mutex_);
        auto& thread = Thread(thread_id);
        auto found = RequireEntry(reference);
        if (found->second.kind != JniReferenceKind::local) WrongKind();
        if (found->second.owner_thread != thread_id) InvalidReference();
        auto frame = std::find_if(
            thread.frames.begin(), thread.frames.end(),
            [&](const LocalFrame& candidate) {
                return std::find(candidate.handles.begin(), candidate.handles.end(),
                                 reference.Value()) != candidate.handles.end();
            });
        if (frame == thread.frames.end()) InvalidReference();
        frame->handles.erase(std::find(frame->handles.begin(), frame->handles.end(),
                                       reference.Value()));
        entries_.erase(found);
    }

    void DeleteGlobal(const JniReference reference) {
        DeleteShared(reference, JniReferenceKind::global, global_count_);
    }

    void DeleteWeakGlobal(const JniReference reference) {
        DeleteShared(reference, JniReferenceKind::weak_global, weak_count_);
    }

    [[nodiscard]] std::optional<JniObjectIdentity> Resolve(
        const std::uint64_t thread_id, const JniReference reference) const {
        std::scoped_lock lock(mutex_);
        return ResolveLocked(thread_id, reference);
    }

    [[nodiscard]] bool IsSameObject(const std::uint64_t thread_id,
                                    const JniReference left,
                                    const JniReference right) const {
        std::scoped_lock lock(mutex_);
        return ResolveLocked(thread_id, left) == ResolveLocked(thread_id, right);
    }

    void ClearWeakReferencesTo(const JniObjectIdentity object) {
        ValidateObject(object);
        std::scoped_lock lock(mutex_);
        for (auto& [handle, entry] : entries_) {
            static_cast<void>(handle);
            if (entry.kind == JniReferenceKind::weak_global &&
                entry.object == object) {
                entry.object.reset();
            }
        }
    }

    [[nodiscard]] std::size_t LocalCount(const std::uint64_t thread_id) const {
        std::scoped_lock lock(mutex_);
        const auto& thread = Thread(thread_id);
        std::size_t count = 0;
        for (const auto& frame : thread.frames) count += frame.handles.size();
        return count;
    }

    [[nodiscard]] std::size_t GlobalCount() const {
        std::scoped_lock lock(mutex_);
        return global_count_;
    }

    [[nodiscard]] std::size_t WeakGlobalCount() const {
        std::scoped_lock lock(mutex_);
        return weak_count_;
    }

private:
    struct Entry final {
        JniReferenceKind kind{JniReferenceKind::local};
        std::optional<JniObjectIdentity> object;
        std::uint64_t owner_thread{};
    };

    struct LocalFrame final {
        std::size_t capacity{};
        std::vector<std::uint32_t> handles;
    };

    struct ThreadState final {
        std::vector<LocalFrame> frames;
    };

    using EntryIterator = std::map<std::uint32_t, Entry>::iterator;

    [[noreturn]] static void Throw(const JniReferenceErrorReason reason,
                                   const char* message) {
        throw JniReferenceError(reason, message);
    }

    [[noreturn]] static void InvalidThread() {
        Throw(JniReferenceErrorReason::invalid_thread,
              "JNI thread is not attached");
    }

    [[noreturn]] static void InvalidReference() {
        Throw(JniReferenceErrorReason::invalid_reference,
              "JNI reference is invalid for this thread");
    }

    [[noreturn]] static void WrongKind() {
        Throw(JniReferenceErrorReason::wrong_reference_kind,
              "JNI reference kind does not match the operation");
    }

    [[noreturn]] static void Capacity(const char* message) {
        Throw(JniReferenceErrorReason::capacity_exceeded, message);
    }

    static void ValidateObject(const JniObjectIdentity object) {
        if (object.value == 0) {
            Throw(JniReferenceErrorReason::invalid_object,
                  "JNI object identity cannot be zero");
        }
    }

    [[nodiscard]] ThreadState& Thread(const std::uint64_t thread_id) {
        const auto found = threads_.find(thread_id);
        if (found == threads_.end()) InvalidThread();
        return found->second;
    }

    [[nodiscard]] const ThreadState& Thread(
        const std::uint64_t thread_id) const {
        const auto found = threads_.find(thread_id);
        if (found == threads_.end()) InvalidThread();
        return found->second;
    }

    [[nodiscard]] LocalFrame& CurrentFrame(const std::uint64_t thread_id) {
        return Thread(thread_id).frames.back();
    }

    [[nodiscard]] static std::size_t LocalCountLocked(
        const ThreadState& thread) {
        std::size_t count = 0;
        for (const auto& frame : thread.frames) count += frame.handles.size();
        return count;
    }

    [[nodiscard]] EntryIterator RequireEntry(const JniReference reference) {
        const auto found = entries_.find(reference.Value());
        if (found == entries_.end()) InvalidReference();
        return found;
    }

    [[nodiscard]] JniReference AllocateHandle() {
        if (next_handle_ > std::numeric_limits<std::uint32_t>::max()) {
            Capacity("JNI reference handle space exhausted");
        }
        return JniReference{static_cast<std::uint32_t>(next_handle_++)};
    }

    [[nodiscard]] JniReference AddEntry(const JniReferenceKind kind,
                                        const JniObjectIdentity object,
                                        const std::uint64_t owner_thread) {
        const auto reference = AllocateHandle();
        entries_.emplace(reference.Value(), Entry{kind, object, owner_thread});
        return reference;
    }

    [[nodiscard]] JniReference AddLocalLocked(
        const std::uint64_t thread_id, const JniObjectIdentity object) {
        auto& thread = Thread(thread_id);
        if (LocalCountLocked(thread) >= limits_.local_per_thread) {
            Capacity("JNI local frame capacity exceeded");
        }
        auto& frame = thread.frames.back();
        // AttachThread/PushLocalFrame guarantee their requested capacity but
        // JNI permits the VM to grow a frame beyond it while resources remain.
        if (frame.handles.size() == frame.capacity) ++frame.capacity;
        const auto reference = AddEntry(JniReferenceKind::local, object, thread_id);
        frame.handles.push_back(reference.Value());
        return reference;
    }

    [[nodiscard]] std::optional<JniObjectIdentity> ResolveLocked(
        const std::uint64_t thread_id, const JniReference reference) const {
        static_cast<void>(Thread(thread_id));
        if (reference.IsNull()) return std::nullopt;
        const auto found = entries_.find(reference.Value());
        if (found == entries_.end()) InvalidReference();
        if (found->second.kind == JniReferenceKind::local &&
            found->second.owner_thread != thread_id) {
            InvalidReference();
        }
        return found->second.object;
    }

    void DeleteShared(const JniReference reference, const JniReferenceKind kind,
                      std::size_t& count) {
        if (reference.IsNull()) return;
        std::scoped_lock lock(mutex_);
        const auto found = RequireEntry(reference);
        if (found->second.kind != kind) WrongKind();
        entries_.erase(found);
        --count;
    }

    JniReferenceLimits limits_;
    mutable std::mutex mutex_;
    std::map<std::uint32_t, Entry> entries_;
    std::map<std::uint64_t, ThreadState> threads_;
    std::uint64_t next_handle_{1};
    std::size_t global_count_{};
    std::size_t weak_count_{};
};

JniReferenceTable::JniReferenceTable(const JniReferenceLimits limits)
    : impl_(std::make_unique<Impl>(limits)) {}

JniReferenceTable::~JniReferenceTable() = default;
JniReferenceTable::JniReferenceTable(JniReferenceTable&&) noexcept = default;
JniReferenceTable& JniReferenceTable::operator=(JniReferenceTable&&) noexcept = default;

void JniReferenceTable::AttachThread(const std::uint64_t thread_id,
                                     const std::size_t initial_local_capacity) {
    impl_->AttachThread(thread_id, initial_local_capacity);
}

void JniReferenceTable::DetachThread(const std::uint64_t thread_id) {
    impl_->DetachThread(thread_id);
}

bool JniReferenceTable::IsThreadAttached(const std::uint64_t thread_id) const {
    return impl_->IsThreadAttached(thread_id);
}

void JniReferenceTable::EnsureLocalCapacity(
    const std::uint64_t thread_id, const std::size_t additional_capacity) {
    impl_->EnsureLocalCapacity(thread_id, additional_capacity);
}

void JniReferenceTable::PushLocalFrame(const std::uint64_t thread_id,
                                       const std::size_t capacity) {
    impl_->PushLocalFrame(thread_id, capacity);
}

JniReference JniReferenceTable::PopLocalFrame(
    const std::uint64_t thread_id, const JniReference result) {
    return impl_->PopLocalFrame(thread_id, result);
}

JniReference JniReferenceTable::NewLocal(const std::uint64_t thread_id,
                                         const JniObjectIdentity object) {
    return impl_->NewLocal(thread_id, object);
}

JniReference JniReferenceTable::NewGlobal(const JniObjectIdentity object) {
    return impl_->NewGlobal(object);
}

JniReference JniReferenceTable::NewWeakGlobal(const JniObjectIdentity object) {
    return impl_->NewWeakGlobal(object);
}

void JniReferenceTable::DeleteLocal(const std::uint64_t thread_id,
                                    const JniReference reference) {
    impl_->DeleteLocal(thread_id, reference);
}

void JniReferenceTable::DeleteGlobal(const JniReference reference) {
    impl_->DeleteGlobal(reference);
}

void JniReferenceTable::DeleteWeakGlobal(const JniReference reference) {
    impl_->DeleteWeakGlobal(reference);
}

std::optional<JniObjectIdentity> JniReferenceTable::Resolve(
    const std::uint64_t thread_id, const JniReference reference) const {
    return impl_->Resolve(thread_id, reference);
}

bool JniReferenceTable::IsSameObject(const std::uint64_t thread_id,
                                     const JniReference left,
                                     const JniReference right) const {
    return impl_->IsSameObject(thread_id, left, right);
}

void JniReferenceTable::ClearWeakReferencesTo(const JniObjectIdentity object) {
    impl_->ClearWeakReferencesTo(object);
}

std::size_t JniReferenceTable::LocalCount(const std::uint64_t thread_id) const {
    return impl_->LocalCount(thread_id);
}

std::size_t JniReferenceTable::GlobalCount() const { return impl_->GlobalCount(); }

std::size_t JniReferenceTable::WeakGlobalCount() const {
    return impl_->WeakGlobalCount();
}

}  // namespace ogplay::runtime
