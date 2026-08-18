#pragma once

#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "ogplay/loader/lifecycle.h"
#include "ogplay/loader/link_namespace.h"
#include "ogplay/loader/tls.h"

namespace ogplay::loader {

struct Elf32ModuleInput final {
    std::string name;
    std::span<const std::byte> bytes;
    memory::GuestAddress load_bias;
};

struct Elf32LoadedModule final {
    Elf32Image image;
    Elf32LoadPlan load_plan{memory::GuestAddress{0}, std::nullopt, {}};
    Elf32RelocationTable relocations;
    Elf32LifecycleInfo lifecycle;
    std::optional<Elf32TlsInfo> tls;
};

struct Elf32LoadedNamespace final {
    Elf32LinkNamespace link_namespace;
    std::vector<Elf32LoadedModule> modules;
};

struct Elf32LoadedModuleExtension final {
    Elf32LinkNamespaceExtension link_extension;
    std::vector<Elf32LoadedModule> modules;
};

using Elf32LinkNamespaceBuilder = std::function<Elf32LinkNamespace(
    std::string_view, std::span<const Elf32LinkModule>)>;
using Elf32LinkNamespaceExtender = std::function<Elf32LinkNamespaceExtension(
    const Elf32LinkNamespace&, std::string_view,
    std::span<const Elf32LinkModule>)>;

[[nodiscard]] Elf32LoadedNamespace LoadElf32ModuleNamespace(
    std::string_view root_name, std::span<const Elf32ModuleInput> inputs,
    memory::AddressSpace& address_space,
    const Elf32LinkNamespaceBuilder& namespace_builder = {});
[[nodiscard]] Elf32LoadedModuleExtension ExtendElf32ModuleNamespace(
    const Elf32LinkNamespace& link_namespace, std::string_view root_name,
    std::span<const Elf32ModuleInput> inputs,
    memory::AddressSpace& address_space,
    const Elf32LinkNamespaceExtender& namespace_extender = {});

}  // namespace ogplay::loader
