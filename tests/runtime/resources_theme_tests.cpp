#include "resources_test_support.h"
#include <doctest/doctest.h>
#include <algorithm>
using namespace ogplay::test::resources;

namespace {
struct Attributes final : res::XmlAttributes {
    std::vector<res::XmlAttribute> values;
    std::optional<std::size_t> style;

    std::size_t attributeCount() const override { return values.size(); }

    res::XmlAttribute attribute(std::size_t i) const override { return values.at(i); }

    std::optional<std::size_t> indexOfStyle() const override { return style; }
};
}  // namespace

TEST_CASE("resources core themes resolve real framework references and copy independently") {
    auto t = Table();

    struct Expected {
        std::string_view name;
        std::array<std::uint32_t, 3> refs;
    };

    // values/themes.xml and values/themes_device_defaults.xml.
    const std::array expected{
        Expected{"Theme", {0x01060001, 0x0103003e, 0x01030014}},
        Expected{"Theme.Holo", {0x01060079, 0x010300fb, 0x0103008b}},
        Expected{"Theme.Holo.Light", {0x0106007a, 0x0103024f, 0x010300b6}},
        Expected{"Theme.DeviceDefault", {0x01060079, 0x010301ad, 0x01030141}},
    };
    for (const auto& [name, refs] : expected) {
        CAPTURE(name);
        res::Theme theme(t);
        theme.applyStyle(Id(t, std::string("style/") + std::string(name)), true);
        std::size_t ref = 0;
        for (auto attr :
             {"textColorPrimary", "textAppearance", "buttonStyle", "windowBackground", "listPreferredItemHeight"}) {
            CAPTURE(attr);
            auto v = theme.getAttribute(Id(t, std::string("attr/") + attr));
            REQUIRE(v);
            auto resolved = theme.resolveAttributeReference(*v);
            CHECK(resolved.cookie == 1);
            if (ref < refs.size()) {
                CHECK(v->value == res::Value{1, refs[ref]});
                CHECK(resolved.resourceId == refs[ref]);
            }
            ++ref;
            if (std::string_view(attr) == "listPreferredItemHeight") {
                CHECK(resolved.value.type == 5);
                CHECK(resolved.value.data == 16385);
            } else {
                CHECK((resolved.value.type == 1 || resolved.value.type == 3 || resolved.value.type >= 0x10));
                if (v->value.type == 1) CHECK(resolved.resourceId != 0);
            }
        }
        res::Theme copy(t);
        copy.setTo(theme);
        auto attr = Id(t, "attr/buttonStyle");
        auto before = copy.getAttribute(attr);
        theme.applyStyle(Id(t, "style/Theme.Holo.Light"), true);
        CHECK(copy.getAttribute(attr)->value == before->value);
    }
}

TEST_CASE("resources core application cookie and theme values resolve across packages") {
    res::AssetManager am(Framework());
    CHECK(am.addAssetPath(AppApk()) == 2);
    auto& t = am.resources();
    t.setParameters(Config());
    auto id = t.identifierForName("fixture:style/AppTheme");
    CHECK(id == 0x7f020000);
    auto bag = t.getBagLocked(id);
    REQUIRE(bag);
    CHECK(std::is_sorted(bag->entries.begin(), bag->entries.end(),
                         [](const res::BagEntry& a, const res::BagEntry& b) { return a.name < b.name; }));
    res::Theme theme(t);
    theme.applyStyle(id, true);
    auto alias = res::loadResourceValue(t, 0x7f030000, 0, true);
    REQUIRE(alias);
    CHECK(alias->cookie == 1);
    CHECK(alias->resourceId == 0x0104000a);
    CHECK(t.strings(1).stringAt(alias->value.data) == "OK");
    auto custom = theme.getAttribute(0x7f010000);
    REQUIRE(custom);
    CHECK(custom->value.data == 0x0106000b);
    auto resolved = theme.resolveAttributeReference(*custom);
    CHECK(resolved.cookie == 1);
    CHECK(resolved.value.type == 0x1c);
    CHECK(resolved.value.data == 0xffffffff);
    CHECK(res::getArraySize(t, 0x7f050000) == 3);
    auto strings = res::getArrayStringResource(t, 0x7f050000);
    CHECK(strings[0] == u"OK");
    // Array strings cross the native boundary as UTF-16, including surrogate pairs.
    const std::vector<Entry> supplementaryRows{
        {.id = 0x7f050000, .name = "supplementary", .complex = true, .bag = {{0x01000000, {3, 0}}}}};
    const std::array supplementaryStrings{std::u16string(u"\U0001f600")};
    res::ResTable supplementary;
    supplementary.add(std::make_shared<res::ResTableData>(
                          OwnedSource(ResourceTable(supplementaryRows, 0x7f, "fixture", supplementaryStrings))),
                      1);
    CHECK(res::getArrayStringResource(supplementary, 0x7f050000)[0] == u"\U0001f600");
    CHECK_FALSE(strings[1]);
    auto info = res::getArrayStringInfo(t, 0x7f050000);
    CHECK(info[0][0] == 0);
    CHECK(info[1][1] == -1);
    auto ints = res::getArrayIntResource(t, 0x7f050000);
    CHECK(ints == std::vector<std::int32_t>{0, 7, 0});
    auto array = res::retrieveArray(t, 0x7f050000);
    CHECK(array.size() == 18);
    CHECK(array[0] == 3);
    CHECK(array[8] == 2);
    CHECK(array[12] == 0);
}

