#include "ogplay/runtime/guest_lifecycle.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <unordered_set>
#include <vector>

namespace ogplay::runtime {
namespace {

[[nodiscard]] std::optional<memory::GuestAddress> Relocate(
    const memory::GuestAddress address,
    const memory::GuestAddress load_bias) {
    if (address.Value() == 0 || address.Value() == UINT32_MAX) {
        return std::nullopt;
    }
    const auto result = static_cast<std::uint64_t>(address.Value()) +
                        load_bias.Value();
    if (result > std::numeric_limits<std::uint32_t>::max()) {
        throw GuestLifecycleError("guest lifecycle address wraps");
    }
    return memory::GuestAddress{static_cast<std::uint32_t>(result)};
}

[[nodiscard]] const GuestLifecycleModule& FindModule(
    const std::span<const GuestLifecycleModule> modules,
    const std::size_t index) {
    const auto found = std::find_if(
        modules.begin(), modules.end(), [index](const auto& module) {
            return module.module_index == index;
        });
    if (found == modules.end()) {
        throw GuestLifecycleError(
            "guest lifecycle order references an unknown module");
    }
    return *found;
}

void ValidateInputs(const std::span<const GuestLifecycleModule> modules,
                    const std::span<const std::size_t> order) {
    std::unordered_set<std::size_t> module_indices;
    for (const auto& module : modules) {
        if (!module_indices.insert(module.module_index).second) {
            throw GuestLifecycleError("duplicate guest lifecycle module index");
        }
    }
    std::unordered_set<std::size_t> ordered_indices;
    for (const auto index : order) {
        if (!ordered_indices.insert(index).second) {
            throw GuestLifecycleError("duplicate module in guest lifecycle order");
        }
        static_cast<void>(FindModule(modules, index));
    }
}

void Append(std::vector<GuestLifecycleCall>& result,
            const GuestLifecycleModule& module,
            const memory::GuestAddress address,
            const GuestLifecycleKind kind) {
    const auto relocated = Relocate(address, module.load_bias);
    if (relocated.has_value()) {
        result.push_back({module.module_index, *relocated, kind});
    }
}

}  // namespace

std::vector<GuestLifecycleCall> BuildGuestInitializationPlan(
    const std::span<const GuestLifecycleModule> modules,
    const std::span<const std::size_t> module_order) {
    ValidateInputs(modules, module_order);
    std::vector<GuestLifecycleCall> result;
    for (const auto index : module_order) {
        const auto& module = FindModule(modules, index);
        if (module.lifecycle.init.has_value()) {
            Append(result, module, *module.lifecycle.init,
                   GuestLifecycleKind::dynamic_init);
        }
        for (const auto address : module.lifecycle.init_array) {
            Append(result, module, address, GuestLifecycleKind::init_array);
        }
    }
    return result;
}

std::vector<GuestLifecycleCall> BuildGuestFinalizationPlan(
    const std::span<const GuestLifecycleModule> modules,
    const std::span<const std::size_t> module_order) {
    ValidateInputs(modules, module_order);
    std::vector<GuestLifecycleCall> result;
    for (const auto index : module_order) {
        const auto& module = FindModule(modules, index);
        for (auto address = module.lifecycle.fini_array.rbegin();
             address != module.lifecycle.fini_array.rend(); ++address) {
            Append(result, module, *address, GuestLifecycleKind::fini_array);
        }
        if (module.lifecycle.fini.has_value()) {
            Append(result, module, *module.lifecycle.fini,
                   GuestLifecycleKind::dynamic_fini);
        }
    }
    return result;
}

void ExecuteGuestLifecycle(const std::span<const GuestLifecycleCall> plan,
                           const GuestLifecycleInvoker& invoker) {
    if (!invoker) {
        throw GuestLifecycleError("guest lifecycle invoker is empty");
    }
    for (const auto& call : plan) invoker(call);
}

}  // namespace ogplay::runtime
