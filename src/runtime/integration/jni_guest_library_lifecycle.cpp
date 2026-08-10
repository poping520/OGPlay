#include "ogplay/runtime/integration/jni_guest_library_lifecycle.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

#include "ogplay/runtime/jni/jni.h"

namespace ogplay::runtime {
namespace {

[[nodiscard]] bool MatchesRoot(const loader::Elf32LinkModule& module,
                               const std::string_view root) {
    return module.name == root ||
           (module.dynamic.soname.has_value() &&
            *module.dynamic.soname == root);
}

[[nodiscard]] memory::GuestAddress RuntimeAddress(
    const loader::Elf32LinkModule& module,
    const loader::Elf32Symbol& symbol) {
    constexpr std::uint16_t kSectionAbsolute = 0xfff1U;
    if (symbol.section_index == kSectionAbsolute) return symbol.value;
    const auto value = static_cast<std::uint64_t>(module.load_bias.Value()) +
                       symbol.value.Value();
    if (value > std::numeric_limits<std::uint32_t>::max()) {
        throw std::runtime_error(
            "JNI_OnLoad address wraps the guest address space");
    }
    return memory::GuestAddress{static_cast<std::uint32_t>(value)};
}

}  // namespace

std::optional<JniGuestLibraryOnLoad> BuildJniGuestLibraryOnLoad(
    const loader::Elf32LinkNamespace& link_namespace,
    const std::string_view root_module,
    const memory::GuestAddress java_vm) {
    if (root_module.empty() || java_vm.IsNull()) {
        throw std::invalid_argument(
            "guest JNI library lifecycle request is incomplete");
    }
    std::optional<std::size_t> root;
    for (std::size_t index = 0; index < link_namespace.modules.size(); ++index) {
        if (!MatchesRoot(link_namespace.modules[index], root_module)) continue;
        if (root.has_value() && *root != index) {
            throw std::runtime_error(
                "guest JNI root module identity is ambiguous");
        }
        root = index;
    }
    if (!root.has_value()) {
        throw std::runtime_error(
            "guest JNI root module is absent from link namespace");
    }

    const auto& module = link_namespace.modules[*root];
    std::optional<memory::GuestAddress> target;
    for (const auto& symbol : module.symbols.symbols) {
        if (symbol.name != "JNI_OnLoad" || !symbol.IsExported() ||
            symbol.section_index == 0U) {
            continue;
        }
        if (symbol.type != 2U) {
            throw std::runtime_error(
                "root JNI_OnLoad export is not a function");
        }
        if (target.has_value()) {
            throw std::runtime_error(
                "root module exports duplicate JNI_OnLoad symbols");
        }
        target = RuntimeAddress(module, symbol);
    }
    if (!target.has_value()) return std::nullopt;
    return JniGuestLibraryOnLoad{
        *root, module.name,
        A32GuestCallFrame{*target, {java_vm.Value(), 0U, 0U, 0U}, {}}};
}

void ValidateJniGuestLibraryOnLoadResult(const std::uint32_t result) {
    constexpr std::uint32_t kJniVersion1_1 = 0x00010001U;
    constexpr std::uint32_t kJniVersion1_2 = 0x00010002U;
    constexpr std::uint32_t kJniVersion1_4 = 0x00010004U;
    constexpr std::uint32_t kJniVersion1_6Bits =
        static_cast<std::uint32_t>(kJniVersion1_6);
    if (result != kJniVersion1_1 && result != kJniVersion1_2 &&
        result != kJniVersion1_4 && result != kJniVersion1_6Bits) {
        throw std::runtime_error(
            "guest JNI_OnLoad returned an unsupported JNI version");
    }
}

}  // namespace ogplay::runtime
