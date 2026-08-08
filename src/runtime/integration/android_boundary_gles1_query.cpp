#include "android_boundary_gles1_query.h"

#include <cstddef>
#include <stdexcept>
#include <utility>
#include <vector>

#include "ogplay/memory/address_space.h"

namespace ogplay::runtime::detail {
namespace {

constexpr memory::GuestAddress kGles1QueryStringRegion{0x70010000U};
constexpr std::uint32_t kQueryStringSlotBytes = 16U * 1024U;
constexpr std::uint32_t kQueryStringRegionBytes = kQueryStringSlotBytes * 4U;

[[nodiscard]] std::uint32_t QueryStringOffset(const std::uint32_t parameter) {
    switch (parameter) {
    case 0x1F00U: return 0U;                              // GL_VENDOR
    case 0x1F01U: return kQueryStringSlotBytes;           // GL_RENDERER
    case 0x1F02U: return kQueryStringSlotBytes * 2U;      // GL_VERSION
    case 0x1F03U: return kQueryStringSlotBytes * 3U;      // GL_EXTENSIONS
    default: throw std::invalid_argument("GLES1 string query is unsupported");
    }
}

}  // namespace

AndroidBoundaryGles1QueryStrings::AndroidBoundaryGles1QueryStrings(
    memory::AddressSpace& address_space)
    : address_space_(&address_space) {}

void AndroidBoundaryGles1QueryStrings::Validate(
    const std::uint32_t parameter) const {
    static_cast<void>(QueryStringOffset(parameter));
}

std::uint32_t AndroidBoundaryGles1QueryStrings::Publish(
    const std::uint32_t parameter, const std::string_view value,
    const std::uint64_t thread_id) {
    const auto offset = QueryStringOffset(parameter);
    if (value.size() >= kQueryStringSlotBytes) {
        throw std::length_error("ANGLE GLES1 query string exceeds its guest slot");
    }
    if (!region_mapped_) {
        address_space_->Map({kGles1QueryStringRegion, kQueryStringRegionBytes},
                            memory::PageProtection::read |
                                memory::PageProtection::write);
        address_space_->Protect(
            {kGles1QueryStringRegion, kQueryStringRegionBytes},
            memory::PageProtection::read);
        region_mapped_ = true;
    }
    std::vector<std::byte> bytes;
    bytes.reserve(value.size() + 1U);
    for (const auto character : value) {
        bytes.push_back(static_cast<std::byte>(
            static_cast<unsigned char>(character)));
    }
    bytes.push_back(std::byte{});
    const memory::GuestRange region{kGles1QueryStringRegion,
                                    kQueryStringRegionBytes};
    address_space_->Protect(region, memory::PageProtection::read |
                                        memory::PageProtection::write);
    try {
        address_space_->Write(kGles1QueryStringRegion.Add(offset), bytes,
                              thread_id);
    } catch (...) {
        address_space_->Protect(region, memory::PageProtection::read);
        throw;
    }
    address_space_->Protect(region, memory::PageProtection::read);
    return kGles1QueryStringRegion.Add(offset).Value();
}

void BindAndroidBoundaryGles1Queries(
    gles::GlesDispatchTable& dispatch,
    AndroidBoundaryGles1QueryStrings& strings,
    AndroidBoundaryGles1StringResolver resolve_string) {
    if (!resolve_string) {
        throw std::invalid_argument("GLES1 string resolver is missing");
    }
    dispatch.Bind(
        "glGetString",
        [&strings, resolve_string = std::move(resolve_string)](
            const std::span<const std::uint32_t> arguments,
            const std::uint64_t thread_id) {
            strings.Validate(arguments[0]);
            return strings.Publish(arguments[0], resolve_string(arguments[0]),
                                   thread_id);
        });
}

}  // namespace ogplay::runtime::detail
