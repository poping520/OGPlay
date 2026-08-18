#include "ogplay/loader/module_loader.h"

#include <cstddef>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

#include "ogplay/loader/symbol_version.h"

namespace ogplay::loader {
namespace {

struct ParsedModuleSet final {
    std::vector<Elf32LinkModule> links;
    std::vector<Elf32LoadedModule> loaded;
};

[[nodiscard]] ParsedModuleSet ParseModules(
    const std::span<const Elf32ModuleInput> inputs) {
    ParsedModuleSet result;
    result.links.reserve(inputs.size());
    result.loaded.reserve(inputs.size());
    for (const auto& input : inputs) {
        if (input.name.empty() || input.bytes.empty()) {
            throw LinkError("ELF module input requires name and bytes");
        }
        auto image = ParseElf32Arm(input.bytes);
        auto dynamic = ReadElf32DynamicInfo(input.bytes, image);
        auto symbols = ReadElf32SymbolTable(input.bytes, image);
        auto relocations = ReadElf32Relocations(input.bytes, image, symbols);
        auto lifecycle = ReadElf32LifecycleInfo(input.bytes, image);
        auto tls = ReadElf32TlsInfo(input.bytes, image);
        auto versions = ReadElf32SymbolVersions(
            input.bytes, image, dynamic, symbols);
        result.links.push_back({input.name, input.load_bias,
                                std::move(dynamic), std::move(symbols),
                                std::move(versions), {}});
        result.loaded.push_back({std::move(image),
                                 {memory::GuestAddress{0}, std::nullopt, {}},
                                 std::move(relocations),
                                 std::move(lifecycle), std::move(tls)});
    }
    return result;
}

}  // namespace

Elf32LoadedNamespace LoadElf32ModuleNamespace(
    const std::string_view root_name,
    const std::span<const Elf32ModuleInput> inputs,
    memory::AddressSpace& address_space,
    const Elf32LinkNamespaceBuilder& namespace_builder) {
    if (inputs.empty()) throw LinkError("ELF module input set is empty");
    auto parsed = ParseModules(inputs);
    Elf32LoadedNamespace result;
    result.modules = std::move(parsed.loaded);
    result.link_namespace = namespace_builder
                                ? namespace_builder(root_name, parsed.links)
                                : BuildElf32LinkNamespace(root_name, parsed.links);
    if (result.link_namespace.modules.size() < inputs.size()) {
        throw LinkError("ELF namespace builder removed guest modules");
    }
    for (std::size_t index = 0; index < inputs.size(); ++index) {
        const auto& module = result.link_namespace.modules[index];
        if (module.name != inputs[index].name ||
            module.load_bias != inputs[index].load_bias) {
            throw LinkError(
                "ELF namespace builder reordered or replaced a guest module");
        }
    }

    const auto snapshot = address_space.CaptureSnapshot();
    try {
        for (const auto index : result.link_namespace.load_order) {
            if (index >= inputs.size()) continue;
            result.modules[index].load_plan = LoadElf32Arm(
                inputs[index].bytes, result.modules[index].image,
                inputs[index].load_bias, address_space);
            auto& ranges = result.link_namespace.modules[index].load_ranges;
            ranges.reserve(result.modules[index].load_plan.regions.size());
            for (const auto& region :
                 result.modules[index].load_plan.regions) {
                ranges.push_back(region.range);
            }
        }
        for (const auto index : result.link_namespace.load_order) {
            if (index >= inputs.size()) continue;
            const auto resolved = ResolveElf32Symbols(
                result.link_namespace, index);
            ApplyElf32ArmRelocations(
                result.modules[index].relocations, resolved,
                inputs[index].load_bias, result.modules[index].load_plan,
                address_space);
        }
    } catch (...) {
        address_space.RestoreSnapshot(snapshot);
        throw;
    }
    return result;
}

Elf32LoadedModuleExtension ExtendElf32ModuleNamespace(
    const Elf32LinkNamespace& link_namespace,
    const std::string_view root_name,
    const std::span<const Elf32ModuleInput> inputs,
    memory::AddressSpace& address_space,
    const Elf32LinkNamespaceExtender& namespace_extender) {
    if (inputs.empty()) {
        auto extension = namespace_extender
                             ? namespace_extender(link_namespace, root_name, {})
                             : ExtendElf32LinkNamespace(
                                   link_namespace, root_name, {});
        return {std::move(extension), {}};
    }
    auto parsed = ParseModules(inputs);
    auto extension = namespace_extender
                         ? namespace_extender(
                               link_namespace, root_name, parsed.links)
                         : ExtendElf32LinkNamespace(
                               link_namespace, root_name, parsed.links);
    const auto old_size = link_namespace.modules.size();
    if (extension.link_namespace.modules.size() < old_size + inputs.size()) {
        throw LinkError("ELF namespace extender removed guest modules");
    }
    for (std::size_t index = 0; index < inputs.size(); ++index) {
        const auto& module = extension.link_namespace.modules[old_size + index];
        if (module.name != inputs[index].name ||
            module.load_bias != inputs[index].load_bias) {
            throw LinkError(
                "ELF namespace extender reordered or replaced a guest module");
        }
    }

    const auto snapshot = address_space.CaptureSnapshot();
    try {
        for (const auto namespace_index : extension.scope.load_order) {
            if (namespace_index < old_size ||
                namespace_index >= old_size + inputs.size()) {
                continue;
            }
            const auto input_index = namespace_index - old_size;
            parsed.loaded[input_index].load_plan = LoadElf32Arm(
                inputs[input_index].bytes,
                parsed.loaded[input_index].image,
                inputs[input_index].load_bias, address_space);
            auto& ranges =
                extension.link_namespace.modules[namespace_index].load_ranges;
            for (const auto& region :
                 parsed.loaded[input_index].load_plan.regions) {
                ranges.push_back(region.range);
            }
        }
        for (const auto namespace_index : extension.scope.load_order) {
            if (namespace_index < old_size ||
                namespace_index >= old_size + inputs.size()) {
                continue;
            }
            const auto input_index = namespace_index - old_size;
            const auto resolved = ResolveElf32Symbols(
                extension.link_namespace, extension.scope, namespace_index);
            ApplyElf32ArmRelocations(
                parsed.loaded[input_index].relocations, resolved,
                inputs[input_index].load_bias,
                parsed.loaded[input_index].load_plan, address_space);
        }
    } catch (...) {
        address_space.RestoreSnapshot(snapshot);
        throw;
    }
    return {std::move(extension), std::move(parsed.loaded)};
}

}  // namespace ogplay::loader
