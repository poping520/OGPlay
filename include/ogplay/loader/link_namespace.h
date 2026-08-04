#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "ogplay/loader/relocation.h"
#include "ogplay/loader/symbol_version.h"

namespace ogplay::loader {

struct Elf32LinkModule final {
    std::string name;
    memory::GuestAddress load_bias;
    Elf32DynamicInfo dynamic;
    Elf32SymbolTable symbols;
    std::optional<Elf32SymbolVersionTable> versions;
    std::vector<memory::GuestRange> load_ranges;
};

struct Elf32LinkNamespace final {
    std::vector<Elf32LinkModule> modules;
    std::vector<std::size_t> load_order;
    std::vector<std::size_t> lookup_scope;
};

struct Elf32SymbolLocation final {
    std::size_t module_index{};
    std::size_t symbol_index{};
    memory::GuestAddress address;
};

struct Elf32LinkScope final {
    std::size_t root_module{};
    std::vector<std::size_t> load_order;
    std::vector<std::size_t> lookup_scope;
};

struct Elf32LinkNamespaceExtension final {
    Elf32LinkNamespace link_namespace;
    Elf32LinkScope scope;
    std::vector<std::size_t> newly_loaded;
};

class LinkError final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

[[nodiscard]] Elf32LinkNamespace BuildElf32LinkNamespace(
    std::string_view root_name, std::span<const Elf32LinkModule> modules);
[[nodiscard]] Elf32LinkNamespaceExtension ExtendElf32LinkNamespace(
    const Elf32LinkNamespace& link_namespace, std::string_view root_name,
    std::span<const Elf32LinkModule> new_modules);
[[nodiscard]] Elf32SymbolLocation LookupElf32Symbol(
    const Elf32LinkNamespace& link_namespace, std::string_view name);
[[nodiscard]] Elf32SymbolLocation LookupElf32Symbol(
    const Elf32LinkNamespace& link_namespace, const Elf32LinkScope& scope,
    std::string_view name,
    std::optional<std::string_view> version = std::nullopt,
    std::optional<std::string_view> dependency = std::nullopt);
[[nodiscard]] Elf32ResolvedSymbols ResolveElf32Symbols(
    const Elf32LinkNamespace& link_namespace, std::size_t module_index);
[[nodiscard]] Elf32ResolvedSymbols ResolveElf32Symbols(
    const Elf32LinkNamespace& link_namespace, const Elf32LinkScope& scope,
    std::size_t module_index);

}  // namespace ogplay::loader
