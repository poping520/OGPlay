#include "resources_test_support.h"
#include <doctest/doctest.h>
#include <algorithm>
#include <thread>
using namespace ogplay::test::resources;

namespace {
std::vector<std::byte> layout() {
    auto f = Framework();
    return ogplay::loader::ReadApkEntry(f->source().bytes, f->archive(), "res/layout/simple_spinner_item.xml");
}

void patch(std::vector<std::byte>& b, std::size_t off, std::uint32_t v) {
    for (unsigned i = 0; i < 4; ++i)
        b.at(off + i) = static_cast<std::byte>(v >> (8 * i));
}
}  // namespace

TEST_CASE("resources core XML exposes namespace IDs typed attributes special indexes and line numbers") {
    auto source = OwnedSource(layout());
    res::ResXMLTree tree(source);
    res::ResXMLParser p(tree);
    CHECK(p.event() == res::ResXMLParser::StartDocument);
    CHECK(p.next() == res::ResXMLParser::StartNamespace);
    CHECK(p.strings().stringAt(p.namespacePrefix()) == "android");
    CHECK(p.strings().stringAt(p.namespaceUri()) == "http://schemas.android.com/apk/res/android");
    CHECK(p.next() == res::ResXMLParser::StartTag);
    CHECK(p.strings().stringAt(p.elementName()) == "TextView");
    CHECK(p.elementNamespace() == res::NoIndex);
    CHECK(p.lineNumber() == 20);
    CHECK(p.attributeCount() == 7);
    auto index = p.indexOfAttribute("http://schemas.android.com/apk/res/android", "layout_width");
    REQUIRE(index);
    auto a = p.attribute(*index);
    CHECK(a.resourceId == 0x010100f4);
    CHECK(a.value.type == 0x10);
    CHECK(a.value.data == 0xffffffff);
    CHECK_FALSE(p.indexOfId());
    CHECK_FALSE(p.indexOfClass());
    REQUIRE(p.indexOfStyle());
    CHECK(p.attribute(*p.indexOfStyle()).value.type == 2);
    CHECK_FALSE(p.indexOfAttribute(std::nullopt, "layout_width"));
    CHECK(p.next() == res::ResXMLParser::EndTag);
    CHECK(p.attributeCount() == 0);
    CHECK_THROWS_AS(static_cast<void>(p.attribute(0)), res::ResourceError);
    CHECK(p.next() == res::ResXMLParser::EndNamespace);
    CHECK(p.next() == res::ResXMLParser::EndDocument);
    CHECK(p.next() == res::ResXMLParser::EndDocument);
    p.restart();
    CHECK(p.next() == res::ResXMLParser::StartNamespace);
    res::ResXMLParser independent(tree);
    CHECK(independent.event() == res::ResXMLParser::StartDocument);
    CHECK(independent.nextXmlBlockEvent() == 2);
    CHECK(independent.idAttribute() == res::NoIndex);
    CHECK(independent.classAttribute() == res::NoIndex);
    CHECK(independent.styleAttribute() == independent.attribute(*independent.indexOfStyle()).value.data);
    CHECK(independent.nextXmlBlockEvent() == 3);
    CHECK(independent.nextXmlBlockEvent() == 1);
}

TEST_CASE("resources core real ROM layout applyStyle produces six slots and requested indices") {
    auto source = OwnedSource(layout());
    res::ResXMLTree tree(source);
    res::ResXMLParser p(tree);
    while (p.next() != res::ResXMLParser::StartTag) {
    }
    auto t = Table();
    res::Theme theme(t);
    theme.applyStyle(Id(t, "style/Theme.Holo"), true);
    std::array attrs = {Id(t, "attr/textAppearance"), Id(t, "attr/id"), Id(t, "attr/layout_width"),
                        Id(t, "attr/layout_height"), Id(t, "attr/singleLine")};
    std::sort(attrs.begin(), attrs.end());
    auto result = res::applyStyle(theme, 0, 0, &p, attrs);
    CHECK(result.values.size() == attrs.size() * 6);
    CHECK(result.indices == std::vector<std::int32_t>{5, 0, 1, 2, 3, 4});
    const auto slot = [&](std::string_view name) {
        auto id = Id(t, name);
        auto off = static_cast<std::size_t>(std::find(attrs.begin(), attrs.end(), id) - attrs.begin()) * 6;
        return std::span(result.values).subspan(off, 6);
    };
    auto width = slot("attr/layout_width");
    CHECK(std::vector<std::int32_t>(width.begin(), width.end()) == std::vector<std::int32_t>{0x10, -1, -1, 0, 0, 0});
    auto height = slot("attr/layout_height");
    CHECK(height[1] == -2);
    auto single = slot("attr/singleLine");
    CHECK(single[0] == 0x12);
    CHECK(single[1] == -1);
    CHECK(single[2] == -1);
    auto appearance = slot("attr/textAppearance");
    CHECK(appearance[0] == 1);
    CHECK(appearance[2] == 1);
    CHECK(appearance[3] == appearance[1]);
    CHECK(appearance[4] != 0);
    CHECK(appearance[5] == 0);
    auto direct = res::retrieveAttributes(t, p, attrs);
    CHECK(direct.values.size() == attrs.size() * 6);
    CHECK(direct.indices[0] == 4);
    CHECK(direct.values[0] == 0);
}

