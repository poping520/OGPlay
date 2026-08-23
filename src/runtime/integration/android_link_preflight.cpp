#include "ogplay/runtime/integration/android_link_preflight.h"

#include <cstddef>
#include <exception>
#include <span>
#include <string>
#include <string_view>

#include "ogplay/memory/address_space.h"
#include "ogplay/runtime/bionic/bionic_profile.h"
#include "../boundary/android_boundary_symbols.h"

namespace ogplay::runtime {

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
