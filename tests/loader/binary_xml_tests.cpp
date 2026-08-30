#include <doctest/doctest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#include "ogplay/loader/binary_xml.h"
#include "ogplay/loader/apk.h"

namespace {

void Append16(std::vector<std::byte>& bytes, const std::uint16_t value) {
    bytes.push_back(static_cast<std::byte>(value & 0xffU));
    bytes.push_back(static_cast<std::byte>((value >> 8U) & 0xffU));
}

void Append32(std::vector<std::byte>& bytes, const std::uint32_t value) {
    for (unsigned shift = 0; shift < 32; shift += 8) {
        bytes.push_back(static_cast<std::byte>((value >> shift) & 0xffU));
    }
}

void Set32(std::vector<std::byte>& bytes, const std::size_t offset,
           const std::uint32_t value) {
    for (unsigned shift = 0; shift < 32; shift += 8) {
        bytes[offset + shift / 8U] =
            static_cast<std::byte>((value >> shift) & 0xffU);
    }
}

void AppendAscii(std::vector<std::byte>& bytes, const std::string_view text) {
    for (const auto value : text) {
        bytes.push_back(static_cast<std::byte>(value));
    }
}

std::vector<std::byte> StringPool(const std::vector<std::string>& strings,
                                  const bool utf8) {
    std::vector<std::byte> data;
    std::vector<std::uint32_t> offsets;
    for (const auto& string : strings) {
        offsets.push_back(static_cast<std::uint32_t>(data.size()));
        if (utf8) {
            data.push_back(static_cast<std::byte>(string.size()));
            data.push_back(static_cast<std::byte>(string.size()));
            AppendAscii(data, string);
            data.push_back(std::byte{0});
        } else {
            Append16(data, static_cast<std::uint16_t>(string.size()));
            for (const auto value : string) {
                Append16(data, static_cast<std::uint8_t>(value));
            }
            Append16(data, 0);
        }
    }
    while (data.size() % 4U != 0) data.push_back(std::byte{0});

    std::vector<std::byte> chunk;
    Append16(chunk, 0x0001);
    Append16(chunk, 28);
    Append32(chunk, 0);
    Append32(chunk, static_cast<std::uint32_t>(strings.size()));
    Append32(chunk, 0);
    Append32(chunk, utf8 ? 0x100U : 0U);
    Append32(chunk, static_cast<std::uint32_t>(28U + offsets.size() * 4U));
    Append32(chunk, 0);
    for (const auto offset : offsets) Append32(chunk, offset);
    chunk.insert(chunk.end(), data.begin(), data.end());
    Set32(chunk, 4, static_cast<std::uint32_t>(chunk.size()));
    return chunk;
}

struct Attribute final {
    std::uint32_t name{};
    std::uint32_t raw_value{0xffffffffU};
    std::uint8_t type{};
    std::uint32_t data{};
    std::uint32_t namespace_index{0xffffffffU};
};

std::vector<std::byte> StartElement(const std::uint32_t name,
                                    const std::vector<Attribute>& attributes) {
    std::vector<std::byte> chunk;
    Append16(chunk, 0x0102);
    Append16(chunk, 16);
    Append32(chunk, static_cast<std::uint32_t>(36U + attributes.size() * 20U));
    Append32(chunk, 1);
    Append32(chunk, 0xffffffffU);
    Append32(chunk, 0xffffffffU);
    Append32(chunk, name);
    Append16(chunk, 20);
    Append16(chunk, 20);
    Append16(chunk, static_cast<std::uint16_t>(attributes.size()));
    Append16(chunk, 0);
    Append16(chunk, 0);
    Append16(chunk, 0);
    for (const auto& attribute : attributes) {
        Append32(chunk, attribute.namespace_index);
        Append32(chunk, attribute.name);
        Append32(chunk, attribute.raw_value);
        Append16(chunk, 8);
        chunk.push_back(std::byte{0});
        chunk.push_back(static_cast<std::byte>(attribute.type));
        Append32(chunk, attribute.data);
    }
    return chunk;
}

std::vector<std::byte> EndElement(const std::uint32_t name) {
    std::vector<std::byte> chunk;
    Append16(chunk, 0x0103);
    Append16(chunk, 16);
    Append32(chunk, 24);
    Append32(chunk, 1);
    Append32(chunk, 0xffffffffU);
    Append32(chunk, 0xffffffffU);
    Append32(chunk, name);
    return chunk;
}

std::vector<std::byte> Cdata(const std::uint32_t text) {
    std::vector<std::byte> chunk;
    Append16(chunk, 0x0104);
    Append16(chunk, 16);
    Append32(chunk, 28);
    Append32(chunk, 1);
    Append32(chunk, 0xffffffffU);
    Append32(chunk, text);
    Append16(chunk, 8);
    chunk.push_back(std::byte{0});
    chunk.push_back(std::byte{0x03});
    Append32(chunk, text);
    return chunk;
}

void Append(std::vector<std::byte>& destination,
            const std::vector<std::byte>& source) {
    destination.insert(destination.end(), source.begin(), source.end());
}

// Layout document: LinearLayout root with a TextView (android:id set) and
// a plain View child.
std::vector<std::byte> Layout(const bool utf8) {
    const std::vector<std::string> strings{
        "LinearLayout", "TextView", "View", "id",
        "http://schemas.android.com/apk/res/android"};
    std::vector<std::byte> result;
    Append16(result, 0x0003);
    Append16(result, 8);
    Append32(result, 0);
    Append(result, StringPool(strings, utf8));
    Append(result, StartElement(0, {}));
    Append(result, StartElement(1, {{3, 0xffffffffU, 0x01, 0x7f0a0001U, 4}}));
    Append(result, EndElement(1));
    Append(result, StartElement(2, {}));
    Append(result, EndElement(2));
    Append(result, EndElement(0));
    Set32(result, 4, static_cast<std::uint32_t>(result.size()));
    return result;
}

}  // namespace

