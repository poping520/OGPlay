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

}  // namespace ogplay::runtime
