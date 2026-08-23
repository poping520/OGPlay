#include "runtime/boundary/core/boundary_catalog.h"

#include <algorithm>
#include <set>
#include <stdexcept>
#include <utility>

namespace ogplay::runtime {
namespace {

constexpr std::uint32_t kThunkStride = 4U;

}  // namespace

bool AndroidApiRange::Contains(const AndroidApi api) const noexcept {
    return static_cast<std::uint8_t>(api) >= static_cast<std::uint8_t>(minimum) &&
           static_cast<std::uint8_t>(api) <= static_cast<std::uint8_t>(maximum);
}

BoundaryCatalog::BoundaryCatalog(
    const AndroidApi api,
    const std::span<const BoundaryModuleDefinition> definitions)
    : api_(api) {
    std::set<std::string, std::less<>> sonames;
    for (const auto& definition : definitions) {
        if (!definition.api.Contains(api_)) continue;
        if (!sonames.insert(std::string(definition.soname)).second) {
            throw std::logic_error("invalid boundary module catalog");
        }
        BoundaryModuleDescriptor module;
        module.soname = definition.soname;
        module.api = definition.api;
        module.first_slot = slot_count_;
        std::set<std::string, std::less<>> names;
        std::set<std::uint16_t> local_ids;
        for (const auto& definition_export : definition.exports) {
            if (!definition_export.api.Contains(api_)) continue;
            if (!names.insert(std::string(definition_export.name)).second ||
                !local_ids.insert(definition_export.local_id).second) {
                throw std::logic_error("invalid boundary export catalog");
            }
            BoundaryExportDescriptor export_;
            export_.name = definition_export.name;
            export_.local_id = definition_export.local_id;
            export_.parameter_count = definition_export.parameter_count;
            export_.api = definition_export.api;
            export_.kind = definition_export.kind;
            export_.size = definition_export.size;
            if (definition_export.kind == BoundaryExportKind::public_data) {
                if (definition_export.data_address.Value() == 0U ||
                    definition_export.size == 0U) {
                    throw std::invalid_argument(
                        "boundary data export requires address and size");
                }
                export_.address = definition_export.data_address;
            } else {
                export_.address = memory::GuestAddress{
                    kBionicHleThunkBegin + slot_count_ * kThunkStride + 1U};
                ++slot_count_;
            }
            module.exports.push_back(std::move(export_));
        }
        // An explicitly export-less Virtual SO is still an active DT_NEEDED
        // provider. A module made empty only by API filtering remains inactive.
        if (definition.exports.empty() || !module.exports.empty()) {
            modules_.push_back(std::move(module));
        }
    }
}

AndroidApi BoundaryCatalog::Api() const noexcept { return api_; }

std::span<const BoundaryModuleDescriptor> BoundaryCatalog::Modules() const noexcept {
    return modules_;
}

const BoundaryModuleDescriptor* BoundaryCatalog::FindModule(
    const std::string_view soname) const noexcept {
    const auto found = std::find_if(modules_.begin(), modules_.end(),
                                    [&](const auto& module) {
                                        return module.soname == soname;
                                    });
    return found == modules_.end() ? nullptr : &*found;
}

std::uint32_t BoundaryCatalog::SlotCount() const noexcept { return slot_count_; }

}  // namespace ogplay::runtime
