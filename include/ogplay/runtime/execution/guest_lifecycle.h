#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <stdexcept>
#include <vector>

#include "ogplay/loader/lifecycle.h"

namespace ogplay::runtime {

enum class GuestLifecycleKind : std::uint8_t {
    dynamic_init,
    init_array,
    fini_array,
    dynamic_fini,
};

struct GuestLifecycleModule final {
    std::size_t module_index{};
    memory::GuestAddress load_bias;
    loader::Elf32LifecycleInfo lifecycle;
};

struct GuestLifecycleCall final {
    std::size_t module_index{};
    memory::GuestAddress address;
    GuestLifecycleKind kind{};

    bool operator==(const GuestLifecycleCall&) const = default;
};

using GuestLifecycleInvoker = std::function<void(const GuestLifecycleCall&)>;

class GuestLifecycleError final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

[[nodiscard]] std::vector<GuestLifecycleCall> BuildGuestInitializationPlan(
    std::span<const GuestLifecycleModule> modules,
    std::span<const std::size_t> module_order);
[[nodiscard]] std::vector<GuestLifecycleCall> BuildGuestFinalizationPlan(
    std::span<const GuestLifecycleModule> modules,
    std::span<const std::size_t> module_order);
void ExecuteGuestLifecycle(std::span<const GuestLifecycleCall> plan,
                           const GuestLifecycleInvoker& invoker);

}  // namespace ogplay::runtime
