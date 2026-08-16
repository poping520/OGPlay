#include <doctest/doctest.h>

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "ogplay/runtime/integration/dexvm_android.h"

namespace {

ogplay::loader::BinaryXmlAttribute AndroidAttribute(
    std::string name, const std::uint32_t data) {
    return {.namespace_uri =
                "http://schemas.android.com/apk/res/android",
            .name = std::move(name),
            .value_type = 0x01,
            .data = data};
}

std::vector<ogplay::loader::BinaryXmlElement> IncludeDocument(
    const std::uint32_t included_id) {
    std::vector<ogplay::loader::BinaryXmlElement> result(2);
    result[0].name = "FrameLayout";
    result[1].name = "include";
    result[1].parent = 0;
    result[1].attributes = {AndroidAttribute("layout", included_id)};
    return result;
}

}  // namespace

TEST_CASE("include expansion rejects cycles and excessive recursion") {
    const auto root = IncludeDocument(1);
    CHECK_THROWS_WITH(
        static_cast<void>(ogplay::runtime::ExpandUiIncludes(
            root, [](const std::uint32_t id) {
                return IncludeDocument(id == 1U ? 2U : 1U);
            })),
        "include layout resource cycle");
    CHECK_THROWS_WITH(
        static_cast<void>(ogplay::runtime::ExpandUiIncludes(
            root, [](const std::uint32_t id) {
                return IncludeDocument(id + 1U);
            })),
        "include depth exceeds 16");
}
