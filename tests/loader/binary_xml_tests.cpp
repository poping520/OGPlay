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
#include "ogplay/runtime/integration/xml_cursor.h"
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
    CHECK(ogplay::loader::ParseBinaryXmlElements(unclosed).size() == 3);

    auto mismatched = Layout(false);
    const auto final_end_offset = mismatched.size() - EndElement(0).size();
    Set32(mismatched, final_end_offset + 20, 1U);
    CHECK(ogplay::loader::ParseBinaryXmlElements(mismatched).size() == 3);
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

namespace {
std::vector<std::byte> XmlDocument(const std::vector<std::vector<std::byte>>& chunks) {
    std::vector<std::byte> bytes;
    Append16(bytes, 3);
    Append16(bytes, 8);
    Append32(bytes, 0);
    for (const auto& chunk : chunks)
        Append(bytes, chunk);
    Set32(bytes, 4, static_cast<std::uint32_t>(bytes.size()));
    return bytes;
}
}  // namespace

TEST_CASE("WU11b XML string conversion follows API19 code units and lazy access") {
    auto utf16 = StringPool({"A", "unused"}, false);
    utf16[38] = std::byte{0};
    utf16[39] = std::byte{0xd8};  // first string: lone high surrogate
    auto doc = XmlDocument({utf16, StartElement(0, {}), EndElement(0)});
    CHECK(ogplay::loader::ParseBinaryXmlElements(doc)[0].name == "\xed\xa0\x80");
    auto utf8 = StringPool({"aa"}, true);
    utf8[32] = std::byte{1};  // declared UTF16 units
    utf8[34] = std::byte{0xc1};
    utf8[35] = std::byte{0x81};  // API19 mask conversion -> A
    doc = XmlDocument({utf8, StartElement(0, {}), EndElement(0)});
    CHECK(ogplay::loader::ParseBinaryXmlElements(doc)[0].name == "A");
    auto lazy = StringPool({"View", "unused"}, true);
    Set32(lazy, 32, 0xfffffff0U);  // unused string offset
    doc = XmlDocument({lazy, StartElement(0, {}), EndElement(0)});
    CHECK(ogplay::loader::ParseBinaryXmlElements(doc)[0].name == "View");
    doc = XmlDocument({lazy, StartElement(0, {{0, 1, 3, 1}}), EndElement(0)});
    CHECK_FALSE(ogplay::loader::ParseBinaryXmlElements(doc)[0].attributes[0].raw_string.has_value());
}

TEST_CASE("WU11b XML preamble pool replacement ends at the first node") {
    auto doc = XmlDocument({StringPool({"old"}, true), StringPool({"new"}, true), StartElement(0, {}),
                            StringPool({"late"}, true), EndElement(0)});
    const auto events = ogplay::loader::ParseBinaryXmlPullEvents(doc);
    CHECK(events[1].name == "new");
    CHECK(events[2].name == "new");
    auto bad = StringPool({"old"}, true);
    Set32(bad, 20, 0xffffffffU);
    doc = XmlDocument({bad, StringPool({"new"}, true), StartElement(0, {}), EndElement(0)});
    CHECK(ogplay::loader::ParseBinaryXmlElements(doc)[0].name == "new");
}

TEST_CASE("WU11b XML retains empty tag and attribute names and typed facts") {
    const auto doc =
        XmlDocument({StringPool({""}, true), StartElement(0, {{0, 0xffffffffU, 0x10, 73}}), EndElement(0)});
    const auto elements = ogplay::loader::ParseBinaryXmlElements(doc);
    REQUIRE(elements.size() == 1);
    CHECK(elements[0].name.empty());
    REQUIRE(elements[0].attributes.size() == 1);
    CHECK(elements[0].attributes[0].name.empty());
    CHECK(elements[0].attributes[0].data == 73);
}

