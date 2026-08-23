#include "ogplay/runtime/bionic/bionic_module_set.h"

#include "runtime/boundary/modules/module_catalog.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>

#include "ogplay/loader/elf.h"

namespace ogplay::runtime {
namespace {

constexpr std::uint64_t kFirstLoadBias = 0x10000000U;
constexpr std::uint64_t kLoadAlignment = 0x10000U;

[[nodiscard]] bool Contains(const std::span<const std::string_view> values,
                            const std::string_view value) {
    return std::find(values.begin(), values.end(), value) != values.end();
}

void RequireLibraryName(const std::string_view name) {
    if (name.empty() || name.find('/') != std::string_view::npos ||
        name.find('\\') != std::string_view::npos || !name.ends_with(".so")) {
        throw BionicProfileError("Bionic module name is not a library basename: " +
                                 std::string(name));
    }
}

[[nodiscard]] std::uint64_t AlignUp(const std::uint64_t value,
                                    const std::uint64_t alignment) {
    if (value > std::numeric_limits<std::uint64_t>::max() - (alignment - 1U)) {
        throw BionicProfileError("Bionic module address allocation overflowed");
    }
    return (value + alignment - 1U) & ~(alignment - 1U);
}

[[nodiscard]] loader::Elf32DynamicInfo InspectModule(
    const BionicOwnedModule& module, const bool require_soname_match) {
    const auto image = loader::ParseElf32Arm(module.image);
    if (image.type != loader::Elf32ImageType::shared_object) {
        throw BionicProfileError("Bionic module is not ET_DYN: " + module.name);
    }
    auto dynamic = loader::ReadElf32DynamicInfo(module.image, image);
    if (require_soname_match && dynamic.soname.has_value() &&
        *dynamic.soname != module.name) {
        throw BionicProfileError("Bionic module SONAME does not match source name: " +
                                 module.name);
    }
    return dynamic;
}

void AssignLoadBiases(std::vector<BionicOwnedModule>& modules) {
    std::uint64_t cursor = kFirstLoadBias;
    for (auto& module : modules) {
        const auto image = loader::ParseElf32Arm(module.image);
        if (cursor >= kBionicHleThunkBegin) {
            throw BionicProfileError("Bionic module set overlaps the HLE thunk range");
        }
        module.load_bias = memory::GuestAddress{static_cast<std::uint32_t>(cursor)};
        const auto plan = loader::BuildElf32LoadPlan(image, module.load_bias, 4096);
        std::uint64_t end = cursor;
        for (const auto& region : plan.regions) {
            end = std::max(end, region.range.EndExclusive());
        }
        cursor = AlignUp(end + kLoadAlignment, kLoadAlignment);
        if (cursor > kBionicHleThunkBegin) {
            throw BionicProfileError("Bionic module set overlaps the HLE thunk range");
        }
    }
}

}  // namespace

BionicModuleSet::BionicModuleSet(std::vector<BionicOwnedModule> modules)
    : modules_(std::move(modules)) {
    if (modules_.empty()) {
        throw std::invalid_argument("Bionic module set cannot be empty");
    }
}

std::string_view BionicModuleSet::RootName() const noexcept {
    return modules_.front().name;
}

const std::vector<BionicOwnedModule>& BionicModuleSet::Modules() const noexcept {
    return modules_;
}

std::vector<loader::Elf32ModuleInput> BionicModuleSet::Inputs() const {
    std::vector<loader::Elf32ModuleInput> result;
    result.reserve(modules_.size());
    for (const auto& module : modules_) {
        result.push_back({module.name, module.image, module.load_bias});
    }
    return result;
}

BionicModuleSet BuildBionicModuleSet(
    const BionicProfile& profile, const std::string_view root_name,
    const std::span<const std::byte> root_image,
    const std::span<const BionicModuleSource> system_libraries) {
    RequireLibraryName(root_name);
    if (root_image.empty()) {
        throw BionicProfileError("Bionic root module image is empty");
    }

    std::map<std::string, std::span<const std::byte>, std::less<>> available;
    for (const auto& source : system_libraries) {
        RequireLibraryName(source.name);
        if (!Contains(profile.guest_libraries, source.name)) {
            throw BionicProfileError("system library is outside the Bionic profile: " +
                                     source.name);
        }
        if (source.image.empty()) {
            throw BionicProfileError("system library image is empty: " + source.name);
        }
        if (!available.emplace(source.name, source.image).second) {
            throw BionicProfileError("duplicate system library source: " + source.name);
        }
    }
    if (available.contains(root_name)) {
        throw BionicProfileError("root module duplicates a system library source");
    }

    std::vector<BionicOwnedModule> modules;
    modules.push_back({std::string(root_name),
                       std::vector<std::byte>(root_image.begin(), root_image.end()),
                       memory::GuestAddress{0}});
    std::set<std::string, std::less<>> selected{std::string(root_name)};
    for (std::size_t index = 0; index < modules.size(); ++index) {
        // The APK catalog name identifies the selected root entry and may
        // legitimately differ from its DT_SONAME. Dependencies, however,
        // are selected by DT_NEEDED and must retain exact catalog identity.
        const auto dynamic = InspectModule(modules[index], index != 0U);
        for (const auto& needed : dynamic.needed) {
            if (selected.contains(needed) ||
                IsAndroidBoundaryLibrary(profile.api, needed)) {
                continue;
            }
            if (!Contains(profile.guest_libraries, needed)) {
                throw BionicProfileError("ELF requires undeclared Bionic library: " +
                                         needed);
            }
            const auto source = available.find(needed);
            if (source == available.end()) {
                throw BionicProfileError("required Bionic system library is missing: " +
                                         needed);
            }
            selected.insert(needed);
            modules.push_back({needed,
                               std::vector<std::byte>(source->second.begin(),
                                                      source->second.end()),
                               memory::GuestAddress{0}});
        }
    }
    AssignLoadBiases(modules);
    return BionicModuleSet{std::move(modules)};
}

}  // namespace ogplay::runtime
