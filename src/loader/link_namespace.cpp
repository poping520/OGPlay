#include "ogplay/loader/link_namespace.h"

#include <algorithm>
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

using ModuleNames = std::map<std::string, std::size_t, std::less<>>;

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

[[nodiscard]] ModuleNames IndexModuleNames(
    const std::span<const Elf32LinkModule> modules) {
    ModuleNames names;
    for (std::size_t index = 0; index < modules.size(); ++index) {
        const auto& module = modules[index];
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
        if (module.versions.has_value() &&
            module.versions->symbols.size() != module.symbols.symbols.size()) {
            throw LinkError("ELF symbol version table size disagrees with dynsym");
        }
    }
    return names;
}

[[nodiscard]] std::size_t FindModule(const ModuleNames& names,
                                     const std::string_view name) {
    const auto found = names.find(name);
    if (found == names.end()) {
        throw LinkError("required ELF module is unavailable: " +
                        std::string(name));
    }
    return found->second;
}

[[nodiscard]] Elf32LinkScope BuildScope(
    const std::span<const Elf32LinkModule> modules, const ModuleNames& names,
    const std::string_view root_name) {
    if (root_name.empty()) throw LinkError("ELF root module name is empty");
    const auto root = FindModule(names, root_name);
    Elf32LinkScope scope;
    scope.root_module = root;

    enum class Visit : std::uint8_t { unseen, active, complete };
    std::vector<Visit> visits(modules.size(), Visit::unseen);
    std::function<void(std::size_t)> visit = [&](const std::size_t index) {
        if (visits[index] == Visit::complete) return;
        if (visits[index] == Visit::active) return;
        visits[index] = Visit::active;
        for (const auto& needed : modules[index].dynamic.needed) {
            visit(FindModule(names, needed));
        }
        visits[index] = Visit::complete;
        scope.load_order.push_back(index);
    };
    visit(root);

    std::queue<std::size_t> pending;
    std::set<std::size_t> queued;
    pending.push(root);
    queued.insert(root);
    while (!pending.empty()) {
        const auto index = pending.front();
        pending.pop();
        scope.lookup_scope.push_back(index);
        for (const auto& needed : modules[index].dynamic.needed) {
            const auto dependency = FindModule(names, needed);
            if (queued.insert(dependency).second) pending.push(dependency);
        }
    }
    return scope;
}

[[nodiscard]] bool MatchesVersion(
    const Elf32LinkModule& module, const std::size_t symbol_index,
    const std::optional<std::string_view> version) {
    // Android API 19/22 providers predate symbol version definitions, while
    // newer NDKs can still attach LIBC requirements to binaries targeting
    // those releases. The platform linker resolves such imports by name.
    if (!module.versions.has_value()) return true;
    const auto& candidate = module.versions->symbols[symbol_index];
    if (version.has_value()) {
        return candidate.kind == Elf32SymbolVersionKind::definition &&
               candidate.name == *version;
    }
    return candidate.kind == Elf32SymbolVersionKind::global ||
           (candidate.kind == Elf32SymbolVersionKind::definition &&
            !candidate.hidden);
}

[[nodiscard]] std::optional<Elf32SymbolLocation> TryLookup(
    const Elf32LinkNamespace& link_namespace,
    const std::span<const std::size_t> lookup_scope,
    const std::string_view name,
    const std::optional<std::string_view> version = std::nullopt,
    const std::optional<std::string_view> dependency = std::nullopt) {
    if (name.empty()) return std::nullopt;
    for (const auto module_index : lookup_scope) {
        if (module_index >= link_namespace.modules.size()) {
            throw LinkError("ELF lookup scope contains an invalid module index");
        }
        const auto& module = link_namespace.modules[module_index];
        if (dependency.has_value() && CanonicalName(module) != *dependency) {
            continue;
        }
        for (std::size_t symbol_index = 1;
             symbol_index < module.symbols.symbols.size(); ++symbol_index) {
            const auto& symbol = module.symbols.symbols[symbol_index];
            if (symbol.name == name && symbol.IsExported() &&
                MatchesVersion(module, symbol_index, version)) {
                return Elf32SymbolLocation{
                    module_index, symbol_index,
                    RuntimeSymbolAddress(module, symbol)};
            }
        }
    }
    return std::nullopt;
}