TEST_CASE("WU11b XML events do not enforce a named element stack") {
    using Type = ogplay::loader::BinaryXmlPullEventType;
    const auto pool = StringPool({"root", "other", "text"}, true);
    auto doc = XmlDocument({pool, Cdata(2), EndElement(1), StartElement(0, {}), EndElement(1), StartElement(0, {})});
    const auto events = ogplay::loader::ParseBinaryXmlPullEvents(doc);
    REQUIRE(events.size() == 7);
    CHECK(events[1].type == Type::text);
    CHECK(events[1].text == "text");
    CHECK(events[1].depth == 0);
    CHECK(events[2].type == Type::end_tag);
    CHECK(events[2].depth == 0);
    CHECK(events[4].name == "other");
    CHECK(events.back().type == Type::end_document);
    CHECK(events.back().depth == 0);
    doc = XmlDocument({pool, StartElement(0, {}), EndElement(0)});
    doc.push_back(std::byte{0});
    Set32(doc, 4, static_cast<std::uint32_t>(doc.size()));
    CHECK_THROWS_WITH(static_cast<void>(ogplay::loader::ParseBinaryXmlPullEvents(doc)),
                      "binary XML document has trailing bytes");
}

namespace {
std::vector<std::byte> ResourceMap() {
    std::vector<std::byte> chunk;
    Append16(chunk, 0x0180);
    Append16(chunk, 8);
    Append32(chunk, 8 + 12 * 4);
    for (std::uint32_t i = 0; i < 12; ++i)
        Append32(chunk, 0x01010000U + i);
    return chunk;
}

std::vector<std::byte> NamespaceNode(std::uint16_t type) {
    auto chunk = EndElement(3);
    chunk[0] = static_cast<std::byte>(type & 0xff);
    Set32(chunk, 16, 11);  // prefix pool index
    Set32(chunk, 20, 3);   // uri pool index
    return chunk;
}

// VD09A1-facts-v1: same name/two namespaces; deliberately unrelated resource
// ID; special indices deliberately point to attributes not named id/class/style.
std::vector<std::byte> CompiledFactsFixture() {
    auto start = StartElement(0, {{2, 0xffffffffU, 0x01, 0x7f010123U, 3},
                                  {2, 7, 0x02, 0x01010099U, 4},
                                  {5, 0xffffffffU, 0x05, 0x12345601U},
                                  {6, 0xffffffffU, 0x10, 0xfffffffeU},
                                  {8, 0xffffffffU, 0x12, 1},
                                  {9, 0xffffffffU, 0x03, 10},
                                  {5, 0xffffffffU, 0x01, 0},
                                  {6, 10, 0x03, 10},
                                  {9, 0xffffffffU, 0x11, 0xff00ff00U}});
    start[30] = std::byte{2};  // one-based id
    start[32] = std::byte{6};  // class
    start[34] = std::byte{7};  // style @null
    Set32(start, 8, 41);
    Set32(start, 16, 3);
    auto end = EndElement(0);
    Set32(end, 16, 3);
    Set32(end, 8, 49);
    auto child = StartElement(1, {{2, 7, 3, 7}});
    Set32(child, 8, 42);
    auto text = Cdata(7);
    Set32(text, 8, 43);
    return XmlDocument({StringPool({"root", "child", "same", "urn:a", "urn:b", "dimension", "integer", "raw", "boolean",
                                    "string", "", "p"},
                                   true),
                        ResourceMap(), NamespaceNode(0x0100), start, child, text, EndElement(1), end,
                        NamespaceNode(0x0101)});
}

void CheckLegacyFields(const ogplay::loader::CompiledXmlDocument& doc, const std::vector<std::byte>& bytes) {
    const auto elements = doc.Elements();
    const auto legacy = ogplay::loader::ParseBinaryXmlElements(bytes);
    REQUIRE(elements.size() == legacy.size());
    for (std::size_t i = 0; i < elements.size(); ++i) {
        const auto& a = elements[i];
        const auto& b = legacy[i];
        CHECK(a.name == b.name);
        CHECK(a.parent == b.parent);
        CHECK(a.id == b.id);
        CHECK(a.layout_width == b.layout_width);
        CHECK(a.layout_height == b.layout_height);
        CHECK(a.gravity == b.gravity);
        CHECK(a.layout_gravity == b.layout_gravity);
        CHECK(a.padding_top == b.padding_top);
        CHECK(a.src == b.src);
        REQUIRE(a.attributes.size() == b.attributes.size());
        for (std::size_t j = 0; j < a.attributes.size(); ++j) {
            CHECK(a.attributes[j].namespace_uri == b.attributes[j].namespace_uri);
            CHECK(a.attributes[j].name == b.attributes[j].name);
            CHECK(a.attributes[j].value_type == b.attributes[j].value_type);
            CHECK(a.attributes[j].data == b.attributes[j].data);
            CHECK(a.attributes[j].raw_string == b.attributes[j].raw_string);
        }
    }
    const auto events = doc.PullEvents();
    const auto old_events = ogplay::loader::ParseBinaryXmlPullEvents(bytes);
    REQUIRE(events.size() == old_events.size());
    for (std::size_t i = 0; i < events.size(); ++i) {
        CHECK(events[i].type == old_events[i].type);
        CHECK(events[i].name == old_events[i].name);
        CHECK(events[i].text == old_events[i].text);
        CHECK(events[i].depth == old_events[i].depth);
    }
}
}  // namespace