TEST_CASE("binary XML layout walk yields tags and android:id references") {
    for (const bool utf8 : {false, true}) {
        const auto elements =
            ogplay::loader::ParseBinaryXmlElements(Layout(utf8));
        REQUIRE(elements.size() == 3);
        CHECK(elements[0].name == "LinearLayout");
        CHECK(elements[0].id == 0);
        CHECK(elements[0].parent == -1);
        CHECK(elements[1].name == "TextView");
        CHECK(elements[1].id == 0x7f0a0001U);
        CHECK(elements[1].parent == 0);
        CHECK(elements[2].name == "View");
        CHECK(elements[2].id == 0);
        CHECK(elements[2].parent == 0);
    }
}

// Layout document exercising the bounds-derivation attribute subset: a
// bottom bar (fill x wrap, layout_gravity bottom, gravity center_horizontal,
// paddingTop 8dp) holding one drawable-backed button.
TEST_CASE("binary XML layout walk decodes the layout attribute subset") {
    const std::vector<std::string> strings{
        "LinearLayout",  "ImageButton",   "layout_width", "layout_height",
        "gravity",       "layout_gravity", "paddingTop",  "src",
        "http://schemas.android.com/apk/res/android"};
    constexpr std::uint32_t kNs = 8;
    std::vector<std::byte> document;
    Append16(document, 0x0003);
    Append16(document, 8);
    Append32(document, 0);
    Append(document, StringPool(strings, true));
    Append(document, StartElement(
        0, {{2, 0xffffffffU, 0x10, 0xffffffffU, kNs},   // fill_parent
            {3, 0xffffffffU, 0x10, 0xfffffffeU, kNs},   // wrap_content
            {4, 0xffffffffU, 0x11, 0x00000001U, kNs},   // center_horizontal
            {5, 0xffffffffU, 0x11, 0x00000050U, kNs},   // bottom
            {6, 0xffffffffU, 0x05, 0x00000801U, kNs}})); // 8dp
    Append(document, StartElement(
        1, {{2, 0xffffffffU, 0x10, 0xfffffffeU, kNs},
            {3, 0xffffffffU, 0x10, 0xfffffffeU, kNs},
            {7, 0xffffffffU, 0x01, 0x7f020010U, kNs}}));
    Append(document, EndElement(1));
    Append(document, EndElement(0));
    Set32(document, 4, static_cast<std::uint32_t>(document.size()));

    const auto elements = ogplay::loader::ParseBinaryXmlElements(document);
    REQUIRE(elements.size() == 2);
    using Element = ogplay::loader::BinaryXmlElement;
    CHECK(elements[0].layout_width == Element::kSizeFillParent);
    CHECK(elements[0].layout_height == Element::kSizeWrapContent);
    CHECK(elements[0].gravity == 0x01U);
    CHECK(elements[0].layout_gravity == 0x50U);
    CHECK(elements[0].padding_top == 8);
    CHECK(elements[0].src == 0);
    CHECK(elements[1].parent == 0);
    CHECK(elements[1].layout_width == Element::kSizeWrapContent);
    CHECK(elements[1].layout_height == Element::kSizeWrapContent);
    CHECK(elements[1].src == 0x7f020010U);
    REQUIRE(elements[0].attributes.size() == 5);
    CHECK(elements[0].attributes[0].namespace_uri ==
          "http://schemas.android.com/apk/res/android");
    CHECK(elements[0].attributes[0].name == "layout_width");
    CHECK(elements[0].attributes[0].value_type == 0x10);
    CHECK(elements[0].attributes[0].data == 0xffffffffU);
    CHECK_FALSE(elements[0].attributes[0].raw_string.has_value());
}

