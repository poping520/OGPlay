#include "ogplay/core/byte_order.h"
#include <thread>
#include <atomic>
#include "resources_test_support.h"
#include "ogplay/core/text.h"
#include "ogplay/loader/dex_class_data.h"
#include "ogplay/loader/dex_code.h"
#include <doctest/doctest.h>
#include <algorithm>
#include <set>
using namespace ogplay::test::resources;

TEST_CASE("resources core table round trips every public android R constant in the shipped ROM") {
    auto t = Table();
    CHECK(t.packageNames() == std::vector<std::string>{"android"});
    auto jar = ReadSource(std::filesystem::path(OGPLAY_SOURCE_DIR) / "data/android/19/framework/framework.jar");
    auto zip = ogplay::loader::ParseApkArchive(jar.bytes);
    auto data = ogplay::loader::ReadApkEntry(jar.bytes, zip, "classes.dex");
    auto bytes = std::span(reinterpret_cast<const std::uint8_t*>(data.data()), data.size());
    auto dex = ogplay::loader::ParseDex(bytes);
    auto classes = ogplay::loader::ReadDexClassData(bytes, dex);
    std::size_t count = 0;
    for (const auto& c : classes) {
        auto& def = dex.classes[c.class_def_index];
        const auto& owner = dex.types[def.class_type_index].descriptor;
        if (!owner.starts_with("Landroid/R$") || owner == "Landroid/R$styleable;") continue;
        auto type = owner.substr(11, owner.size() - 12);
        auto values = ogplay::loader::ReadDexStaticValues(bytes, dex, def.static_values_offset);
        for (std::size_t i = 0; i < c.static_fields.size(); ++i) {
            const auto& f = c.static_fields[i];
            if ((f.access_flags & 0x19) != 0x19) continue;
            auto& field = dex.fields[f.field_index];
            if (dex.types[field.type_index].descriptor != "I") continue;
            auto name = *ogplay::core::Utf16ToUtf8(dex.strings[field.name_string_index].value,
                                                   ogplay::core::InvalidUtf16Policy::reject);
            REQUIRE(i < values.size());
            if (type == "style") std::replace(name.begin(), name.end(), '_', '.');
            auto id = static_cast<std::uint32_t>(values[i].integral);
            CAPTURE(type);
            CAPTURE(name);
            CAPTURE(id);
            CHECK(t.identifierForName("@android:" + type + "/" + name) == id);
            auto identity = t.getResourceName(id);
            REQUIRE(identity);
            CHECK(identity->fullName() == "android:" + type + "/" + name);
            ++count;
        }
    }
    MESSAGE("public R constants compared: ", count);
    CHECK(count == 1833);
}

TEST_CASE("resources core table retains all configurations and chooses API19 qualifiers") {
    auto t = Table();
    CHECK(t.identifierForName("@*android:style/Theme.Holo") == Id(t, "style/Theme.Holo"));
    CHECK(t.identifierForName("Theme.Holo", "style", "android") == Id(t, "style/Theme.Holo"));
    CHECK(t.identifierForName("^min") == 0x01000001);
    CHECK(t.identifierForName("^index_12") == 0x0200000c);
    CHECK(t.identifierForName("missing") == 0);
    auto locales = t.getLocales();
    CHECK(std::find(locales.begin(), locales.end(), "zh_CN") != locales.end());
    std::size_t variants = 0, ids = 0;
    std::set<std::uint32_t> seen;
    t.forEachVariant([&](const res::ResourceVariant& v) {
        ++variants;
        seen.insert(v.id);
    });
    ids = seen.size();
    MESSAGE("resource entries: ", ids, ", variants: ", variants);
    CHECK(ids == 7067);
    CHECK(variants == 98902);
    auto c = Config();
    t.setParameters(c);
    auto value = t.getResource(Id(t, "string/ok"));
    REQUIRE(value);
    CHECK(t.strings(1).stringAt(value->value.data) == "OK");
    c.language[0] = 'z';
    c.language[1] = 'h';
    c.country[0] = 'C';
    c.country[1] = 'N';
    t.setParameters(c);
    value = t.getResource(Id(t, "string/ok"));
    REQUIRE(value);
    CHECK(t.strings(1).stringAt(value->value.data) == "确定");  // values-zh-rCN/strings.xml: ok
}

