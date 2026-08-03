#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "ogplay/loader/link_namespace.h"

namespace ogplay::loader {

using Elf32DynamicHandle = std::uint32_t;

struct Elf32DynamicOpenResult final {
    Elf32DynamicHandle handle{};
    std::uint32_t reference_count{};
    std::vector<std::size_t> initialization_order;
};

struct Elf32DynamicCloseResult final {
    std::uint32_t reference_count{};
    std::vector<std::size_t> finalization_order;
};

class Elf32DynamicLinker final {
public:
    explicit Elf32DynamicLinker(Elf32LinkNamespace link_namespace);

    [[nodiscard]] Elf32DynamicOpenResult Open(
        std::string_view root_name,
        std::span<const Elf32LinkModule> new_modules = {});
    [[nodiscard]] Elf32SymbolLocation Symbol(
        Elf32DynamicHandle handle, std::string_view name) const;
    [[nodiscard]] Elf32DynamicCloseResult Close(Elf32DynamicHandle handle);

    [[nodiscard]] const Elf32LinkNamespace& LinkNamespace() const noexcept;

private:
    struct HandleState final {
        Elf32DynamicHandle handle{};
        std::size_t root_module{};
        std::uint32_t reference_count{};
        Elf32LinkScope scope;
        bool open{};
    };

    [[nodiscard]] HandleState* FindOpenRoot(std::string_view root_name);
    [[nodiscard]] const HandleState& RequireHandle(
        Elf32DynamicHandle handle) const;
    [[nodiscard]] HandleState& RequireHandle(Elf32DynamicHandle handle);

    Elf32LinkNamespace link_namespace_;
    std::size_t base_module_count_{};
    Elf32DynamicHandle next_handle_{1};
    std::vector<std::uint32_t> module_references_;
    std::vector<HandleState> handles_;
};

}  // namespace ogplay::loader
