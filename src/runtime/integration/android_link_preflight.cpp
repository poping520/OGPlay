#include "ogplay/runtime/integration/android_link_preflight.h"

#include <cstddef>
#include "ogplay/core/json.h"
#include <exception>
#include <span>
#include <string>
#include <string_view>
#include <set>
#include <tuple>
#include "ogplay/runtime/integration/native_library_loader.h"

#include "ogplay/memory/address_space.h"
#include "ogplay/runtime/bionic/bionic_profile.h"
#include "runtime/boundary/core/boundary_symbols.h"

namespace ogplay::runtime {

std::string DumpAndroidBoundarySurfaceJson() {
    core::JsonWriter out;
    const auto root = out.Array();
    for (const auto& symbol : detail::BuildAndroidBoundarySymbols(AndroidApi::api19)) {
        const auto item = out.Object();
        out.AddString(item, "library", symbol.library);
        out.AddString(item, "symbol", symbol.symbol);
        out.Append(root, item);
    }
    return out.Serialize(root);
}

AndroidImportAudit AuditAndroidNativeImports(
    const loader::ApkSelectedNativeLibraries& libraries, const BionicProfile& profile,
    const std::span<const BionicModuleSource> system_libraries) {
    AndroidImportAudit report;
    const auto symbols = detail::BuildAndroidBoundarySymbols(profile.api);
    const BionicHleSymbolProvider provider(symbols);
    std::set<std::tuple<std::string, std::string, bool, bool>> missing;
    for (const auto& library : libraries.Libraries()) {
        const auto closure = CollectApkNativeLibraryClosure(
            libraries, profile, system_libraries, library.soname);
        std::vector<loader::Elf32LinkModule> modules;
        std::vector<std::set<std::uint32_t>> referenced;
        for (const auto& source : closure) {
            const auto image = loader::ParseElf32Arm(source.image);
            auto dynamic = loader::ReadElf32DynamicInfo(source.image, image);
            auto table = loader::ReadElf32SymbolTable(source.image, image);
            const auto relocations = loader::ReadElf32Relocations(source.image, image, table);
            auto& used = referenced.emplace_back();
            for (const auto& relocation : relocations.relocations) {
                if (relocation.type != loader::kArmRelocationNone) used.insert(relocation.symbol_index);
            }
            auto versions = loader::ReadElf32SymbolVersions(source.image, image, dynamic, table);
            modules.push_back({source.name, source.load_bias, std::move(dynamic),
                               std::move(table), std::move(versions), {}});
        }
        const auto ns = BuildBionicLinkNamespace(profile, library.soname, modules, provider);
        const loader::Elf32LinkScope scope{0, ns.load_order, ns.lookup_scope};
        for (std::size_t m = 0; m < modules.size(); ++m) {
            const auto& module = ns.modules[m];
            for (std::size_t i = 1; i < module.symbols.symbols.size(); ++i) {
                const auto& symbol = module.symbols.symbols[i];
                if (symbol.section_index != 0 || (symbol.binding != 1 && symbol.binding != 2)) continue;
                std::optional<std::string_view> version, dependency;
                if (module.versions) {
                    const auto& required = module.versions->symbols[i];
                    if (required.kind == loader::Elf32SymbolVersionKind::requirement) {
                        version = required.name;
                        dependency = required.dependency;
                    } else if (required.kind != loader::Elf32SymbolVersionKind::global &&
                               required.kind != loader::Elf32SymbolVersionKind::local) {
                        throw AndroidLinkPreflightError("undefined ELF symbol has an invalid version kind: " + symbol.name);
                    }
                }
                try {
                    static_cast<void>(loader::LookupElf32Symbol(ns, scope, symbol.name, version, dependency));
                } catch (const loader::LinkError& error) {
                    if (!std::string_view(error.what()).starts_with("ELF symbol is unresolved: ")) throw;
                    missing.emplace(module.name, symbol.name, symbol.binding == 2,
                                    referenced[m].contains(static_cast<std::uint32_t>(i)));
                }
            }
        }
    }
    for (const auto& [library, symbol, weak, used] : missing) {
        report.gaps.push_back({library, symbol, weak, used});
        if (!used) {
            if (weak)
                ++report.unused_weak_missing;
            else
                ++report.unused_strong_missing;
            continue;
        }
        if (weak) ++report.weak_missing; else ++report.strong_missing;
    }
    return report;
}

AndroidLinkPreflightReport PreflightAndroidGuestLink(
    const AndroidLinkPreflightRequest& request) {
    if (request.root_module.empty() || request.modules.empty()) {
        throw AndroidLinkPreflightError(
            "Android link preflight requires modules");
    }
    try {
        const auto& profile = SelectBionicProfile(request.api);
        memory::AddressSpace address_space;
        const auto symbols = detail::BuildAndroidBoundarySymbols(profile.api);
        const BionicHleSymbolProvider provider(symbols);
        const auto loaded = loader::LoadElf32ModuleNamespace(
            request.root_module, request.modules, address_space,
            [&profile, &provider](
                const std::string_view root,
                const std::span<const loader::Elf32LinkModule> guest) {
                return BuildBionicLinkNamespace(profile, root, guest,
                                                provider);
            });
        std::size_t relocations{};
        for (const auto& module : loaded.modules) {
            relocations += module.relocations.relocations.size();
        }
        return {loaded.modules.size(),
                loaded.link_namespace.modules.size() - loaded.modules.size(),
                relocations};
    } catch (const AndroidLinkPreflightError&) {
        throw;
    } catch (const std::exception& error) {
        throw AndroidLinkPreflightError(
            "Android guest link preflight failed: " + std::string(error.what()));
    }
}

}  // namespace ogplay::runtime
