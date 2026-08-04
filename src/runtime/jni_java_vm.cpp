#include "ogplay/runtime/jni_java_vm.h"

#include <algorithm>
#include <array>
#include <limits>
#include <map>
#include <mutex>
#include <stdexcept>
#include <utility>

namespace ogplay::runtime {
namespace {

constexpr std::array<std::string_view, kJniInvokeInterfaceSlotCount>
    kInvokeSlots{"reserved0", "reserved1", "reserved2", "DestroyJavaVM",
                 "AttachCurrentThread", "DetachCurrentThread", "GetEnv",
                 "AttachCurrentThreadAsDaemon"};

[[nodiscard]] bool IsSupportedVersion(const JniInt version) noexcept {
    return version == kJniVersion1_1 || version == kJniVersion1_2 ||
           version == kJniVersion1_4 || version == kJniVersion1_6;
}

template <typename Range>
[[nodiscard]] bool IsOneOf(const std::string_view name, const Range& names) {
    return std::find(names.begin(), names.end(), name) != names.end();
}

[[nodiscard]] bool IsCallSlot(const std::string_view name) {
    return name.starts_with("Call") && name.find("Method") != name.npos;
}

[[nodiscard]] bool IsPrimitiveArraySlot(const std::string_view name) {
    constexpr std::array types{"Boolean", "Byte", "Char", "Short", "Int",
                               "Long", "Float", "Double"};
    for (const auto type : types) {
        const std::string marker{type};
        if (name == "New" + marker + "Array" ||
            name == "Get" + marker + "ArrayElements" ||
            name == "Release" + marker + "ArrayElements" ||
            name == "Get" + marker + "ArrayRegion" ||
            name == "Set" + marker + "ArrayRegion") {
            return true;
        }
    }
    return name == "GetArrayLength" ||
           name == "GetPrimitiveArrayCritical" ||
           name == "ReleasePrimitiveArrayCritical";
}

[[nodiscard]] std::optional<JniSlotHandlerKind> CommonHandler(
    const std::string_view name) {
    constexpr std::array environment{
        "GetVersion",          "Throw",          "ExceptionOccurred",
        "ExceptionClear",      "PushLocalFrame", "PopLocalFrame",
        "NewGlobalRef",        "DeleteGlobalRef", "DeleteLocalRef",
        "IsSameObject",        "NewLocalRef",     "EnsureLocalCapacity",
        "NewWeakGlobalRef",    "DeleteWeakGlobalRef",
        "ExceptionCheck"};
    constexpr std::array classes{
        "FindClass",       "GetSuperclass", "IsAssignableFrom",
        "GetMethodID",     "GetFieldID",     "GetStaticMethodID",
        "GetStaticFieldID"};
    constexpr std::array strings{
        "NewString",          "GetStringLength",    "GetStringChars",
        "ReleaseStringChars", "NewStringUTF",       "GetStringUTFLength",
        "GetStringUTFChars",  "ReleaseStringUTFChars", "GetStringRegion",
        "GetStringUTFRegion", "GetStringCritical",  "ReleaseStringCritical"};
    constexpr std::array natives{"RegisterNatives", "UnregisterNatives"};
    if (IsOneOf(name, environment)) return JniSlotHandlerKind::environment;
    if (IsOneOf(name, classes)) return JniSlotHandlerKind::class_registry;
    if (IsCallSlot(name)) return JniSlotHandlerKind::invocation;
    if (IsOneOf(name, strings)) return JniSlotHandlerKind::string_store;
    if (IsPrimitiveArraySlot(name)) {
        return JniSlotHandlerKind::primitive_array_store;
    }
    if (IsOneOf(name, natives)) return JniSlotHandlerKind::native_registry;
    return std::nullopt;
}

[[nodiscard]] std::size_t InvokeSlotIndex(const JniInvokeSlot slot) {
    const auto index = static_cast<std::size_t>(slot.Value());
    if (index >= kInvokeSlots.size()) {
        throw std::invalid_argument("JNI invoke slot is outside JavaVM ABI");
    }
    return index;
}

}  // namespace

std::span<const std::string_view> JniInvokeInterfaceSlots() noexcept {
    return kInvokeSlots;
}

std::string_view JniInvokeSlotName(const JniInvokeSlot slot) {
    const auto index = static_cast<std::size_t>(slot.Value());
    if (index >= kInvokeSlots.size()) {
        throw std::invalid_argument("JNI invoke slot is outside JavaVM ABI");
    }
    return kInvokeSlots[index];
}

std::optional<JniInvokeSlot> FindJniInvokeSlot(
    const std::string_view name) noexcept {
    const auto found = std::find(kInvokeSlots.begin(), kInvokeSlots.end(), name);
    if (found == kInvokeSlots.end()) return std::nullopt;
    return JniInvokeSlot{
        static_cast<std::uint8_t>(std::distance(kInvokeSlots.begin(), found))};
}

class JniJavaVm::Impl final {
public:
    explicit Impl(JniEnvironment& environment) : environment_(&environment) {}

