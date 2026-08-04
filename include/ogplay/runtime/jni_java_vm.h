#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string_view>
#include <vector>

#include "ogplay/runtime/jni.h"
#include "ogplay/runtime/jni_environment.h"

namespace ogplay::runtime {

inline constexpr std::size_t kJniInvokeInterfaceSlotCount = 8;
inline constexpr JniInt kJniVersion1_1 = 0x00010001;
inline constexpr JniInt kJniVersion1_2 = 0x00010002;
inline constexpr JniInt kJniVersion1_4 = 0x00010004;

class JniInvokeSlot final {
public:
    explicit constexpr JniInvokeSlot(std::uint8_t value = 0) noexcept
        : value_(value) {}
    [[nodiscard]] constexpr std::uint8_t Value() const noexcept {
        return value_;
    }
    auto operator<=>(const JniInvokeSlot&) const = default;

private:
    std::uint8_t value_{};
};

class JniEnvHandle final {
public:
    explicit constexpr JniEnvHandle(std::uint32_t value = 0) noexcept
        : value_(value) {}
    [[nodiscard]] constexpr std::uint32_t Value() const noexcept {
        return value_;
    }
    [[nodiscard]] constexpr bool IsNull() const noexcept { return value_ == 0; }
    auto operator<=>(const JniEnvHandle&) const = default;

private:
    std::uint32_t value_{};
};

struct JniJavaVmResult final {
    JniStatus status{JniStatus::error};
    JniEnvHandle environment;
};

[[nodiscard]] std::span<const std::string_view> JniInvokeInterfaceSlots()
    noexcept;
[[nodiscard]] std::string_view JniInvokeSlotName(JniInvokeSlot slot);
[[nodiscard]] std::optional<JniInvokeSlot> FindJniInvokeSlot(
    std::string_view name) noexcept;

class JniJavaVm final {
public:
    explicit JniJavaVm(JniEnvironment& environment);
    ~JniJavaVm();
    JniJavaVm(const JniJavaVm&) = delete;
    JniJavaVm& operator=(const JniJavaVm&) = delete;
    JniJavaVm(JniJavaVm&&) noexcept;
    JniJavaVm& operator=(JniJavaVm&&) noexcept;

    [[nodiscard]] JniJavaVmResult GetEnv(std::uint64_t thread_id,
                                         JniInt version) const;
    [[nodiscard]] JniJavaVmResult AttachCurrentThread(
        std::uint64_t thread_id, JniInt version,
        std::size_t initial_local_capacity = 16);
    [[nodiscard]] JniJavaVmResult AttachCurrentThreadAsDaemon(
        std::uint64_t thread_id, JniInt version,
        std::size_t initial_local_capacity = 16);
    [[nodiscard]] JniStatus DetachCurrentThread(std::uint64_t thread_id);
    [[nodiscard]] bool IsDaemon(std::uint64_t thread_id) const;
    [[nodiscard]] std::size_t AttachedThreadCount() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

class JniInvokeUnimplementedCall final : public std::runtime_error {
public:
    JniInvokeUnimplementedCall(JniInvokeSlot slot,
                               std::uint64_t link_register);
    [[nodiscard]] JniInvokeSlot Slot() const noexcept { return slot_; }
    [[nodiscard]] std::uint64_t LinkRegister() const noexcept {
        return link_register_;
    }

private:
    JniInvokeSlot slot_;
    std::uint64_t link_register_{};
};

class JniInvokeFunctionTable final {
public:
    explicit JniInvokeFunctionTable(core::CapabilityLedger& ledger) noexcept;
    void Bind(JniInvokeSlot slot, memory::GuestAddress target);
    void Seal();
    [[nodiscard]] bool IsSealed() const noexcept { return sealed_; }
    [[nodiscard]] bool IsBound(JniInvokeSlot slot) const;
    [[nodiscard]] memory::GuestAddress Resolve(
        JniInvokeSlot slot, std::uint64_t link_register) const;

private:
    core::CapabilityLedger* ledger_{};
    std::array<std::optional<memory::GuestAddress>,
               kJniInvokeInterfaceSlotCount> targets_{};
    bool sealed_{};
};

enum class JniSlotHandlerKind : std::uint8_t {
    environment,
    class_registry,
    invocation,
    string_store,
    primitive_array_store,
    native_registry,
    java_vm,
};

struct JniThunkBinding final {
    memory::GuestAddress thunk;
    std::string_view name;
    JniSlotHandlerKind handler{JniSlotHandlerKind::environment};
    std::uint16_t slot{};
    bool java_vm{};
};

inline constexpr std::uint32_t kJniThunkBegin = 0x71000000U;
inline constexpr std::uint32_t kJniInvokeThunkBegin = 0x71100000U;

class JniCommonSlotDirectory final {
public:
    JniCommonSlotDirectory();

    void Install(JniFunctionTable& environment_table,
                 JniInvokeFunctionTable& invoke_table) const;
    [[nodiscard]] std::span<const JniThunkBinding> Bindings() const noexcept;
    [[nodiscard]] std::optional<JniThunkBinding> FindByThunk(
        memory::GuestAddress thunk) const noexcept;

private:
    std::vector<JniThunkBinding> bindings_;
};

}  // namespace ogplay::runtime