TEST_CASE("resources core string pools retain UTF encodings sorted flag spans and synchronized caches") {
    std::array<std::u16string, 3> words = {u"hello", u"中文😀", std::u16string(180, u'x')};
    for (bool utf8 : {false, true}) {
        auto bytes = StringPool(words, utf8);
        res::ResStringPool pool(bytes);
        CHECK(pool.utf8() == utf8);
        CHECK_FALSE(pool.sorted());
        CHECK(pool.stringAt(1) == "中文😀");
        CHECK(pool.string16At(1) == words[1]);
        CHECK(pool.indexOfString("hello") == 0);
        CHECK_FALSE(pool.indexOfString("missing"));
        CHECK(pool.string16At(2) == words[2]);
        CHECK_THROWS_AS(static_cast<void>(pool.stringAt(3)), res::ResourceError);
        CHECK(pool.styleAt(1).empty());
        std::array<std::thread, 4> threads;
        for (auto& thread : threads)
            thread = std::thread([&] {
                for (int i = 0; i < 100; ++i)
                    if (pool.string16At(1) != words[1]) std::terminate();
            });
        for (auto& thread : threads)
            thread.join();
    }
    std::array<std::u16string, 2> styled = {u"bold", u"b"};
    auto bytes = StringPool(styled);
    auto dataStart = 36U;
    bytes.insert(bytes.begin() + 36, 8, std::byte{});
    patch(bytes, 12, 2);
    patch(bytes, 20, dataStart + 8);
    auto styleStart = bytes.size();
    patch(bytes, 24, static_cast<std::uint32_t>(styleStart));
    patch(bytes, 36, 0);
    patch(bytes, 40, res::NoIndex);
    bytes.resize(bytes.size() + 16);
    patch(bytes, styleStart, 1);
    patch(bytes, styleStart + 4, 0);
    patch(bytes, styleStart + 8, 3);
    patch(bytes, styleStart + 12, res::NoIndex);
    patch(bytes, 4, static_cast<std::uint32_t>(bytes.size()));
    res::ResStringPool pool(bytes);
    CHECK(pool.styleAt(0) == std::vector<res::Span>{{1, 0, 3}});
    CHECK(pool.styleAt(1).empty());
    patch(bytes, styleStart + 8, 4);
    res::ResStringPool invalid(bytes);
    CHECK_THROWS_AS(static_cast<void>(invalid.styleAt(0)), res::ResourceError);
    std::array<std::u16string, 3> sorted = {u"a", u"z", u"中"};
    auto sb = StringPool(sorted, true, true);
    res::ResStringPool sp(sb);
    CHECK(sp.sorted());
    CHECK(sp.indexOfString("中") == 2);
}

TEST_CASE("resources core malformed XML and string pool offsets fail explicitly") {
    auto data = layout();
    data.resize(data.size() - 4);
    CHECK_THROWS_AS(static_cast<void>(res::ResXMLTree{OwnedSource(data)}), res::ResourceError);
    data = layout();
    patch(data, 8 + 20, res::NoIndex);
    CHECK_THROWS_AS(static_cast<void>(res::ResXMLTree{OwnedSource(data)}), res::ResourceError);
    std::array<std::u16string, 1> words = {u"bad"};
    auto pool = StringPool(words, true);
    patch(pool, 28, res::NoIndex);
    CHECK_THROWS_AS(static_cast<void>(res::ResStringPool{pool}), res::ResourceError);
    pool = StringPool(words, true);
    pool.back() = std::byte{0xff};
    pool[35] = std::byte{0xff};
    res::ResStringPool invalid(pool);
    CHECK_THROWS_AS(static_cast<void>(invalid.stringAt(0)), res::ResourceError);
}

TEST_CASE("resources core retains styled strings from the real framework pool") {
    auto table = Table();
    auto value = table.getResource(0x01040303);
    REQUIRE(value);
    REQUIRE(value->value.type == 3);
    auto pool = table.strings(value->cookie);
    CHECK(pool.stringAt(value->value.data) == "Touch to type password");  // values/strings.xml
    auto spans = pool.styleAt(value->value.data);
    REQUIRE(spans.size() == 1);
    CHECK(pool.stringAt(spans[0].name) == "font;size=17");
    CHECK(spans[0].firstChar == 0);
    CHECK(spans[0].lastChar == 21);
}