TEST_CASE("binary XML preserves generic typed and namespaced attributes") {
    const std::vector<std::string> strings{
        "TextView", "visibility", "orientation", "text", "hello",
        "custom", "urn:example", "http://schemas.android.com/apk/res/android"};
    std::vector<std::byte> document;
    Append16(document, 0x0003);
    Append16(document, 8);
    Append32(document, 0);
    Append(document, StringPool(strings, true));
    Append(document, StartElement(
        0, {{1, 0xffffffffU, 0x10, 4U, 7},
            {2, 0xffffffffU, 0x11, 1U, 7},
            {3, 4U, 0x03, 4U, 7},
            {5, 0xffffffffU, 0x01, 0x7f010002U, 6}}));
    Append(document, EndElement(0));
    Set32(document, 4, static_cast<std::uint32_t>(document.size()));

    const auto elements = ogplay::loader::ParseBinaryXmlElements(document);
    REQUIRE(elements.size() == 1);
    REQUIRE(elements[0].attributes.size() == 4);
    CHECK(elements[0].attributes[0].name == "visibility");
    CHECK(elements[0].attributes[0].value_type == 0x10);
    CHECK(elements[0].attributes[1].name == "orientation");
    CHECK(elements[0].attributes[1].value_type == 0x11);
    CHECK(elements[0].attributes[2].name == "text");
    CHECK(elements[0].attributes[2].raw_string == "hello");
    CHECK(elements[0].attributes[3].namespace_uri == "urn:example");
    CHECK(elements[0].attributes[3].data == 0x7f010002U);
}

