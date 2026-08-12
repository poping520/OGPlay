#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "ogplay/runtime/dexvm/interpreter.h"
#include "ogplay/runtime/integration/android_guest_call_session.h"

namespace ogplay::runtime {

// Assembles the dexvm interpreter on top of a running Android guest call
// session and bridges both JNI directions (docs/design/dexvm/04-integration.md
// §1):
//   outbound  interpreter -> native: descriptor-driven A32 marshaling into
//             the existing guest call executor (RegisterNatives mappings
//             first, then Java_ export names);
//   inbound   native -> interpreter: every interpreted dex class/method is
//             registered into the session's JniClassRegistry with a
//             "dexvm.m<id>" implementation handler, so FindClass /
//             GetStaticMethodID / CallStatic* resolve real DEX facts through
//             the unchanged 233-slot ABI (the third route).

struct DexVmBridgeConfig final {
    dexvm::InterpreterConfig interpreter{};
    dexvm::JavaObjectModelConfig heap{};
};

class DexVmBridgeError final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class DexVmGuestBridge final : public dexvm::NativeMethodBridge {
public:
    DexVmGuestBridge(
        AndroidGuestCallSession& session, std::vector<std::uint8_t> dex_bytes,
        std::span<const dexvm::IntrinsicClassDecl> platform_catalog,
        const std::function<void(dexvm::IntrinsicRegistry&)>&
            platform_handlers,
        core::CapabilityLedger& ledger, core::Logger* logger,
        DexVmBridgeConfig config = {});
    ~DexVmGuestBridge() override;
    DexVmGuestBridge(const DexVmGuestBridge&) = delete;
    DexVmGuestBridge& operator=(const DexVmGuestBridge&) = delete;

    [[nodiscard]] dexvm::Interpreter& Vm() noexcept;
    [[nodiscard]] dexvm::DexClassLinker& Linker() noexcept;
    [[nodiscard]] dexvm::JavaObjectModel& Model() noexcept;
    [[nodiscard]] AndroidGuestCallSession& Session() noexcept;

    // Reference conversions on the session root thread.
    [[nodiscard]] JniReference PublishLocal(dexvm::VmObjectRef ref);
    [[nodiscard]] dexvm::VmObjectRef FromReference(JniReference reference);
    [[nodiscard]] std::optional<JniObjectIdentity> RegisteredClassIdentity(
        dexvm::DexClassId java_class) const;

    // Outbound native invocation (dexvm::NativeMethodBridge).
    [[nodiscard]] dexvm::VmValue Invoke(
        const dexvm::LinkedMethod& method, dexvm::VmObjectRef receiver,
        std::span<const dexvm::VmValue> arguments) override;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace ogplay::runtime
