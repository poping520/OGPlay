#include "ogplay/runtime/bionic_selfcheck.h"

#include <array>
#include <cstddef>
#include <string_view>

namespace ogplay::runtime {

BionicSelfCheckReport SelfCheckBionicProfile(
    const std::uint32_t api,
    const std::span<const loader::Elf32ModuleInput> inputs,
    memory::AddressSpace& address_space) {
    const auto& profile = SelectBionicProfile(api);
    bool has_libc{};
    bool has_libdl{};
    for (const auto& input : inputs) {
        has_libc = has_libc || input.name == "libc.so";
        has_libdl = has_libdl || input.name == "libdl.so";
    }
    if (!has_libc || !has_libdl) {
        throw BionicProfileError(
            "Bionic self-check requires libc.so and libdl.so");
    }
    const auto loaded = loader::LoadElf32ModuleNamespace(
        "libc.so", inputs, address_space);
    constexpr std::array<std::string_view, 8> kRequiredLibcSymbols{
        "malloc", "free", "open", "read", "write", "close",
        "pthread_create", "pthread_join"};
    for (const auto symbol : kRequiredLibcSymbols) {
        try {
            static_cast<void>(loader::LookupElf32Symbol(
                loaded.link_namespace, symbol));
        } catch (const loader::LinkError&) {
            throw BionicProfileError(
                "Bionic libc self-check is missing symbol: " +
                std::string(symbol));
        }
    }

    BionicSelfCheckReport report;
    report.api = profile.api;
    report.module_count = loaded.modules.size();
    for (std::size_t index = 0; index < loaded.modules.size(); ++index) {
        report.symbol_count +=
            loaded.link_namespace.modules[index].symbols.symbols.size();
        report.relocation_count +=
            loaded.modules[index].relocations.relocations.size();
        if (loaded.link_namespace.modules[index].versions.has_value()) {
            report.versioned_symbol_count += loaded.link_namespace
                                                   .modules[index]
                                                   .versions->symbols.size();
        }
        const auto& lifecycle = loaded.modules[index].lifecycle;
        report.lifecycle_function_count += lifecycle.init_array.size() +
                                           lifecycle.fini_array.size() +
                                           (lifecycle.init.has_value() ? 1U : 0U) +
                                           (lifecycle.fini.has_value() ? 1U : 0U);
        report.has_arm_exidx = report.has_arm_exidx ||
                               lifecycle.arm_exidx.has_value();
    }
    if (report.symbol_count == 0 || report.relocation_count == 0 ||
        !report.has_arm_exidx) {
        throw BionicProfileError(
            "Bionic libc self-check lacks required ELF runtime facts");
    }
    return report;
}

}  // namespace ogplay::runtime