TEST_CASE("DVM-286 compiled XML retains indexed facts without semantic inference") {
    using namespace ogplay::loader;
    auto bytes = CompiledFactsFixture();
    const auto original = bytes;
    const auto doc = ParseCompiledXmlDocument(bytes);
    std::fill(bytes.begin(), bytes.end(), std::byte{0});
    CHECK(std::ranges::equal(doc->Bytes(), original));
    REQUIRE(doc->StringCount() == 12);
    CHECK(doc->String(10) == "");
    CHECK_FALSE(doc->String(kBinaryXmlNoIndex));
    CHECK_THROWS_AS(static_cast<void>(doc->String(12)), std::out_of_range);
    REQUIRE(doc->ResourceMap().size() == 12);
    REQUIRE(doc->Nodes().size() == 7);
    const auto& root = doc->Nodes()[1];
    CHECK(root.type == 0x0102);
    CHECK(root.line == 41);
    CHECK(root.namespace_index == 3);
    CHECK(root.comment_index == kBinaryXmlNoIndex);
    CHECK(root.name_index == 0);
    CHECK(root.depth == 1);
    CHECK(root.parent_node == -1);
    CHECK(root.matching_node == 5);
    CHECK(doc->Nodes()[5].matching_node == 1);
    CHECK(doc->Nodes()[2].parent_node == 1);
    CHECK(doc->Nodes()[2].matching_node == 4);
    CHECK(doc->Nodes()[0].matching_node == 6);
    CHECK(root.id_index == 2);
    CHECK(root.class_index == 6);
    CHECK(root.style_index == 7);
    const std::uint8_t types[]{1, 2, 5, 0x10, 0x12, 3, 1, 3, 0x11};
    const std::uint32_t values[]{0x7f010123U, 0x01010099U, 0x12345601U, 0xfffffffeU, 1, 10, 0, 10, 0xff00ff00U};
    REQUIRE(root.attributes.size() == 9);
    for (std::size_t i = 0; i < root.attributes.size(); ++i) {
        const auto& attr = root.attributes[i];
        CHECK(attr.value.type == types[i]);
        CHECK(attr.value.data == values[i]);
        CHECK(attr.value.size == 8);
        CHECK(attr.value.reserved == 0);
        CHECK(attr.resource_id == 0x01010000U + attr.name_index);
        CHECK(attr.raw_value_index == (i == 1 ? 7 : (i == 7 ? 10 : kBinaryXmlNoIndex)));
    }
    CHECK(root.attributes[0].name_index == root.attributes[1].name_index);
    CHECK(root.attributes[0].namespace_index == 3);
    CHECK(root.attributes[1].namespace_index == 4);
    CHECK(root.attributes[0].resource_id == 0x01010002U);
    CHECK(doc->String(root.attributes[0].name_index) == "same");
    CHECK_FALSE(doc->String(root.attributes[5].raw_value_index));
    CHECK(doc->String(root.attributes[7].raw_value_index) == "");
    CHECK(doc->String(root.attributes[5].value.data) == "");
    CheckLegacyFields(*doc, original);
    const auto elements = doc->Elements();
    REQUIRE(elements.size() == 2);
    CHECK(elements[0].name == "root");
    CHECK(elements[0].parent == -1);
    CHECK(elements[1].name == "child");
    CHECK(elements[1].parent == 0);
    CHECK(elements[0].id == 0);
    CHECK(elements[0].layout_width == 0);
    CHECK(elements[0].padding_top == 0);
    CHECK(elements[0].attributes[5].raw_string == "");  // historical typed-string fallback
    CHECK(elements[0].attributes[1].raw_string == "raw");
    const auto events = doc->PullEvents();
    REQUIRE(events.size() == 7);
    CHECK(events[1].depth == 1);
    CHECK(events[2].depth == 2);
    CHECK(events[3].text == "raw");
    CHECK(events[4].depth == 2);
    CHECK(events[5].depth == 1);
    CHECK(events[6].depth == 0);
}

