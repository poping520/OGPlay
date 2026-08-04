#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#include "ogplay/memory/address.h"
#include "ogplay/runtime/jni/jni_invocation.h"

namespace ogplay::runtime {

using GuestNativeExecutor =
    std::function<JniValue(memory::GuestAddress,
                           std::span<const JniValue>)>;

struct HeadlessJniContractReport final {
    bool java_to_native{};
    bool native_to_java{};
    bool data_round_trip{};
    bool reference_closed{};
    bool exception_closed{};
    bool asset_round_trip{};
    bool preferences_round_trip{};
    bool locale_round_trip{};
    bool package_round_trip{};
    bool native_threads_closed{};
    JniInt native_result{};
    std::size_t lifecycle_event_count{};
    std::vector<std::string> trace;
};

enum class HeadlessJniContractErrorReason : std::uint8_t {
    missing_executor,
    unresolved_native,
    native_return_mismatch,
    invariant_failed,
};

class HeadlessJniContractError final : public std::runtime_error {
public:
    HeadlessJniContractError(HeadlessJniContractErrorReason reason,
                             const char* message);
    [[nodiscard]] HeadlessJniContractErrorReason Reason() const noexcept;

private:
    HeadlessJniContractErrorReason reason_;
};

[[nodiscard]] HeadlessJniContractReport RunHeadlessJniContract(
    const GuestNativeExecutor& execute_native);

}  // namespace ogplay::runtime