    [[nodiscard]] JniJavaVmResult GetEnv(const std::uint64_t thread_id,
                                         const JniInt version) const {
        if (!IsSupportedVersion(version)) {
            return {JniStatus::version, JniEnvHandle{}};
        }
        if (thread_id == 0) {
            return {JniStatus::invalid_arguments, JniEnvHandle{}};
        }
        std::scoped_lock lock(mutex_);
        const auto found = threads_.find(thread_id);
        if (found == threads_.end()) {
            return {JniStatus::detached, JniEnvHandle{}};
        }
        return {JniStatus::ok, found->second.environment};
    }

    [[nodiscard]] JniJavaVmResult Attach(
        const std::uint64_t thread_id, const JniInt version,
        const std::size_t initial_local_capacity, const bool daemon) {
        if (!IsSupportedVersion(version)) {
            return {JniStatus::version, JniEnvHandle{}};
        }
        if (thread_id == 0) {
            return {JniStatus::invalid_arguments, JniEnvHandle{}};
        }
        std::scoped_lock lock(mutex_);
        const auto found = threads_.find(thread_id);
        if (found != threads_.end()) {
            return {JniStatus::ok, found->second.environment};
        }
        if (next_environment_ == 0) {
            return {JniStatus::no_memory, JniEnvHandle{}};
        }
        environment_->AttachThread(thread_id, initial_local_capacity);
        const JniEnvHandle handle{next_environment_++};
        threads_.emplace(thread_id, ThreadEntry{handle, daemon});
        return {JniStatus::ok, handle};
    }

    [[nodiscard]] JniStatus Detach(const std::uint64_t thread_id) {
        if (thread_id == 0) return JniStatus::invalid_arguments;
        std::scoped_lock lock(mutex_);
        const auto found = threads_.find(thread_id);
        if (found == threads_.end()) return JniStatus::detached;
        environment_->DetachThread(thread_id);
        threads_.erase(found);
        return JniStatus::ok;
    }

    [[nodiscard]] bool IsDaemon(const std::uint64_t thread_id) const {
        std::scoped_lock lock(mutex_);
        const auto found = threads_.find(thread_id);
        if (found == threads_.end()) {
            throw std::invalid_argument("JNI thread is not attached to JavaVM");
        }
        return found->second.daemon;
    }

    [[nodiscard]] std::size_t Count() const {
        std::scoped_lock lock(mutex_);
        return threads_.size();
    }

private:
    struct ThreadEntry final {
        JniEnvHandle environment;
        bool daemon{};
    };

