#pragma once

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "ogplay/loader/module_loader.h"
#include "ogplay/runtime/bionic/bionic_profile.h"

namespace ogplay::runtime {

struct BionicModuleSource final {
    std::string name;
    std::span<const std::byte> image;
};

struct BionicOwnedModule final {
    std::string name;
    std::vector<std::byte> image;
    memory::GuestAddress load_bias;
};

class BionicModuleSet final {
public:
    explicit BionicModuleSet(std::vector<BionicOwnedModule> modules);

    [[nodiscard]] std::string_view RootName() const noexcept;
    [[nodiscard]] const std::vector<BionicOwnedModule>& Modules() const noexcept;
    [[nodiscard]] std::vector<loader::Elf32ModuleInput> Inputs() const;

private:
    std::vector<BionicOwnedModule> modules_;
};

[[nodiscard]] BionicModuleSet BuildBionicModuleSet(
    const BionicProfile& profile, std::string_view root_name,
    std::span<const std::byte> root_image,
    std::span<const BionicModuleSource> system_libraries);

}  // namespace ogplay::runtime