[[nodiscard]] Elf32ResolvedSymbols ResolveInScope(
    const Elf32LinkNamespace& link_namespace,
    const std::span<const std::size_t> lookup_scope,
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

        std::optional<std::string_view> version;
        std::optional<std::string_view> dependency;
        if (module.versions.has_value() && symbol.section_index == 0) {
            const auto& required = module.versions->symbols[index];
            if (required.kind == Elf32SymbolVersionKind::requirement) {
                version = required.name;
                dependency = required.dependency;
            } else if (required.kind != Elf32SymbolVersionKind::global &&
                       required.kind != Elf32SymbolVersionKind::local) {
                throw LinkError(
                    "undefined ELF symbol has an invalid version kind: " +
                    symbol.name + " (" +
                    std::to_string(static_cast<std::uint8_t>(required.kind)) +
                    ")");
            }
        }
        const auto resolved = TryLookup(link_namespace, lookup_scope,
                                        symbol.name, version, dependency);
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

}  // namespace

Elf32LinkNamespace BuildElf32LinkNamespace(
    const std::string_view root_name,
    const std::span<const Elf32LinkModule> modules) {
    if (modules.empty()) throw LinkError("ELF link namespace has no modules");
    Elf32LinkNamespace result;
    result.modules.assign(modules.begin(), modules.end());
    const auto names = IndexModuleNames(result.modules);
    const auto scope = BuildScope(result.modules, names, root_name);
    if (scope.lookup_scope.size() != result.modules.size()) {
        throw LinkError("ELF link namespace contains unreachable modules");
    }
    result.load_order = scope.load_order;
    result.lookup_scope = scope.lookup_scope;
    return result;
}

Elf32LinkNamespaceExtension ExtendElf32LinkNamespace(
    const Elf32LinkNamespace& link_namespace, const std::string_view root_name,
    const std::span<const Elf32LinkModule> new_modules) {
    if (link_namespace.modules.empty()) {
        throw LinkError("cannot extend an empty ELF link namespace");
    }
    Elf32LinkNamespaceExtension result;
    result.link_namespace = link_namespace;
    const auto old_size = result.link_namespace.modules.size();
    result.link_namespace.modules.insert(result.link_namespace.modules.end(),
                                         new_modules.begin(), new_modules.end());
    const auto names = IndexModuleNames(result.link_namespace.modules);
    result.scope = BuildScope(result.link_namespace.modules, names, root_name);
    for (std::size_t index = old_size;
         index < result.link_namespace.modules.size(); ++index) {
        if (std::find(result.scope.lookup_scope.begin(),
                      result.scope.lookup_scope.end(), index) ==
            result.scope.lookup_scope.end()) {
            throw LinkError("dynamic ELF extension contains an unreachable module");
        }
    }
    for (const auto index : result.scope.load_order) {
        if (index >= old_size) result.newly_loaded.push_back(index);
    }
    return result;
}

Elf32SymbolLocation LookupElf32Symbol(
    const Elf32LinkNamespace& link_namespace, const std::string_view name) {
    const auto result = TryLookup(link_namespace, link_namespace.lookup_scope,
                                  name);
    if (!result.has_value()) {
        throw LinkError("ELF symbol is unresolved: " + std::string(name));
    }
    return *result;
}

Elf32SymbolLocation LookupElf32Symbol(
    const Elf32LinkNamespace& link_namespace, const Elf32LinkScope& scope,
    const std::string_view name,
    const std::optional<std::string_view> version,
    const std::optional<std::string_view> dependency) {
    const auto result = TryLookup(link_namespace, scope.lookup_scope, name,
                                  version, dependency);
    if (!result.has_value()) {
        throw LinkError("ELF symbol is unresolved: " + std::string(name));
    }
    return *result;
}

Elf32ResolvedSymbols ResolveElf32Symbols(
    const Elf32LinkNamespace& link_namespace,
    const std::size_t module_index) {
    return ResolveInScope(link_namespace, link_namespace.lookup_scope,
                          module_index);
}

Elf32ResolvedSymbols ResolveElf32Symbols(
    const Elf32LinkNamespace& link_namespace, const Elf32LinkScope& scope,
    const std::size_t module_index) {
    return ResolveInScope(link_namespace, scope.lookup_scope, module_index);
}

}  // namespace ogplay::loader
