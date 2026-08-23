#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "ogplay/memory/address_space.h"
#include "ogplay/runtime/bionic/bionic_profile.h"

namespace ogplay::runtime {

struct AndroidApiRange final {
    AndroidApi minimum{AndroidApi::api19};
    AndroidApi maximum{AndroidApi::api23};

    [[nodiscard]] bool Contains(AndroidApi api) const noexcept;
};

struct BoundaryExportDescriptor final {
    std::string name;
    std::uint16_t local_id{};
    std::uint8_t parameter_count{};
    AndroidApiRange api{};
    memory::GuestAddress address;
};

struct BoundaryModuleDescriptor final {
    std::string soname;
    AndroidApiRange api{};
    std::uint32_t first_slot{};
    std::vector<BoundaryExportDescriptor> exports;
};

struct BoundaryExportDefinition final {
    std::string_view name;
    std::uint16_t local_id{};
    std::uint8_t parameter_count{};
    AndroidApiRange api{};
};

struct BoundaryModuleDefinition final {
    std::string_view soname;
    AndroidApiRange api{};
    std::span<const BoundaryExportDefinition> exports;
};

struct BoundaryModuleInstance final {
    const BoundaryModuleDescriptor* descriptor{};
    void* instance{};
};

class BoundaryCatalog final {
public:
    explicit BoundaryCatalog(AndroidApi api);
    BoundaryCatalog(AndroidApi api,
                    std::span<const BoundaryModuleDefinition> definitions);

    [[nodiscard]] AndroidApi Api() const noexcept;
    [[nodiscard]] std::span<const BoundaryModuleDescriptor> Modules() const noexcept;
    [[nodiscard]] const BoundaryModuleDescriptor* FindModule(
        std::string_view soname) const noexcept;
    [[nodiscard]] std::uint32_t SlotCount() const noexcept;

private:
    AndroidApi api_{};
    std::vector<BoundaryModuleDescriptor> modules_;
    std::uint32_t slot_count_{};
};

[[nodiscard]] const BoundaryCatalog& AndroidBoundaryCatalog(AndroidApi api);
[[nodiscard]] bool IsAndroidBoundaryLibrary(AndroidApi api,
                                            std::string_view soname) noexcept;

}  // namespace ogplay::runtime
