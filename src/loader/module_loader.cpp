#include "ogplay/loader/module_loader.h"

#include <cstddef>
#include <utility>
#include <vector>

#include "ogplay/loader/symbol_version.h"

namespace ogplay::loader {

Elf32LoadedNamespace LoadElf32ModuleNamespace(
    const std::string_view root_name,
    const std::span<const Elf32ModuleInput> inputs,
    memory::AddressSpace& address_space) {
    if (inputs.empty()) throw LinkError("ELF module input set is empty");
    std::vector<Elf32LinkModule> link_modules;
    link_modules.reserve(inputs.size());
    Elf32LoadedNamespace result;
    result.modules.reserve(inputs.size());
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
        link_modules.push_back({input.name, input.load_bias,
                                std::move(dynamic), std::move(symbols),
                                std::move(versions)});
        result.modules.push_back({std::move(image),
                                  {memory::GuestAddress{0}, std::nullopt, {}},
                                  std::move(relocations),
                                  std::move(lifecycle), std::move(tls)});
    }
    result.link_namespace = BuildElf32LinkNamespace(root_name, link_modules);

    const auto snapshot = address_space.CaptureSnapshot();
    try {
        for (const auto index : result.link_namespace.load_order) {
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

}  // namespace ogplay::loader