TEST_CASE("resources core configuration honors all API19 matching dimensions") {
    auto requested = Config();
    auto generic = res::ResTable_config{};
    auto specific = generic;
    CHECK(generic.match(requested));
    specific.orientation = 1;
    CHECK_FALSE(specific.match(requested));
    specific.orientation = 2;
    CHECK(specific.match(requested));
    CHECK(specific.isBetterThan(generic, &requested));
    for (auto field : {&res::ResTable_config::mcc, &res::ResTable_config::mnc, &res::ResTable_config::screenWidth,
                       &res::ResTable_config::screenHeight, &res::ResTable_config::sdkVersion,
                       &res::ResTable_config::smallestScreenWidthDp, &res::ResTable_config::screenWidthDp,
                       &res::ResTable_config::screenHeightDp}) {
        auto desired = requested;
        desired.*field = 10;
        auto a = generic;
        a.*field = 11;
        CHECK_FALSE(a.match(desired));
        a.*field = 10;
        CHECK(a.match(desired));
        CHECK(a.isBetterThan(generic, &desired));
    }
    for (auto field :
         {&res::ResTable_config::touchscreen, &res::ResTable_config::keyboard, &res::ResTable_config::navigation}) {
        auto desired = requested;
        desired.*field = 1;
        auto a = generic;
        a.*field = 2;
        CHECK_FALSE(a.match(desired));
        a.*field = 1;
        CHECK(a.match(desired));
        CHECK(a.isBetterThan(generic, &desired));
    }
    auto hdpi = generic;
    hdpi.density = 240;
    auto xhdpi = generic;
    xhdpi.density = 320;
    CHECK(hdpi.match(requested));
    CHECK(hdpi.isBetterThan(xhdpi, &requested));
    requested.density = 320;
    CHECK(xhdpi.isBetterThan(hdpi, &requested));
    auto land = generic;
    land.screenLayout = 0x80;
    CHECK_FALSE(land.match(requested));
    requested.screenLayout = 0x80;
    CHECK(land.match(requested));
    CHECK(land.isBetterThan(generic, &requested));
    auto night = generic;
    night.uiMode = 0x20;
    CHECK_FALSE(night.match(requested));
    requested.uiMode = 0x20;
    CHECK(night.match(requested));
    CHECK(night.isBetterThan(generic, &requested));
}

TEST_CASE("resources core malformed table offsets fail before lookup") {
    auto f = Framework();
    auto bytes = ogplay::loader::ValidatedStoredApkEntryData(f->source().bytes, f->archive(), "resources.arsc");
    auto source = OwnedSource(std::vector<std::byte>(bytes.begin(), bytes.end() - 4));
    CHECK_THROWS_AS(static_cast<void>(res::ResTableData{source}), res::ResourceError);
    auto bad = std::vector<std::byte>(bytes.begin(), bytes.end());
    bad[4] = std::byte{0};
    bad[5] = std::byte{0};
    source = OwnedSource(std::move(bad));
    CHECK_THROWS_AS(static_cast<void>(res::ResTableData{source}), res::ResourceError);
}

TEST_CASE("resources core real qualifiers select land density smallest width locale and SDK") {
    auto t = Table();
    for (auto c : {Config(800, 480, 240), Config(1280, 720, 320)}) {
        t.setParameters(c);
        auto d = t.getResource(Id(t, "dimen/action_bar_default_height"));
        REQUIRE(d);
        CHECK(d->value.type == 5);
        CHECK(d->value.data == 10241);  // values-land/dimens.xml: 40dip
        auto layout = t.getResource(Id(t, "layout/icon_menu_layout"));
        REQUIRE(layout);
        CHECK(t.strings(1).stringAt(layout->value.data) ==
              "res/layout-land/icon_menu_layout.xml");  // layout-land/icon_menu_layout.xml
        auto pointer = t.getResource(Id(t, "drawable/pointer_arrow_icon"));
        REQUIRE(pointer);
        CHECK(pointer->density == c.density);
        CHECK(t.strings(1).stringAt(pointer->value.data) ==
              (c.density == 240
                   ? "res/drawable-hdpi/pointer_arrow_icon.xml"
                   : "res/drawable-xhdpi/pointer_arrow_icon.xml"));  // drawable-{h,xh}dpi/pointer_arrow_icon.xml
    }
    auto tablet = Config(1280, 800, 160);
    tablet.smallestScreenWidthDp = 600;
    t.setParameters(tablet);
    auto d = t.getResource(Id(t, "dimen/action_bar_default_height"));
    REQUIRE(d);
    CHECK(d->value.data == 14337);  // values-sw600dp/dimens.xml: 56dip, compiled sw600dp-v13
    auto rotation = t.getResource(Id(t, "bool/config_enableLockScreenRotation"));
    REQUIRE(rotation);
    CHECK(rotation->value.data == 0xffffffff);  // values-sw600dp/config.xml: true
    tablet.sdkVersion = 12;
    t.setParameters(tablet);
    rotation = t.getResource(Id(t, "bool/config_enableLockScreenRotation"));
    REQUIRE(rotation);
    CHECK(rotation->value.data == 0);  // values/config.xml: false; v13 qualifier filtered
    auto port = Config(480, 800, 240);
    t.setParameters(port);
    d = t.getResource(Id(t, "dimen/action_bar_default_height"));
    REQUIRE(d);
    CHECK(d->value.data == 12289);  // values/dimens.xml: 48dip
    std::uint32_t flags = 0;
    auto pointer = t.getResource(Id(t, "drawable/pointer_arrow_icon"), 320, &flags);
    REQUIRE(pointer);
    CHECK(pointer->density == 320);
    CHECK((flags & 0x100) != 0);
}

