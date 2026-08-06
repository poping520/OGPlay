#include "ogplay/runtime/integration/android_link_preflight.h"

#include <cstddef>
#include <exception>
#include <span>
#include <string>
#include <string_view>

#include "ogplay/memory/address_space.h"
#include "ogplay/runtime/bionic/bionic_profile.h"
#include "ogplay/runtime/integration/android_boundary_hle.h"

namespace ogplay::runtime {

AndroidLinkPreflightReport PreflightAndroidGuestLink(
    const AndroidLinkPreflightRequest& request) {
    if (request.root_module.empty() || request.modules.empty() ||
        request.width == 0 || request.height == 0) {
        throw AndroidLinkPreflightError(
            "Android link preflight requires modules and a surface");
    }
    try {
        const auto& profile = SelectBionicProfile(request.api);
        memory::AddressSpace address_space;
        AndroidBoundaryHle boundary(address_space, request.backend,
                                    request.width, request.height,
                                    request.supersample_factor);
        boundary.MapThunks();
        const auto loaded = loader::LoadElf32ModuleNamespace(
            request.root_module, request.modules, address_space,
            [&profile, &boundary](
                const std::string_view root,
                const std::span<const loader::Elf32LinkModule> guest) {
                return BuildBionicLinkNamespace(profile, root, guest,
                                                boundary.Symbols());
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