TEST_CASE("DVM-286 compiled XML preserves WTF-8 identities and checked string failures") {
    using namespace ogplay::loader;
    auto pool = StringPool({"A", "A", "unused"}, false);
    const auto start = 28 + 3 * 4;
    pool[start + 2] = std::byte{0};
    pool[start + 3] = std::byte{0xd8};
    pool[start + 8] = std::byte{0};
    pool[start + 9] = std::byte{0xdc};
    Set32(pool, 36, 0xfffffff0U);
    const auto bytes = XmlDocument({pool, StartElement(0, {{1, 1, 3, 0}}), EndElement(0)});
    const auto doc = ParseCompiledXmlDocument(bytes);
    CHECK(doc->String(0) == "\xed\xa0\x80");
    CHECK(doc->String(1) == "\xed\xb0\x80");
    CHECK(doc->Nodes()[0].attributes[0].value.data == 0);
    CHECK(doc->Nodes()[0].attributes[0].raw_value_index == 1);
    CHECK_THROWS_AS(static_cast<void>(doc->String(2)), std::runtime_error);
    CheckLegacyFields(*doc, bytes);
}

TEST_CASE("DVM-286 compiled XML rejects invalid indices and unsupported bounded structures") {
    using namespace ogplay::loader;
    const auto pool = StringPool({"root"}, true);
    auto bad = StartElement(0, {{0, 0xffffffffU, 0x10, 1}});
    bad[30] = std::byte{2};
    CHECK_THROWS_WITH(static_cast<void>(ParseCompiledXmlDocument(XmlDocument({pool, bad, EndElement(0)}))),
                      "binary XML special attribute index is invalid");
    CHECK(ParseBinaryXmlElements(XmlDocument({pool, bad, EndElement(0)})).size() == 1);
    CHECK_THROWS_WITH(
        static_cast<void>(ParseCompiledXmlDocument(XmlDocument({pool, StartElement(1, {}), EndElement(0)}))),
        "binary XML pool index is invalid");
    CHECK_THROWS_WITH(static_cast<void>(ParseCompiledXmlDocument(
                          XmlDocument({pool, StartElement(0, {{0, 1, 3, 0}}), EndElement(0)}))),
                      "binary XML pool index is invalid");
    CHECK_THROWS_WITH(static_cast<void>(ParseCompiledXmlDocument(
                          XmlDocument({pool, StartElement(0, {{0, 0xffffffffU, 3, 1}}), EndElement(0)}))),
                      "binary XML typed string is invalid");
    bad = StartElement(0, {});
    Set32(bad, 4, 0xfffffffcU);
    CHECK_THROWS_WITH(static_cast<void>(ParseCompiledXmlDocument(XmlDocument({pool, bad}))),
                      "binary XML field is out of range");
    CHECK_THROWS_WITH(static_cast<void>(ParseCompiledXmlDocument(XmlDocument({pool, StartElement(0, {})}))),
                      "binary XML document has unclosed tags or no nodes");
    CHECK_THROWS_WITH(static_cast<void>(ParseCompiledXmlDocument(XmlDocument({pool, EndElement(0)}))),
                      "binary XML end tag has no start");
    bad = EndElement(0);
    bad[0] = std::byte{5};
    CHECK_THROWS_WITH(static_cast<void>(ParseCompiledXmlDocument(XmlDocument({pool, bad}))),
                      "binary XML node type is unsupported");
    bad = StartElement(0, {{0, 0xffffffffU, 0x10, 1}});
    bad[26] = std::byte{0xff};
    bad[27] = std::byte{0xff};  // attribute stride
    CHECK_THROWS_WITH(static_cast<void>(ParseCompiledXmlDocument(XmlDocument({pool, bad}))),
                      "binary XML field is out of range");
}