TEST_CASE("resources core bag parents merge attr order flags cycles and resource references") {
    std::vector<Entry> entries = {
        {0x7f010000, "one", {}},
        {0x7f060000, "private_name", {}},
        {0x7f010001, "two", {}},
        {0x7f010002, "three", {}},
        {0x7f020000, "Parent", {}, true, 0, {{0x7f010000, {0x10, 1}}, {0x7f010001, {0x10, 2}}}, {}, 0x80},
        {0x7f020001,
         "Child",
         {},
         true,
         0x7f020000,
         {{0x7f010001, {0x10, 20}}, {0x7f010002, {1, 0x7f040000}}},
         {},
         0x100},
        {0x7f020002, "Cycle", {}, true, 0x7f020002, {}},
        {0x7f040000, "answer", {0x10, 42}, false, 0, {}, {}, 0x200},
        {0x7f040001, "alias", {1, 0x7f040000}, false, 0, {}, {}, 0x400}};
    auto source = OwnedSource(ResourceTable(entries));
    res::ResTable t;
    t.add(std::make_shared<res::ResTableData>(source), 2);
    CHECK(t.identifierForName("fixture:attr/private_name") == 0);
    CHECK(t.identifierForName("@fixture:^attr-private/private_name") == 0x7f060000);
    auto b = t.getBagLocked(0x7f020001);
    REQUIRE(b);
    CHECK(b->entries.size() == 3);
    CHECK(b->typeSpecFlags == 0x180);
    CHECK(b->entries[1].value.value.data == 20);
    CHECK_THROWS_AS(static_cast<void>(t.getBagLocked(0x7f020002)), res::ResourceError);
    auto v = res::loadResourceValue(t, 0x7f040001, 0, true);
    REQUIRE(v);
    CHECK(v->value.data == 42);
    CHECK(v->resourceId == 0x7f040000);
    CHECK(v->cookie == 2);
    CHECK(v->changingConfigurations == 0x600);
    res::Theme theme(t);
    theme.applyStyle(0x7f020000, true);
    theme.applyStyle(0x7f020001, false);
    CHECK(theme.getAttribute(0x7f010001)->value.data == 2);
    theme.applyStyle(0x7f020001, true);
    CHECK(theme.getAttribute(0x7f010001)->value.data == 20);
    CHECK(theme.getAttribute(0x7f010001)->changingConfigurations == 0x180);
    auto resolved = theme.resolveAttributeReference(*theme.getAttribute(0x7f010002));
    CHECK(resolved.value.data == 42);
    CHECK(resolved.changingConfigurations == 0x380);
}

TEST_CASE("resources core validates entries once per type chunk on first access") {
    const std::array rows{Entry{.id = 0x7f010000, .name = "first", .value = {16, 7}},
                          Entry{.id = 0x7f020000, .name = "second", .value = {16, 9}}};
    auto bytes = ResourceTable(rows);
    const auto read16 = [&](std::size_t at) {
        return ogplay::core::ReadLittleEndian<std::uint16_t>(std::span<const std::byte>(bytes), at);
    };
    const auto read32 = [&](std::size_t at) {
        return ogplay::core::ReadLittleEndian<std::uint32_t>(std::span<const std::byte>(bytes), at);
    };
    const auto package = 12U + read32(16);
    auto chunk = package + read16(package + 2);
    while (read16(chunk) != 0x0201)
        chunk += read32(chunk + 4);
    const auto entry = chunk + read32(chunk + 16) + read32(chunk + read16(chunk + 2));
    bytes.at(entry) = std::byte{0};  // Invalid entry header, in the first type only.
    res::ResTable table;
    CHECK_NOTHROW(table.add(std::make_shared<res::ResTableData>(OwnedSource(bytes)), 1));
    REQUIRE(table.getResource(0x7f020000));
    CHECK(table.getResource(0x7f020000)->value.data == 9);
    CHECK_THROWS_AS(static_cast<void>(table.getResource(0x7f010000)), res::ResourceError);
    CHECK_THROWS_AS(static_cast<void>(table.getResource(0x7f010000)), res::ResourceError);

    res::ResTable valid;
    valid.add(std::make_shared<res::ResTableData>(OwnedSource(ResourceTable(rows))), 1);
    std::atomic<bool> correct{true};
    std::vector<std::thread> readers;
    for (int i = 0; i < 4; ++i)
        readers.emplace_back([&] {
            try {
                for (int n = 0; n < 100; ++n)
                    if (valid.getResource(0x7f010000)->value.data != 7) correct = false;
            } catch (...) {
                correct = false;
            }
        });
    for (auto& reader : readers)
        reader.join();
    CHECK(correct.load());
}