    JniEnvironment* environment_{};
    mutable std::mutex mutex_;
    std::map<std::uint64_t, ThreadEntry> threads_;
    std::uint32_t next_environment_{1};
};

JniJavaVm::JniJavaVm(JniEnvironment& environment)
    : impl_(std::make_unique<Impl>(environment)) {}
JniJavaVm::~JniJavaVm() = default;
JniJavaVm::JniJavaVm(JniJavaVm&&) noexcept = default;
JniJavaVm& JniJavaVm::operator=(JniJavaVm&&) noexcept = default;

JniJavaVmResult JniJavaVm::GetEnv(const std::uint64_t thread_id,
                                  const JniInt version) const {
    return impl_->GetEnv(thread_id, version);
}

JniJavaVmResult JniJavaVm::AttachCurrentThread(
    const std::uint64_t thread_id, const JniInt version,
    const std::size_t initial_local_capacity) {
    return impl_->Attach(thread_id, version, initial_local_capacity, false);
}

JniJavaVmResult JniJavaVm::AttachCurrentThreadAsDaemon(
    const std::uint64_t thread_id, const JniInt version,
    const std::size_t initial_local_capacity) {
    return impl_->Attach(thread_id, version, initial_local_capacity, true);
}

JniStatus JniJavaVm::DetachCurrentThread(const std::uint64_t thread_id) {
    return impl_->Detach(thread_id);
}

bool JniJavaVm::IsDaemon(const std::uint64_t thread_id) const {
    return impl_->IsDaemon(thread_id);
}

std::size_t JniJavaVm::AttachedThreadCount() const { return impl_->Count(); }

JniInvokeUnimplementedCall::JniInvokeUnimplementedCall(
    const JniInvokeSlot slot, const std::uint64_t link_register)
    : std::runtime_error("unimplemented JavaVM slot: " +
                         std::string(JniInvokeSlotName(slot))),
      slot_(slot),
      link_register_(link_register) {}

JniInvokeFunctionTable::JniInvokeFunctionTable(
    core::CapabilityLedger& ledger) noexcept
    : ledger_(&ledger) {}

void JniInvokeFunctionTable::Bind(const JniInvokeSlot slot,
                                  const memory::GuestAddress target) {
    if (sealed_) throw std::logic_error("JavaVM function table is sealed");
    const auto index = InvokeSlotIndex(slot);
    if (index < 3) {
        throw std::invalid_argument("reserved JavaVM slots cannot be bound");
    }
    if (target.IsNull()) {
        throw std::invalid_argument("JavaVM function target cannot be null");
    }
    if (targets_[index].has_value()) {
        throw std::logic_error("JavaVM function slot is already bound");
    }
    targets_[index] = target;
}

void JniInvokeFunctionTable::Seal() {
    if (sealed_) throw std::logic_error("JavaVM function table is already sealed");
    sealed_ = true;
}

bool JniInvokeFunctionTable::IsBound(const JniInvokeSlot slot) const {
    return targets_[InvokeSlotIndex(slot)].has_value();
}

memory::GuestAddress JniInvokeFunctionTable::Resolve(
    const JniInvokeSlot slot, const std::uint64_t link_register) const {
    if (!sealed_) throw std::logic_error("JavaVM function table is not sealed");
    const auto index = InvokeSlotIndex(slot);
    if (index < 3) {
        throw std::invalid_argument("reserved JavaVM slots are not callable");
    }
    if (!targets_[index].has_value()) {
        ledger_->RecordUnimplemented(
            "runtime.jni.invoke." + std::string(JniInvokeSlotName(slot)),
            link_register);
        throw JniInvokeUnimplementedCall(slot, link_register);
    }
    return *targets_[index];
}

JniCommonSlotDirectory::JniCommonSlotDirectory() {
    const auto slots = JniNativeInterfaceSlots();
    for (std::size_t index = kJniReservedSlotCount; index < slots.size();
         ++index) {
        const auto handler = CommonHandler(slots[index]);
        if (!handler.has_value()) continue;
        bindings_.push_back(
            {memory::GuestAddress{kJniThunkBegin +
                                  static_cast<std::uint32_t>(index * 4)},
             slots[index], *handler, static_cast<std::uint16_t>(index), false});
    }
    for (std::size_t index = 4; index < kInvokeSlots.size(); ++index) {
        bindings_.push_back(
            {memory::GuestAddress{kJniInvokeThunkBegin +
                                  static_cast<std::uint32_t>(index * 4)},
             kInvokeSlots[index], JniSlotHandlerKind::java_vm,
             static_cast<std::uint16_t>(index), true});
    }
}

void JniCommonSlotDirectory::Install(
    JniFunctionTable& environment_table,
    JniInvokeFunctionTable& invoke_table) const {
    for (const auto& binding : bindings_) {
        if (binding.java_vm) {
            invoke_table.Bind(
                JniInvokeSlot{static_cast<std::uint8_t>(binding.slot)},
                binding.thunk);
        } else {
            environment_table.Bind(JniSlot{binding.slot}, binding.thunk);
        }
    }
    environment_table.Seal();
    invoke_table.Seal();
}

std::span<const JniThunkBinding> JniCommonSlotDirectory::Bindings() const
    noexcept {
    return bindings_;
}

std::optional<JniThunkBinding> JniCommonSlotDirectory::FindByThunk(
    const memory::GuestAddress thunk) const noexcept {
    const auto found = std::find_if(
        bindings_.begin(), bindings_.end(),
        [thunk](const JniThunkBinding& binding) {
            return binding.thunk == thunk;
        });
    return found == bindings_.end()
               ? std::nullopt
               : std::optional<JniThunkBinding>{*found};
}

}  // namespace ogplay::runtime