TEST_CASE("DVM-286 XML cursors isolate progression and reject retired identities") {
    using namespace ogplay::runtime;
    using Type = ogplay::loader::BinaryXmlPullEventType;
    XmlCursorRegistry registry, other;
    const auto bytes = CompiledFactsFixture();
    const auto owner = registry.CreateDocument(bytes);
    std::weak_ptr<const ogplay::loader::CompiledXmlDocument> lifetime = registry.Document(owner);
    const auto a = registry.CreateCursor(owner), b = registry.CreateCursor(owner);
    CHECK(registry.LiveDocuments() == 1);
    CHECK(registry.LiveCursors() == 2);
    CHECK(registry.Current(a).event == Type::start_document);
    CHECK_THROWS_AS(static_cast<void>(registry.StartTag(a)), std::logic_error);
    CHECK_THROWS_AS(static_cast<void>(other.Current(a)), std::invalid_argument);
    CHECK_THROWS_AS(static_cast<void>(other.CreateCursor(owner)), std::invalid_argument);
    const auto root = registry.Next(a);
    CHECK(root.event == Type::start_tag);
    CHECK(root.depth == 1);
    REQUIRE(root.node);
    CHECK(root.node->line == 41);
    CHECK(root.node->namespace_index == 3);
    CHECK(registry.StartTag(a).id_index == 2);
    CHECK(registry.StartTag(a).class_index == 6);
    CHECK(registry.StartTag(a).style_index == 7);
    CHECK(registry.Attribute(a, 6).value.data == 0);
    CHECK(registry.Attribute(a, 6).value.type == 1);
    CHECK(registry.FindAttribute(a, "urn:a", "same") == 0);
    CHECK(registry.FindAttribute(a, "urn:b", "same") == 1);
    CHECK_FALSE(registry.FindAttribute(a, std::nullopt, "same"));
    CHECK_THROWS_AS(static_cast<void>(registry.Attribute(a, 9)), std::out_of_range);
    CHECK(registry.Current(b).event == Type::start_document);
    const auto child = registry.Next(a);
    CHECK(child.depth == 2);
    REQUIRE(child.node);
    CHECK(child.node->line == 42);
    CHECK(registry.StartTag(a).attributes.size() == 1);
    CHECK(registry.FindAttribute(a, std::nullopt, "same") == 0);
    CHECK_FALSE(registry.FindAttribute(a, "urn:a", "same"));
    CHECK(registry.Next(b).depth == 1);
    CHECK(registry.StartTag(b).attributes.size() == 9);
    const auto text = registry.Next(a);
    CHECK(text.event == Type::text);
    CHECK(text.depth == 2);
    REQUIRE(text.node);
    CHECK(text.node->line == 43);
    CHECK_THROWS_AS(static_cast<void>(registry.Attribute(a, 0)), std::logic_error);
    CHECK(registry.Next(a).depth == 2);
    const auto end = registry.Next(a);
    CHECK(end.depth == 1);
    REQUIRE(end.node);
    CHECK(end.node->line == 49);
    CHECK_THROWS_AS(static_cast<void>(registry.FindAttribute(a, std::nullopt, "same")), std::logic_error);
    CHECK(registry.Next(a).event == Type::end_document);
    CHECK(registry.Current(a).depth == 0);
    CHECK_FALSE(registry.Current(a).node);
    CHECK(registry.Next(a).event == Type::end_document);
    registry.Close(a);
    CHECK(registry.LiveCursors() == 1);
    CHECK_THROWS_AS(static_cast<void>(registry.Current(a)), std::invalid_argument);
    CHECK_THROWS_AS(static_cast<void>(registry.Next(a)), std::invalid_argument);
    CHECK_THROWS_AS(static_cast<void>(registry.Close(a)), std::invalid_argument);
    const auto c = registry.CreateCursor(owner);
    CHECK(c.value != a.value);
    registry.RetireOwner(owner);
    CHECK(registry.LiveCursors() == 0);
    CHECK(registry.LiveDocuments() == 0);
    CHECK(lifetime.expired());
    CHECK_THROWS_AS(static_cast<void>(registry.Next(b)), std::invalid_argument);
    CHECK_THROWS_AS(static_cast<void>(registry.Current(c)), std::invalid_argument);
    CHECK_THROWS_AS(static_cast<void>(registry.Document(owner)), std::invalid_argument);
    CHECK_THROWS_AS(static_cast<void>(registry.CreateCursor(owner)), std::invalid_argument);
    const auto replacement = registry.CreateDocument(bytes);
    CHECK(replacement.value != owner.value);
    const auto last = registry.CreateCursor(replacement);
    registry.Shutdown();
    registry.Shutdown();
    CHECK(registry.LiveCursors() == 0);
    CHECK(registry.LiveDocuments() == 0);
    CHECK_THROWS_AS(static_cast<void>(registry.Current(last)), std::invalid_argument);
    CHECK_THROWS_AS(static_cast<void>(registry.CreateDocument(bytes)), std::logic_error);
    {
        XmlCursorRegistry temporary;
        const auto owned = temporary.CreateDocument(bytes);
        lifetime = temporary.Document(owned);
        static_cast<void>(temporary.CreateCursor(owned));
    }
    CHECK(lifetime.expired());
}

