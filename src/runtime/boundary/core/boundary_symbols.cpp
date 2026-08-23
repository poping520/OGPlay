#include "boundary_symbols.h"

#include <array>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include "ogplay/gles/gles_dispatch.h"
#include "ogplay/runtime/bionic/guest_symbol_override.h"
#include "runtime/boundary/modules/module_catalog.h"

namespace ogplay::runtime::detail {
namespace {

constexpr std::uint32_t kThunkStride = 4U;

}  // namespace

std::vector<BionicHleSymbol> BuildAndroidBoundarySymbols(const AndroidApi api) {
    std::vector<BionicHleSymbol> result;
    const auto& catalog = AndroidBoundaryCatalog(api);
    result.reserve(catalog.SlotCount() + GuestSymbolOverrides().size());
    for (const auto& module : catalog.Modules()) {
        for (const auto& export_ : module.exports) {
            result.push_back({module.soname, export_.name, export_.address});
        }
    }
    for (const auto& override_ : GuestSymbolOverrides()) {
        result.push_back({std::string(override_.library),
                          std::string(override_.symbol),
                          memory::GuestAddress{kBionicHleThunkBegin +
                              static_cast<std::uint32_t>(result.size()) *
                                  kThunkStride + 1U}});
    }
    return result;
}

std::vector<HleThunkDescriptor> BuildAndroidBoundaryDescriptors(
    const std::span<const BionicHleSymbol> symbols) {
    std::vector<HleThunkDescriptor> result;
    result.reserve(symbols.size());
    for (std::size_t index = 0; index < symbols.size(); ++index) {
        const auto& symbol = symbols[index];
        const auto code_address = symbol.address.Value() & ~std::uint32_t{1};
        const auto expected = kBionicHleThunkBegin +
                              static_cast<std::uint32_t>(index) * kThunkStride;
        if (code_address != expected) {
            throw std::logic_error("Android boundary thunk catalog is not dense");
        }
        const auto overrides = GuestSymbolOverrides();
        const auto override_ = std::find_if(
            overrides.begin(), overrides.end(), [&](const auto& candidate) {
                return candidate.library == symbol.library &&
                       candidate.symbol == symbol.symbol;
            });
        if (override_ != overrides.end()) {
            result.push_back({symbol.library, symbol.symbol,
                              override_->local_id,
                              override_->parameter_count});
            continue;
        }
        const auto* module = AndroidBoundaryCatalog(AndroidApi::api19)
                                 .FindModule(symbol.library);
        if (module == nullptr) {
            throw std::logic_error("boundary descriptor module is missing");
        }
        const auto export_ = std::find_if(
            module->exports.begin(), module->exports.end(),
            [&](const auto& candidate) {
                return candidate.name == symbol.symbol;
            });
        if (export_ == module->exports.end()) {
            throw std::logic_error("boundary descriptor export is missing");
        }
        result.push_back({symbol.library, symbol.symbol, export_->local_id,
                          export_->parameter_count});
    }
    return result;
}

const HleThunkDescriptor* DecodeAndroidBoundaryThunk(
    const std::uint64_t pc,
    const std::span<const HleThunkDescriptor> descriptors) noexcept {
    const auto code = pc & ~UINT64_C(1);
    if (code < kBionicHleThunkBegin || code >= kBionicHleThunkEnd) {
        return nullptr;
    }
    const auto offset = code - kBionicHleThunkBegin;
    if (offset % kThunkStride != 0U) return nullptr;
    const auto index = offset / kThunkStride;
    if (index >= descriptors.size()) return nullptr;
    return &descriptors[static_cast<std::size_t>(index)];
}

}  // namespace ogplay::runtime::detail