TEST_CASE("binary XML pull events preserve compiled text and tag order") {
    const std::vector<std::string> strings{"settings", "Level", "error"};
    std::vector<std::byte> document;
    Append16(document, 0x0003);
    Append16(document, 8);
    Append32(document, 0);
    Append(document, StringPool(strings, true));
    Append(document, StartElement(0, {}));
    Append(document, StartElement(1, {}));
    Append(document, Cdata(2));
    Append(document, EndElement(1));
    Append(document, EndElement(0));
    Set32(document, 4, static_cast<std::uint32_t>(document.size()));

    const auto events = ogplay::loader::ParseBinaryXmlPullEvents(document);
    using Type = ogplay::loader::BinaryXmlPullEventType;
    REQUIRE(events.size() == 7);
    CHECK(events[0].type == Type::start_document);
    CHECK(events[1].type == Type::start_tag);
    CHECK(events[1].name == "settings");
    CHECK(events[1].depth == 1);
    CHECK(events[2].name == "Level");
    CHECK(events[2].depth == 2);
    CHECK(events[3].type == Type::text);
    CHECK(events[3].text == "error");
    CHECK(events[3].depth == 2);
    CHECK(events[4].type == Type::end_tag);
    CHECK(events[4].name == "Level");
    CHECK(events[4].depth == 2);
    CHECK(events[6].type == Type::end_document);
}

TEST_CASE("binary XML layout walk rejects malformed documents") {
    std::vector<std::byte> not_xml;
    Append16(not_xml, 0x0001);
    Append16(not_xml, 8);
    Append32(not_xml, 8);
    CHECK_THROWS_WITH(static_cast<void>(
                          ogplay::loader::ParseBinaryXmlElements(not_xml)),
                      "not a binary XML document");

    auto truncated = Layout(false);
    Set32(truncated, 4, static_cast<std::uint32_t>(truncated.size() + 8U));
    CHECK_THROWS_WITH(static_cast<void>(
                          ogplay::loader::ParseBinaryXmlElements(truncated)),
                      "binary XML field is out of range");

    auto bad_chunk = Layout(false);
    // First chunk after the header is the string pool; zero its size.
    Set32(bad_chunk, 12, 0);
    CHECK_THROWS_WITH(static_cast<void>(
                          ogplay::loader::ParseBinaryXmlElements(bad_chunk)),
                      "binary XML chunk is malformed");

    auto unclosed = Layout(false);
    unclosed.resize(unclosed.size() - EndElement(0).size());
    Set32(unclosed, 4, static_cast<std::uint32_t>(unclosed.size()));
    CHECK_THROWS_WITH(static_cast<void>(
                          ogplay::loader::ParseBinaryXmlElements(unclosed)),
                      "binary XML element is not closed");

    auto mismatched = Layout(false);
    const auto final_end_offset = mismatched.size() - EndElement(0).size();
    Set32(mismatched, final_end_offset + 20, 1U);
    CHECK_THROWS_WITH(static_cast<void>(
                          ogplay::loader::ParseBinaryXmlElements(mismatched)),
                      "binary XML element nesting is malformed");
}

TEST_CASE("binary XML pull reader accepts local exact resource documents when present") {
    const auto apk_path = std::filesystem::path(OGPLAY_SOURCE_DIR) / ".local" /
                          "games" / "pvz_na_zhcn_fixed.apk";
    if (!std::filesystem::exists(apk_path)) return;
    std::ifstream stream(apk_path, std::ios::binary);
    std::vector<std::byte> apk(
        static_cast<std::size_t>(std::filesystem::file_size(apk_path)));
    stream.read(reinterpret_cast<char*>(apk.data()),
                static_cast<std::streamsize>(apk.size()));
    const auto archive = ogplay::loader::ParseApkArchive(apk);
    for (const auto path : {"res/xml/nimble_log.xml",
                            "res/xml/components.xml"}) {
        const auto bytes = ogplay::loader::ReadApkEntry(apk, archive, path);
        const auto events = ogplay::loader::ParseBinaryXmlPullEvents(bytes);
        REQUIRE(events.size() >= 4);
        CHECK(events.front().type ==
              ogplay::loader::BinaryXmlPullEventType::start_document);
        CHECK(events.back().type ==
              ogplay::loader::BinaryXmlPullEventType::end_document);
        CHECK(std::ranges::any_of(events, [](const auto& event) {
            return event.type ==
                   ogplay::loader::BinaryXmlPullEventType::text;
        }));
    }
}