TEST_CASE("resources core applyStyle prioritizes XML style defaults theme and null masks") {
    res::AssetManager am(Framework());
    (void)am.addAssetPath(AppApk());
    auto& t = am.resources();
    t.setParameters(Config());
    res::Theme theme(t);
    theme.applyStyle(Id(t, "style/Theme.Holo"), true);
    auto attr = Id(t, "attr/textColorPrimary");
    Attributes xml;
    xml.values = {{{}, {}, {}, attr, {1, 0}}};
    std::array attrs = {attr};
    auto result = res::applyStyle(theme, 0, 0, &xml, attrs);
    CHECK(result.values == std::vector<std::int32_t>{0, 0, -1, 0, 0, 0});
    CHECK(result.indices == std::vector<std::int32_t>{0});
    xml.values[0].value = {0x1c, 0xff123456};
    result = res::applyStyle(theme, 0, 0, &xml, attrs);
    CHECK(result.values[0] == 0x1c);
    CHECK(static_cast<std::uint32_t>(result.values[1]) == 0xff123456);
    CHECK(result.values[2] == -1);
    CHECK(result.indices == std::vector<std::int32_t>{1, 0});
    xml.values = {{res::NoIndex, 0, res::NoIndex, 0, {1, 0x7f020000}}};
    xml.style = 0;
    result = res::applyStyle(theme, 0, 0, &xml, attrs);
    CHECK(static_cast<std::uint32_t>(result.values[1]) == 0xffffffff);
    CHECK(result.values[2] == 1);
    CHECK(result.values[3] == 0x0106000b);
    xml.values.clear();
    xml.style.reset();
    result = res::applyStyle(theme, 0, 0x7f020000, &xml, attrs);
    CHECK(result.values[3] == 0x0106000b);
}

TEST_CASE("resources core caches configured bags and rejects long theme attribute chains as missing") {
    std::vector<Entry> entries;
    Entry style{.id = 0x7f020000, .name = "Chain", .complex = true};
    for (std::uint32_t i = 0; i < 22; ++i) {
        const auto id = 0x7f010000 + i;
        entries.push_back({.id = id, .name = "attr" + std::to_string(i), .value = {16, i}});
        style.bag.emplace_back(id, i < 21 ? res::Value{2, id + 1} : res::Value{16, 42});
    }
    entries.push_back(style);
    style.config.orientation = 2;
    style.bag.back().second.data = 84;
    entries.push_back(style);
    res::ResTable table;
    table.add(std::make_shared<res::ResTableData>(OwnedSource(ResourceTable(entries))), 2);
    auto original = table.getBagLocked(style.id);
    REQUIRE(original);
    CHECK(table.getBagLocked(style.id) == original);
    table.setParameters(Config());
    const auto landscape = table.getBagLocked(style.id);
    REQUIRE(landscape);
    CHECK(landscape != original);
    CHECK(landscape->entries.back().value.value.data == 84);
    CHECK(original->entries.back().value.value.data == 42);
    res::Theme theme(table);
    CHECK_FALSE(theme.applyStyle(0x7f010000, true));
    REQUIRE(theme.applyStyle(style.id, true));
    CHECK_FALSE(theme.getAttribute(0x7f010000));
    CHECK(theme.getAttribute(0x7f010015)->value.data == 84);
}
