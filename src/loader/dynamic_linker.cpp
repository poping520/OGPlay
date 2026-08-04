#include "ogplay/loader/dynamic_linker.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ogplay::loader {
namespace {

[[nodiscard]] bool HasName(const Elf32LinkModule& module,
                           const std::string_view name) {
    return module.name == name ||
           (module.dynamic.soname.has_value() &&
            *module.dynamic.soname == name);
}

}  // namespace

Elf32DynamicLinker::Elf32DynamicLinker(
    Elf32LinkNamespace link_namespace)
    : link_namespace_(std::move(link_namespace)),
      base_module_count_(link_namespace_.modules.size()),
      module_references_(base_module_count_) {
    if (link_namespace_.modules.empty()) {
        throw LinkError("dynamic linker requires a base ELF namespace");
    }
    for (const auto index : link_namespace_.load_order) {
        if (index >= base_module_count_) {
            throw LinkError("base ELF load order contains an invalid module index");
        }
    }
    for (const auto index : link_namespace_.lookup_scope) {
        if (index >= base_module_count_) {
            throw LinkError("base ELF lookup scope contains an invalid module index");
        }
    }
}

Elf32DynamicLinker::HandleState* Elf32DynamicLinker::FindOpenRoot(
    const std::string_view root_name) {
    for (auto& state : handles_) {
        if (state.open &&
            HasName(link_namespace_.modules[state.root_module], root_name)) {
            return &state;
        }
    }
    return nullptr;
}

const Elf32DynamicLinker::HandleState& Elf32DynamicLinker::RequireHandle(
    const Elf32DynamicHandle handle) const {
    const auto found = std::find_if(
        handles_.begin(), handles_.end(), [&](const HandleState& state) {
            return state.open && state.handle == handle;
        });
    if (found == handles_.end()) {
        throw LinkError("dynamic ELF handle is invalid or closed");
    }
    return *found;
}

Elf32DynamicLinker::HandleState& Elf32DynamicLinker::RequireHandle(
    const Elf32DynamicHandle handle) {
    return const_cast<HandleState&>(
        std::as_const(*this).RequireHandle(handle));
}

Elf32DynamicOpenResult Elf32DynamicLinker::Open(
    const std::string_view root_name,
    const std::span<const Elf32LinkModule> new_modules) {
    if (auto* open = FindOpenRoot(root_name); open != nullptr) {
        if (!new_modules.empty()) {
            throw LinkError("reopening a dynamic ELF root cannot add modules");
        }
        if (open->reference_count == std::numeric_limits<std::uint32_t>::max()) {
            throw LinkError("dynamic ELF handle reference count overflow");
        }
        ++open->reference_count;
        return {open->handle, open->reference_count, {}};
    }
    if (next_handle_ == 0) {
        throw LinkError("dynamic ELF handle space is exhausted");
    }

    auto extension = ExtendElf32LinkNamespace(
        link_namespace_, root_name, new_modules);
    auto references = module_references_;
    references.resize(extension.link_namespace.modules.size());
    std::vector<std::size_t> initialization_order;
    for (const auto index : extension.scope.load_order) {
        if (index < base_module_count_) continue;
        if (references[index] == std::numeric_limits<std::uint32_t>::max()) {
            throw LinkError("dynamic ELF module reference count overflow");
        }
        if (references[index] == 0) initialization_order.push_back(index);
        ++references[index];
    }

    const auto handle = next_handle_++;
    link_namespace_ = std::move(extension.link_namespace);
    module_references_ = std::move(references);
    handles_.push_back(
        {handle, extension.scope.root_module, 1,
         std::move(extension.scope), true});
    return {handle, 1, std::move(initialization_order)};
}

Elf32SymbolLocation Elf32DynamicLinker::Symbol(
    const Elf32DynamicHandle handle, const std::string_view name) const {
    if (name.empty()) throw LinkError("dlsym requires a non-empty symbol name");
    const auto& state = RequireHandle(handle);
    return LookupElf32Symbol(link_namespace_, state.scope, name);
}

Elf32DynamicAddressInfo Elf32DynamicLinker::Address(
    const memory::GuestAddress address) const {
    std::optional<std::size_t> module_index;
    for (std::size_t index = 0; index < link_namespace_.modules.size(); ++index) {
        const auto active = index < base_module_count_ ||
                            (index < module_references_.size() &&
                             module_references_[index] != 0);
        if (!active) continue;
        const auto& module = link_namespace_.modules[index];
        const auto contains = std::any_of(
            module.load_ranges.begin(), module.load_ranges.end(),
            [address](const memory::GuestRange& range) {
                return range.Contains(address);
            });
        if (!contains) continue;
        if (module_index.has_value()) {
            throw LinkError("dladdr address belongs to multiple ELF modules");
        }
        module_index = index;
    }
    if (!module_index.has_value()) {
        throw LinkError("dladdr address is outside loaded ELF modules");
    }

    const auto& module = link_namespace_.modules[*module_index];
    std::optional<std::pair<std::string, memory::GuestAddress>> nearest;
    for (const auto& symbol : module.symbols.symbols) {
        if (symbol.name.empty() || symbol.section_index == 0) continue;
        constexpr std::uint16_t kSectionAbsolute = 0xfff1;
        const auto value = symbol.section_index == kSectionAbsolute
                               ? symbol.value.Value()
                               : static_cast<std::uint64_t>(
                                     module.load_bias.Value()) +
                                     symbol.value.Value();
        if (value > std::numeric_limits<std::uint32_t>::max()) {
            throw LinkError("dladdr symbol wraps the guest address space");
        }
        const memory::GuestAddress runtime{
            static_cast<std::uint32_t>(value)};
        if (!std::any_of(module.load_ranges.begin(),
                         module.load_ranges.end(),
                         [runtime](const memory::GuestRange& range) {
                             return range.Contains(runtime);
                         })) {
            continue;
        }
        if (runtime > address ||
            (nearest.has_value() && runtime < nearest->second)) {
            continue;
        }
        nearest = std::pair{symbol.name, runtime};
    }
    return {*module_index,
            module.name,
            module.load_bias,
            nearest.has_value() ? nearest->first : std::string{},
            nearest.has_value() ? nearest->second
                                : memory::GuestAddress{0}};
}

Elf32DynamicCloseResult Elf32DynamicLinker::Close(
    const Elf32DynamicHandle handle) {
    auto& state = RequireHandle(handle);
    if (state.reference_count > 1) {
        --state.reference_count;
        return {state.reference_count, {}};
    }

    auto references = module_references_;
    std::vector<std::size_t> finalization_order;
    for (auto item = state.scope.load_order.rbegin();
         item != state.scope.load_order.rend(); ++item) {
        const auto index = *item;
        if (index < base_module_count_) continue;
        if (index >= references.size() || references[index] == 0) {
            throw LinkError("dynamic ELF module reference state is inconsistent");
        }
        --references[index];
        if (references[index] == 0) finalization_order.push_back(index);
    }
    module_references_ = std::move(references);
    state.reference_count = 0;
    state.open = false;
    return {0, std::move(finalization_order)};
}

const Elf32LinkNamespace& Elf32DynamicLinker::LinkNamespace() const noexcept {
    return link_namespace_;
}

}  // namespace ogplay::loader