TEST_CASE("DVM-286 XML namespace queries distinguish absent from pooled empty URI") {
    using namespace ogplay::runtime;
    const auto bytes =
        XmlDocument({StringPool({"root", "same", ""}, true),
                     StartElement(0, {{1, 0xffffffffU, 0x10, 1, 2}, {1, 0xffffffffU, 0x10, 2}}), EndElement(0)});
    XmlCursorRegistry registry;
    const auto owner = registry.CreateDocument(bytes);
    const auto parser = registry.CreateCursor(owner);
    static_cast<void>(registry.Next(parser));
    CHECK(registry.FindAttribute(parser, "", "same") == 0);
    CHECK(registry.FindAttribute(parser, std::nullopt, "same") == 1);
    registry.RetireOwner(owner);
    CHECK(registry.LiveCursors() == 0);
}

TEST_CASE("DVM-286 compiled XML checks namespace pairs and node headers") {
    using namespace ogplay::loader;
    const auto pool = StringPool({"root", "unused", "name", "uri", "other", "", "", "", "", "", "", "p"}, true);
    auto end = NamespaceNode(0x0101);
    Set32(end, 20, 4);
    auto bytes = XmlDocument({pool, NamespaceNode(0x0100), StartElement(0, {}), EndElement(0), end});
    CHECK_THROWS_WITH(static_cast<void>(ParseCompiledXmlDocument(bytes)),
                      "binary XML namespace end does not match start");
    CHECK(ParseBinaryXmlElements(bytes).size() == 1);
    bytes = XmlDocument({pool, NamespaceNode(0x0101)});
    CHECK_THROWS_WITH(static_cast<void>(ParseCompiledXmlDocument(bytes)), "binary XML namespace end has no start");
    auto node = EndElement(0);
    node[2] = std::byte{8};
    bytes = XmlDocument({pool, node});
    CHECK_THROWS_WITH(static_cast<void>(ParseCompiledXmlDocument(bytes)), "binary XML node header is malformed");
    auto utf8 = StringPool({"abc"}, true);
    utf8[32] = std::byte{1};  // one UTF16 unit, three bytes: lone surrogate in WTF-8
    utf8[34] = std::byte{0xed};
    utf8[35] = std::byte{0xa0};
    utf8[36] = std::byte{0x80};
    bytes = XmlDocument({utf8, StartElement(0, {}), EndElement(0)});
    const auto doc = ParseCompiledXmlDocument(bytes);
    CHECK(doc->String(0) == "\xed\xa0\x80");
    CHECK(doc->Nodes()[0].name_index == 0);
    CheckLegacyFields(*doc, bytes);
}
