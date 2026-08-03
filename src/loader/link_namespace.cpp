#include "ogplay/loader/link_namespace.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <optional>
#include <queue>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace ogplay::loader {
namespace {

[[nodiscard]] std::string_view CanonicalName(const Elf32LinkModule& module) {
    if (module.dynamic.soname.has_value()) return *module.dynamic.soname;
    return module.name;
}

[[nodiscard]] memory::GuestAddress RuntimeSymbolAddress(
    const Elf32LinkModule& module, const Elf32Symbol& symbol) {
    constexpr std::uint16_t kSectionAbsolute = 0xfff1;
    if (symbol.section_index == kSectionAbsolute) return symbol.value;
    const auto value = static_cast<std::uint64_t>(symbol.value.Value()) +
                       module.load_bias.Value();
    if (value > std::numeric_limits<std::uint32_t>::max()) {
        throw LinkError("symbol address wraps the guest address space");
    }
    return memory::GuestAddress{static_cast<std::uint32_t>(value)};
}

[[nodiscard]] std::size_t FindModule(
    const std::map<std::string, std::size_t, std::less<>>& names,
    const std::string_view name) {
    const auto found = names.find(name);
    if (found == names.end()) {
        throw LinkError("required ELF module is unavailable: " +
                        std::string(name));
    }
    return found->second;
}

[[nodiscard]] std::optional<Elf32SymbolLocation> TryLookup(
    const Elf32LinkNamespace& link_namespace, const std::string_view name) {
    if (name.empty()) return std::nullopt;
    for (const auto module_index : link_namespace.lookup_scope) {
        const auto& module = link_namespace.modules[module_index];
        for (std::size_t symbol_index = 1;
             symbol_index < module.symbols.symbols.size(); ++symbol_index) {
            const auto& symbol = module.symbols.symbols[symbol_index];
            if (symbol.name == name && symbol.IsExported()) {
                return Elf32SymbolLocation{
                    module_index, symbol_index,
                    RuntimeSymbolAddress(module, symbol)};
            }
        }
    }
    return std::nullopt;
}

}  // namespace

Elf32LinkNamespace BuildElf32LinkNamespace(
    const std::string_view root_name,
    const std::span<const Elf32LinkModule> modules) {
    if (root_name.empty()) throw LinkError("ELF root module name is empty");
    if (modules.empty()) throw LinkError("ELF link namespace has no modules");

    Elf32LinkNamespace result;
    result.modules.assign(modules.begin(), modules.end());
    std::map<std::string, std::size_t, std::less<>> names;
    for (std::size_t index = 0; index < result.modules.size(); ++index) {
        const auto& module = result.modules[index];
        if (module.name.empty()) throw LinkError("ELF module name is empty");
        const auto add_name = [&](const std::string_view name) {
            const auto [iterator, inserted] =
                names.emplace(std::string(name), index);
            if (!inserted && iterator->second != index) {
                throw LinkError("ELF module name or soname is ambiguous: " +
                                std::string(name));
            }
        };
        add_name(module.name);
        add_name(CanonicalName(module));
    }
    const auto root = FindModule(names, root_name);

    enum class Visit : std::uint8_t { unseen, active, complete };
    std::vector<Visit> visits(result.modules.size(), Visit::unseen);
    std::function<void(std::size_t)> visit = [&](const std::size_t index) {
        if (visits[index] == Visit::complete) return;
        if (visits[index] == Visit::active) return;
        visits[index] = Visit::active;
        for (const auto& needed : result.modules[index].dynamic.needed) {
            visit(FindModule(names, needed));
        }
        visits[index] = Visit::complete;
        result.load_order.push_back(index);
    };
    visit(root);

    std::queue<std::size_t> pending;
    std::set<std::size_t> queued;
    pending.push(root);
    queued.insert(root);
    while (!pending.empty()) {
        const auto index = pending.front();
        pending.pop();
        result.lookup_scope.push_back(index);
        for (const auto& needed : result.modules[index].dynamic.needed) {
            const auto dependency = FindModule(names, needed);
            if (queued.insert(dependency).second) pending.push(dependency);
        }
    }
    return result;
}

Elf32SymbolLocation LookupElf32Symbol(
    const Elf32LinkNamespace& link_namespace, const std::string_view name) {
    const auto result = TryLookup(link_namespace, name);
    if (!result.has_value()) {
        throw LinkError("ELF symbol is unresolved: " + std::string(name));
    }
    return *result;
}

Elf32ResolvedSymbols ResolveElf32Symbols(
    const Elf32LinkNamespace& link_namespace,
    const std::size_t module_index) {
    if (module_index >= link_namespace.modules.size()) {
        throw LinkError("ELF module index is outside the link namespace");
    }
    const auto& module = link_namespace.modules[module_index];
    Elf32ResolvedSymbols result;
    result.values.resize(module.symbols.symbols.size());
    for (std::size_t index = 1; index < module.symbols.symbols.size(); ++index) {
        const auto& symbol = module.symbols.symbols[index];
        const bool locally_bound = symbol.section_index != 0 &&
                                   (symbol.binding == 0 ||
                                    symbol.visibility == 1 ||
                                    symbol.visibility == 2 ||
                                    symbol.visibility == 3);
        if (locally_bound) {
            result.values[index] = RuntimeSymbolAddress(module, symbol);
            continue;
        }
        const auto resolved = TryLookup(link_namespace, symbol.name);
        if (resolved.has_value()) {
            result.values[index] = resolved->address;
        } else if (symbol.section_index != 0) {
            result.values[index] = RuntimeSymbolAddress(module, symbol);
        } else if (symbol.binding == 2) {
            result.values[index] = memory::GuestAddress{};
        } else {
            throw LinkError("strong ELF symbol is unresolved: " + symbol.name);
        }
    }
    return result;
}

}  // namespace ogplay::loader
