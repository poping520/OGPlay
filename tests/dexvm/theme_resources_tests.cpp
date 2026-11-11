#include "android_value_fixture.h"
#include "runtime/integration/dexvm_android/shared.h"
#include "dexvm/resource_fixture.h"
#include "ogplay/runtime/raster/bitmap.h"
#include "container_traversal_fixture.h"
#include "file_vfs_fixture.h"
#include "ogplay/loader/apk.h"

#include <bit>
#include "../fixtures/bitmap_decode_data.h"
#include <fstream>

using namespace ogplay::runtime;

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

void Set32(std::vector<std::byte>& bytes, const std::size_t offset, const std::uint32_t value) {
    for (unsigned shift = 0; shift < 32; shift += 8) {
        bytes[offset + shift / 8U] = static_cast<std::byte>((value >> shift) & 0xffU);
    }
}

void AppendAscii(std::vector<std::byte>& bytes, const std::string_view text) {
    for (const auto value : text) {
        bytes.push_back(static_cast<std::byte>(value));
    }
}

std::vector<std::byte> StringPool(const std::vector<std::string>& strings, const bool utf8) {
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
    while (data.size() % 4U != 0)
        data.push_back(std::byte{0});

    std::vector<std::byte> chunk;
    Append16(chunk, 0x0001);
    Append16(chunk, 28);
    Append32(chunk, 0);
    Append32(chunk, static_cast<std::uint32_t>(strings.size()));
    Append32(chunk, 0);
    Append32(chunk, utf8 ? 0x100U : 0U);
    Append32(chunk, static_cast<std::uint32_t>(28U + offsets.size() * 4U));
    Append32(chunk, 0);
    for (const auto offset : offsets)
        Append32(chunk, offset);
    chunk.insert(chunk.end(), data.begin(), data.end());
    Set32(chunk, 4, static_cast<std::uint32_t>(chunk.size()));
    return chunk;
}

constexpr std::uint32_t a = 0x7f010001, b = 0x7f010002, c = 0x7f010003, def = 0x7f010004;
constexpr std::uint32_t theme_id = 0x7f020001, default_id = 0x7f020002, xml_style_id = 0x7f020003;

ogplay::test::ResourceFixtureEntry Style(std::uint32_t id, std::uint32_t parent,
                                         std::vector<ogplay::test::ResourceFixtureBagValue> bag) {
    ogplay::test::ResourceFixtureEntry e;
    e.resource_id = id;
    e.type_name = "style";
    e.entry_name = "fixture";
    e.is_complex = true;
    e.parent = parent;
    e.bag = std::move(bag);
    return e;
}

std::vector<std::byte> ThemeXml() {
    auto pool = StringPool({"root", "style", "a", "text", "null", "from XML"}, true);
    std::vector<std::byte> bytes;
    Append16(bytes, 3);
    Append16(bytes, 8);
    Append32(bytes, 0);
    bytes.insert(bytes.end(), pool.begin(), pool.end());
    Append16(bytes, 0x0180);
    Append16(bytes, 8);
    Append32(bytes, 28);
    for (auto id : {0U, 0U, a, b, c})
        Append32(bytes, id);
    Append16(bytes, 0x0102);
    Append16(bytes, 16);
    Append32(bytes, 116);
    Append32(bytes, 1);
    Append32(bytes, 0xffffffffU);
    Append32(bytes, 0xffffffffU);
    Append32(bytes, 0);
    Append16(bytes, 20);
    Append16(bytes, 20);
    Append16(bytes, 4);
    Append16(bytes, 0);
    Append16(bytes, 0);
    Append16(bytes, 1);
    for (auto [name, type, data] : std::array<std::array<std::uint32_t, 3>, 4>{
             {{{1, 1, xml_style_id}}, {{2, 16, 40}}, {{3, 3, 5}}, {{4, 1, 0}}}}) {
        Append32(bytes, 0xffffffffU);
        Append32(bytes, name);
        Append32(bytes, 0xffffffffU);
        Append16(bytes, 8);
        bytes.push_back(std::byte{0});
        bytes.push_back(static_cast<std::byte>(type));
        Append32(bytes, data);
    }
    Append16(bytes, 0x0103);
    Append16(bytes, 16);
    Append32(bytes, 24);
    Append32(bytes, 1);
    Append32(bytes, 0xffffffffU);
    Append32(bytes, 0xffffffffU);
    Append32(bytes, 0);
    Set32(bytes, 4, static_cast<std::uint32_t>(bytes.size()));
    return bytes;
}

ogplay::test::ResourceFixtureTable Table(std::vector<ogplay::test::ResourceFixtureEntry> extra = {},
                                         std::vector<std::byte>* serialized = nullptr) {
    ogplay::test::ResourceFixtureTable t;
    t.package_id = 0x7f;
    t.entries.push_back(Style(theme_id, 0x01030005, {{a, 16, 10, {}}, {b, 16, 11, {}}, {def, 1, default_id, {}}}));
    t.entries.push_back(Style(default_id, 0, {{a, 16, 20, {}}, {b, 16, 21, {}}}));
    t.entries.push_back(Style(xml_style_id, default_id, {{a, 16, 30, {}}, {c, 2, b, {}}}));
    t.entries.insert(t.entries.end(), extra.begin(), extra.end());
    // Serialize the APK style bags and pass through the production ARSC reader.
    auto global = StringPool({"from APK"}, true);
    auto types = StringPool({"attr", "style"}, true);
    std::vector<std::string> names;
    for (std::size_t i = 0; i < t.entries.size(); ++i)
        names.push_back("style" + std::to_string(i));
    auto keys = StringPool(names, true);
    std::vector<std::byte> chunk;
    Append16(chunk, 0x0201);
    Append16(chunk, 56);
    Append32(chunk, 0);
    chunk.push_back(std::byte{2});
    chunk.push_back(std::byte{0});
    Append16(chunk, 0);
    Append32(chunk, static_cast<std::uint32_t>(t.entries.size() + 1));
    Append32(chunk, static_cast<std::uint32_t>(56 + (t.entries.size() + 1) * 4));
    Append32(chunk, 36);
    chunk.resize(56);
    Append32(chunk, 0xffffffffU);
    std::uint32_t offset{};
    for (const auto& e : t.entries) {
        Append32(chunk, offset);
        offset += 16 + static_cast<std::uint32_t>(e.bag.size()) * 12;
    }
    for (std::size_t i = 0; i < t.entries.size(); ++i) {
        const auto& e = t.entries[i];
        Append16(chunk, 16);
        Append16(chunk, 1);
        Append32(chunk, static_cast<std::uint32_t>(i));
        Append32(chunk, e.parent);
        Append32(chunk, static_cast<std::uint32_t>(e.bag.size()));
        for (const auto& item : e.bag) {
            Append32(chunk, item.name);
            Append16(chunk, 8);
            chunk.push_back(std::byte{0});
            chunk.push_back(static_cast<std::byte>(item.value_type));
            Append32(chunk, item.value_data);
        }
    }
    Set32(chunk, 4, static_cast<std::uint32_t>(chunk.size()));
    std::vector<std::byte> package;
    Append16(package, 0x0200);
    Append16(package, 288);
    Append32(package, 0);
    Append32(package, 0x7f);
    for (char c : std::string_view("fixture"))
        Append16(package, static_cast<std::uint16_t>(c));
    package.resize(268);
    Append32(package, 288);
    Append32(package, 0);
    Append32(package, 288 + static_cast<std::uint32_t>(types.size()));
    Append32(package, 0);
    Append32(package, 0);
    for (const auto* part : {&types, &keys, &chunk})
        package.insert(package.end(), part->begin(), part->end());
    Set32(package, 4, static_cast<std::uint32_t>(package.size()));
    std::vector<std::byte> bytes;
    Append16(bytes, 2);
    Append16(bytes, 12);
    Append32(bytes, 12 + static_cast<std::uint32_t>(global.size() + package.size()));
    Append32(bytes, 1);
    for (const auto* part : {&global, &package})
        bytes.insert(bytes.end(), part->begin(), part->end());
    if (serialized) *serialized = bytes;
    t.global_string_pool = std::move(global);
    return t;
}

}  // namespace

TEST_CASE("API19 ContextImpl default theme underlies an activity theme with an inert ROM parent") {
    using ogplay::test::container_traversal::ContainerVm;
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        CAPTURE(backend);
        ContainerVm f(backend);
        ogplay::test::ResourceFixture(*f.context) = Table();
        ogplay::test::ResourceFixture(*f.context)
            .entries.push_back(Style(0x7f020020, 0x0103022f, {{0x01010064, 1, 0, {}}}));
        f.context->application_theme = 0x7f020020;
        const auto owner = f.New("Landroid/app/Activity;");
        const auto owner_root = f.vm.ProtectReferences(std::array{owner});
        ogplay::test::AttachViewTestContext(f.vm, owner, f.context);
        f.On(owner, "setTheme", "(I)V", {VmValue::Int(0)});
        f.On(owner, "setTheme", "(I)V", {VmValue::Int(0x7f020020)});
        const auto theme = f.On(owner, "getTheme", "()Landroid/content/res/Resources$Theme;").ref;
        const auto value = f.New("Landroid/util/TypedValue;");
        const auto roots = f.vm.ProtectReferences(std::array{theme, value});
        // The manifest theme is layered by Activity.setTheme, never used as the base.
        CHECK(f.On(theme, "resolveAttribute", "(ILandroid/util/TypedValue;Z)Z",
                   {VmValue::Int(0x01010078), VmValue::Ref(value), VmValue::Int(0)})
                  .AsInt() == 1);
        const auto type = f.linker.FindFieldRecursive(f.model.ObjectClass(value), "type", "I");
        REQUIRE(type);
        CHECK(f.model.InstanceSlots(value)[f.linker.Field(*type).slot].bits == 1);
    }
}

TEST_CASE("DVM-288 ROM Theme and TypedArray execute on both interpreter backends") {
    using ogplay::test::container_traversal::ContainerVm;
    using namespace ogplay::runtime::dexvm;
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ContainerVm f(backend);
        ogplay::test::ResourceFixture(*f.context) = Table();
        ogplay::test::ResourceFixture(*f.context).global_string_pool = StringPool({"from APK"}, true);
        ogplay::test::ResourceFixture(*f.context)
            .entries.push_back(Style(0x7f020010, theme_id,
                                     {{a, 16, 42, {}},
                                      {b, 18, 1, {}},
                                      {c, 4, 0x3fc00000, {}},
                                      {0x7f010005, 5, 0x0a01, {}},
                                      {0x7f010006, 1, 0, {}}}));
        const auto owner = f.New("Landroid/content/Context;");
        ogplay::test::AttachViewTestContext(f.vm, owner, f.context);
        const auto resources = f.On(owner, "getResources", "()Landroid/content/res/Resources;").ref;
        const auto roots = f.vm.ProtectReferences(std::array{owner, resources});
        const auto theme = f.On(resources, "newTheme", "()Landroid/content/res/Resources$Theme;").ref;
        const auto copy = f.On(resources, "newTheme", "()Landroid/content/res/Resources$Theme;").ref;
        const auto theme_roots = f.vm.ProtectReferences(std::array{theme, copy});
        f.On(theme, "applyStyle", "(IZ)V", {VmValue::Int(0x7f020010), VmValue::Int(1)});
        f.On(copy, "setTo", "(Landroid/content/res/Resources$Theme;)V", {VmValue::Ref(theme)});
        const auto attrs = f.model.NewPrimitiveArray(f.linker.ResolveDescriptor("[I"), JniPrimitiveKind::integer, 7);
        const std::array<std::uint32_t, 7> requested{a, b, c, 0x7f010005, 0x7f010006, 0x7f010007, 0x010100d5};
        for (std::size_t i = 0; i < requested.size(); ++i)
            f.model.SetPrimitiveElement(attrs, static_cast<JniSize>(i), requested[i]);
        const auto array_root = f.vm.ProtectReferences(std::array{attrs});
        const auto typed =
            f.On(theme, "obtainStyledAttributes", "([I)Landroid/content/res/TypedArray;", {VmValue::Ref(attrs)}).ref;
        const auto typed_root = f.vm.ProtectReferences(std::array{typed});
        CHECK(f.On(typed, "getIndexCount", "()I").AsInt() == 4);
        for (int i = 0; i < 4; ++i)
            CHECK(f.On(typed, "getIndex", "(I)I", {VmValue::Int(i)}).AsInt() == i);
        CHECK(f.On(typed, "getInt", "(II)I", {VmValue::Int(0), VmValue::Int(-1)}).AsInt() == 42);
        CHECK(f.On(typed, "getBoolean", "(IZ)Z", {VmValue::Int(1), VmValue::Int(0)}).AsInt() == 1);
        CHECK(f.On(typed, "getFloat", "(IF)F", {VmValue::Int(2), VmValue::Float(0)}).AsFloat() == 1.5F);
        CHECK(f.On(typed, "getDimension", "(IF)F", {VmValue::Int(3), VmValue::Float(0)}).AsFloat() ==
              doctest::Approx(10));
        CHECK(f.On(typed, "getDimensionPixelOffset", "(II)I", {VmValue::Int(3), VmValue::Int(-1)}).AsInt() == 10);
        CHECK(f.On(typed, "getDimensionPixelSize", "(II)I", {VmValue::Int(3), VmValue::Int(-1)}).AsInt() == 10);
        CHECK(f.On(typed, "getLayoutDimension", "(II)I", {VmValue::Int(3), VmValue::Int(-1)}).AsInt() == 10);
        CHECK(f.On(typed, "getResourceId", "(II)I", {VmValue::Int(4), VmValue::Int(-9)}).AsInt() == -9);
        CHECK_FALSE(f.On(typed, "hasValue", "(I)Z", {VmValue::Int(4)}).AsInt());
        CHECK_FALSE(f.On(typed, "hasValue", "(I)Z", {VmValue::Int(6)}).AsInt());
        const auto peek = f.On(typed, "peekValue", "(I)Landroid/util/TypedValue;", {VmValue::Int(0)}).ref;
        const auto value = f.New("Landroid/util/TypedValue;");
        const auto values_root = f.vm.ProtectReferences(std::array{peek, value});
        CHECK(
            f.On(typed, "getValue", "(ILandroid/util/TypedValue;)Z", {VmValue::Int(0), VmValue::Ref(value)}).AsInt() ==
            1);
        CHECK(f.On(theme, "resolveAttribute", "(ILandroid/util/TypedValue;Z)Z",
                   {VmValue::Int(a), VmValue::Ref(value), VmValue::Int(1)})
                  .AsInt() == 1);
        for (const auto& [name, desc] : std::array<std::pair<const char*, const char*>, 4>{
                 {{"getInt", "(II)I"}, {"getIndexCount", "()I"}, {"getDimension", "(IF)F"}, {"recycle", "()V"}}}) {
            const auto method = f.linker.FindVtableIndex(f.model.ObjectClass(typed), name, desc);
            REQUIRE(method);
            CHECK(f.linker.Method(f.linker.Class(f.model.ObjectClass(typed)).vtable[*method]).code.has_value());
        }
        f.On(typed, "recycle", "()V");
        const auto reused =
            f.On(copy, "obtainStyledAttributes", "([I)Landroid/content/res/TypedArray;", {VmValue::Ref(attrs)}).ref;
        CHECK(reused == typed);
        f.On(theme, "applyStyle", "(IZ)V", {VmValue::Int(default_id), VmValue::Int(1)});
        CHECK(f.On(reused, "getInt", "(II)I", {VmValue::Int(0), VmValue::Int(-1)}).AsInt() == 42);
        // selectDefaultTheme is the unchanged ROM algorithm (targetSdk 10/11/14).
        CHECK(f.Static("Landroid/content/res/Resources;", "selectDefaultTheme", "(II)I",
                       {VmValue::Int(0), VmValue::Int(10)})
                  .AsInt() == 0x01030005);
        CHECK(f.Static("Landroid/content/res/Resources;", "selectDefaultTheme", "(II)I",
                       {VmValue::Int(0), VmValue::Int(11)})
                  .AsInt() == 0x0103006b);
        CHECK(f.Static("Landroid/content/res/Resources;", "selectDefaultTheme", "(II)I",
                       {VmValue::Int(0), VmValue::Int(14)})
                  .AsInt() == 0x01030128);
        CHECK(f.Static("Landroid/content/res/Resources;", "selectDefaultTheme", "(II)I",
                       {VmValue::Int(theme_id), VmValue::Int(1)})
                  .AsInt() == theme_id);
        CHECK(f.On(resources, "getIdentifier", "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)I",
                   {VmValue::Ref(f.vm.NewStringUtf8("Theme")), VmValue::Ref(f.vm.NewStringUtf8("style")),
                    VmValue::Ref(f.vm.NewStringUtf8("android"))})
                  .AsInt() == 0x01030005);
        // A real compiled XML parser feeds both native six-slot entry points.
        const auto xml_bytes = ThemeXml();
        const auto byte_array = f.model.NewPrimitiveArray(f.linker.ResolveDescriptor("[B"), JniPrimitiveKind::byte,
                                                          static_cast<JniSize>(xml_bytes.size()));
        for (std::size_t i = 0; i < xml_bytes.size(); ++i)
            f.model.SetPrimitiveElement(byte_array, static_cast<JniSize>(i),
                                        std::to_integer<unsigned char>(xml_bytes[i]));
        const auto block = f.New("Landroid/content/res/XmlBlock;", "([B)V", {VmValue::Ref(byte_array)});
        const auto parser = f.On(block, "newParser", "()Landroid/content/res/XmlResourceParser;").ref;
        const auto xml_roots = f.vm.ProtectReferences(std::array{block, parser});
        f.On(parser, "next", "()I");
        f.On(parser, "next", "()I");
        const auto xml_typed =
            f.On(theme, "obtainStyledAttributes", "(Landroid/util/AttributeSet;[III)Landroid/content/res/TypedArray;",
                 {VmValue::Ref(parser), VmValue::Ref(attrs), VmValue::Int(def), VmValue::Int(default_id)})
                .ref;
        const auto xml_typed_root = f.vm.ProtectReferences(std::array{xml_typed});
        CHECK(f.On(xml_typed, "getInt", "(II)I", {VmValue::Int(0), VmValue::Int(-1)}).AsInt() == 40);
        CHECK(f.vm.StringUtf8(f.On(xml_typed, "getString", "(I)Ljava/lang/String;", {VmValue::Int(1)}).ref) ==
              "from XML");
        CHECK(f.vm.StringUtf8(f.On(xml_typed, "getText", "(I)Ljava/lang/CharSequence;", {VmValue::Int(1)}).ref) ==
              "from XML");
        CHECK_FALSE(f.On(xml_typed, "hasValue", "(I)Z", {VmValue::Int(2)}).AsInt());
        auto field_ref = [&](VmObjectRef obj, const char* name, const char* type) {
            const auto field = f.linker.FindFieldRecursive(f.model.ObjectClass(obj), name, type);
            REQUIRE(field);
            return VmObjectRef{f.model.InstanceSlots(obj)[f.linker.Field(*field).slot].bits};
        };
        const auto raw_data = field_ref(xml_typed, "mData", "[I");
        const auto raw_indices = field_ref(xml_typed, "mIndices", "[I");
        const std::array<std::uint32_t, 18> expected{16, 40, 0xffffffffU, 0, 0, 0,           3, 5, 0xffffffffU,
                                                     0,  0,  0,           0, 0, 0xffffffffU, 0, 0, 0};
        for (std::size_t i = 0; i < expected.size(); ++i)
            CHECK(static_cast<std::uint32_t>(f.model.GetPrimitiveElement(raw_data, static_cast<JniSize>(i))) ==
                  expected[i]);
        CHECK(f.model.GetPrimitiveElement(raw_indices, 0) == 3);  // explicit a/text plus inherited dimension
        CHECK(f.model.GetPrimitiveElement(raw_indices, 1) == 0);
        CHECK(f.model.GetPrimitiveElement(raw_indices, 2) == 1);
        CHECK(f.model.GetPrimitiveElement(raw_indices, 3) == 3);
        const auto retrieved =
            f.On(resources, "obtainAttributes", "(Landroid/util/AttributeSet;[I)Landroid/content/res/TypedArray;",
                 {VmValue::Ref(parser), VmValue::Ref(attrs)})
                .ref;
        const auto retrieved_root = f.vm.ProtectReferences(std::array{retrieved});
        CHECK(f.On(retrieved, "getIndexCount", "()I").AsInt() == 2);
        CHECK_FALSE(f.On(retrieved, "hasValue", "(I)Z", {VmValue::Int(3)}).AsInt());  // no theme/default
        const auto retrieved_data = field_ref(retrieved, "mData", "[I");
        for (std::size_t i = 0; i < expected.size(); ++i)
            CHECK(static_cast<std::uint32_t>(f.model.GetPrimitiveElement(retrieved_data, static_cast<JniSize>(i))) ==
                  expected[i]);
        ogplay::test::ResourceFixture(*f.context).entries.push_back(Style(0x7f020011, 0, {{a, 3, 0, {}}}));
        const auto string_typed = f.On(theme, "obtainStyledAttributes", "(I[I)Landroid/content/res/TypedArray;",
                                       {VmValue::Int(0x7f020011), VmValue::Ref(attrs)})
                                      .ref;
        const auto string_root = f.vm.ProtectReferences(std::array{string_typed});
        CHECK(f.vm.StringUtf8(f.On(string_typed, "getString", "(I)Ljava/lang/String;", {VmValue::Int(0)}).ref) ==
              "from APK");
        const auto hits = f.ledger.Unimplemented();
        for (const auto& hit : hits)
            CHECK_MESSAGE(hit.count == 0, hit.id);
        CHECK(f.OnOutcome(reused, "getDrawable", "(I)Landroid/graphics/drawable/Drawable;", {VmValue::Int(0)})
                  .exception.IsValid());
        CHECK(f.OnOutcome(reused, "getColorStateList", "(I)Landroid/content/res/ColorStateList;", {VmValue::Int(0)})
                  .exception.IsValid());
        const auto unbound =
            f.OnOutcome(resources, "getIdentifier", "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)I",
                        {VmValue::Ref(f.vm.NewStringUtf8("Theme.Dialog.NoFrame")),
                         VmValue::Ref(f.vm.NewStringUtf8("style")), VmValue::Ref(f.vm.NewStringUtf8("android"))});
        CHECK_FALSE(unbound.exception.IsValid());
        CHECK(unbound.value.AsInt() != 0);
        CHECK_FALSE(
            f.OnOutcome(theme, "applyStyle", "(IZ)V", {VmValue::Int(0x0103ffff), VmValue::Int(1)}).exception.IsValid());
        const auto assets = f.On(resources, "getAssets", "()Landroid/content/res/AssetManager;").ref;
        const auto field = f.linker.FindFieldRecursive(f.model.ObjectClass(theme), "mTheme", "I");
        REQUIRE(field);
        const auto token = f.model.InstanceSlots(theme)[f.linker.Field(*field).slot].bits;
        const auto destroy = f.linker.FindDirectMethod(f.model.ObjectClass(assets), "deleteTheme", "(I)V");
        REQUIRE(destroy);
        f.Checked(
            f.vm.Call(*destroy, std::array{VmValue::Ref(assets), VmValue::Int(static_cast<std::int32_t>(token))}));
        CHECK(f.vm.Call(*destroy, std::array{VmValue::Ref(assets), VmValue::Int(static_cast<std::int32_t>(token))})
                  .exception.IsValid());
        CHECK(f.OnOutcome(theme, "resolveAttribute", "(ILandroid/util/TypedValue;Z)Z",
                          {VmValue::Int(a), VmValue::Ref(value), VmValue::Int(1)})
                  .exception.IsValid());
    }
}

TEST_CASE("DVM-289 ROM attribute constructor smoke") {
    using ogplay::test::container_traversal::ContainerVm;
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ContainerVm f(backend);
        ogplay::test::ResourceFixture(*f.context) = Table();
        ogplay::test::ResourceFixture(*f.context)
            .entries.push_back(Style(0x7f020020, theme_id, {{0x01010064, 1, 0, {}}, {0x01010065, 1, 0, {}}}));
        f.context->application_theme = 0x7f020020;
        const auto owner = f.New("Landroid/app/Activity;");
        ogplay::test::AttachViewTestContext(f.vm, owner, f.context);
        f.On(owner, "setTheme", "(I)V", {VmValue::Int(static_cast<std::int32_t>(f.context->application_theme))});
        for (const auto* type : {"Landroid/view/View;", "Ltest/HierarchyGroup;", "Landroid/widget/FrameLayout;",
                                 "Landroid/widget/LinearLayout;", "Landroid/widget/RelativeLayout;"}) {
            INFO(std::string(type));
            const auto view = f.New(type, "(Landroid/content/Context;Landroid/util/AttributeSet;)V",
                                    {VmValue::Ref(owner), VmValue::Ref(VmObjectRef{})});
            REQUIRE(view.IsValid());
        }
        CHECK(f.ledger.Unimplemented().empty());
    }
}

namespace {
using ogplay::test::container_traversal::ContainerVm;

struct XmlValue {
    std::uint32_t id;
    std::uint8_t type;
    std::uint32_t data;
};

std::vector<std::byte> AttributeXml(const std::vector<std::vector<XmlValue>>& tags) {
    std::vector<std::uint32_t> ids{0};
    for (const auto& attrs : tags)
        for (const auto& attr : attrs)
            if (std::ranges::find(ids, attr.id) == ids.end()) ids.push_back(attr.id);
    std::vector<std::string> names{"style"};
    for (std::size_t i = 1; i < ids.size(); ++i)
        names.push_back("a" + std::to_string(i));
    std::vector<std::byte> bytes;
    Append16(bytes, 3);
    Append16(bytes, 8);
    Append32(bytes, 0);
    const auto pool = StringPool(names, true);
    bytes.insert(bytes.end(), pool.begin(), pool.end());
    Append16(bytes, 0x0180);
    Append16(bytes, 8);
    Append32(bytes, static_cast<std::uint32_t>(8 + ids.size() * 4));
    for (const auto id : ids)
        Append32(bytes, id);
    for (const auto& attrs : tags) {
        Append16(bytes, 0x0102);
        Append16(bytes, 16);
        Append32(bytes, static_cast<std::uint32_t>(36 + attrs.size() * 20));
        Append32(bytes, 1);
        Append32(bytes, 0xffffffffU);
        Append32(bytes, 0xffffffffU);
        Append32(bytes, 0);
        Append16(bytes, 20);
        Append16(bytes, 20);
        Append16(bytes, static_cast<std::uint16_t>(attrs.size()));
        Append16(bytes, 0);
        Append16(bytes, 0);
        const auto style = std::ranges::find_if(attrs, [](const auto& a) { return a.id == 0; });
        Append16(bytes, style == attrs.end() ? 0 : static_cast<std::uint16_t>(style - attrs.begin() + 1));
        for (const auto& attr : attrs) {
            Append32(bytes, 0xffffffffU);
            Append32(bytes, static_cast<std::uint32_t>(std::ranges::find(ids, attr.id) - ids.begin()));
            Append32(bytes, 0xffffffffU);
            Append16(bytes, 8);
            bytes.push_back(std::byte{0});
            bytes.push_back(static_cast<std::byte>(attr.type));
            Append32(bytes, attr.data);
        }
    }
    for (std::size_t i = 0; i < tags.size(); ++i) {
        Append16(bytes, 0x0103);
        Append16(bytes, 16);
        Append32(bytes, 24);
        Append32(bytes, 1);
        Append32(bytes, 0xffffffffU);
        Append32(bytes, 0xffffffffU);
        Append32(bytes, 0);
    }
    Set32(bytes, 4, static_cast<std::uint32_t>(bytes.size()));
    return bytes;
}

Slot& JavaField(ContainerVm& f, VmObjectRef object, const char* name, const char* type = "I") {
    const auto field = f.linker.FindFieldRecursive(f.model.ObjectClass(object), name, type);
    REQUIRE(field);
    REQUIRE_FALSE(f.linker.Field(*field).is_unimplemented);
    return f.model.InstanceSlots(object)[f.linker.Field(*field).slot];
}

VmObjectRef OpenAttributeXml(ContainerVm& f, VmObjectRef owner, const std::vector<std::byte>& xml,
                             const std::vector<std::byte>& arsc = {}) {
    static_cast<void>(owner);
    if (!arsc.empty()) {
        ogplay::test::ResourceFixture(*f.context) = ogplay::test::ReadResourceFixture(arsc);
    }
    const auto array = f.model.NewPrimitiveArray(f.linker.ResolveDescriptor("[B"), JniPrimitiveKind::byte,
                                                 static_cast<JniSize>(xml.size()));
    f.model.WriteByteRegion(array, 0, xml);
    const auto root = f.vm.ProtectReferences(std::array{array});
    const auto block = f.New("Landroid/content/res/XmlBlock;", "([B)V", {VmValue::Ref(array)});
    const auto blockRoot = f.vm.ProtectReferences(std::array{block});
    const auto parser = f.On(block, "newParser", "()Landroid/content/res/XmlResourceParser;").ref;
    const auto parserRoot = f.vm.ProtectReferences(std::array{parser});
    f.On(block, "close", "()V");
    CHECK(f.linker.Class(f.model.ObjectClass(parser)).descriptor == "Landroid/content/res/XmlBlock$Parser;");
    CHECK(f.On(parser, "next", "()I").AsInt() == 0);
    CHECK(f.On(parser, "next", "()I").AsInt() == 2);
    return parser;
}
}  // namespace

TEST_CASE("DVM-351 DVM-289 XML attributes construct ROM containers and LayoutParams") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ContainerVm f(backend);
        ogplay::test::ResourceFixture(*f.context) =
            Table({Style(0x7f020004, theme_id, {{0x01010064, 1, 0, {}}, {0x01010065, 1, 0, {}}})});
        f.context->application_theme = 0x7f020004;
        const auto owner = f.New("Landroid/app/Activity;");
        ogplay::test::AttachViewTestContext(f.vm, owner, f.context);
        f.On(owner, "setTheme", "(I)V", {VmValue::Int(static_cast<std::int32_t>(f.context->application_theme))});
        ogplay::test::ResourceFixtureEntry id;
        id.resource_id = 0x7f030001;
        id.type_name = "id";
        id.value_type = 16;
        ogplay::test::ResourceFixture(*f.context).entries.push_back(id);
        const auto parser =
            OpenAttributeXml(f, owner,
                             AttributeXml({{
                                 {0x010100d0, 1, 0x7f030001},                       // id
                                 {0x010100dc, 16, 1},                               // invisible
                                 {0x010100ea, 18, 0},         {0x010100eb, 18, 0},  // group clip flags
                                 {0x010100ed, 18, 0},         {0x010100ee, 16, 3},
                                 {0x010100ef, 18, 0},         {0x010102ef, 18, 0},
                                 {0x0101010a, 18, 1},         {0x01010200, 16, 0x33},
                                 {0x0101042e, 18, 0},         {0x010102d4, 18, 1},
                                 {0x01010329, 16, 3},         {0x0101032a, 5, 0x701},
                                 {0x010101ff, 1, 0x7f030001}, {0x010100d5, 5, 0x501},  // padding 5dp
                                 {0x0101013f, 5, 0x1f01},                              // minWidth 31dp
                                 {0x01010140, 5, 0x1701},                              // minHeight 23dp
                                 {0x010100c4, 16, 1},                                  // vertical
                                 {0x010100af, 16, 0x55},                               // gravity
                                 {0x01010128, 4, 0x40800000},                          // weightSum 4
                                 {0x01010126, 18, 0},  // baselineAligned false (verified against pinned IDs below)
                                 {0x010100f4, 5, 0x2801},     {0x010100f5, 5, 0x1401},  // width/height 40x20
                                 {0x010100f6, 5, 0x301},                                // margin 3
                                 {0x010100b3, 16, 0x33},                                // layout gravity
                                 {0x0101017f, 5, 0x901},      {0x01010180, 5, 0xb01},
                                 {0x01010181, 4, 0x40000000}  // layout weight 2
                             }}));
        const auto roots = f.vm.ProtectReferences(std::array{owner, parser});
        for (const auto* type :
             {"Landroid/view/View;", "Ltest/HierarchyGroup;", "Landroid/widget/FrameLayout;",
              "Landroid/widget/LinearLayout;", "Landroid/widget/RelativeLayout;", "Landroid/widget/AbsoluteLayout;"}) {
            INFO(std::string(type));
            const auto view = f.New(type, "(Landroid/content/Context;Landroid/util/AttributeSet;)V",
                                    {VmValue::Ref(owner), VmValue::Ref(parser)});
            const auto view_root = f.vm.ProtectReferences(std::array{view});
            CHECK(JavaField(f, view, "mID").bits == 0x7f030001);
            CHECK(JavaField(f, view, "mPaddingLeft").bits == 5);
            CHECK(JavaField(f, view, "mMinWidth").bits == 31);
            CHECK(JavaField(f, view, "mMinHeight").bits == 23);
            CHECK((JavaField(f, view, "mViewFlags").bits & 0x0cU) == 4);
            if (std::string_view(type) != "Landroid/view/View;") {
                CHECK((JavaField(f, view, "mGroupFlags").bits & (0x1U | 0x2U | 0x40U | 0x4000U | 0x200000U)) == 0);
                CHECK(JavaField(f, view, "mPersistentDrawingCache").bits == 3);
            }
            if (std::string_view(type) == "Landroid/widget/AbsoluteLayout;") {
                const auto styled = f.New(type, "(Landroid/content/Context;Landroid/util/AttributeSet;I)V",
                                          {VmValue::Ref(owner), VmValue::Ref(parser), VmValue::Int(0)});
                CHECK(JavaField(f, styled, "mPaddingLeft").bits == 5);
                const auto generated =
                    f.On(view, "generateLayoutParams",
                         "(Landroid/util/AttributeSet;)Landroid/view/ViewGroup$LayoutParams;", {VmValue::Ref(parser)})
                        .ref;
                CHECK(JavaField(f, generated, "width").bits == 40);
                CHECK(JavaField(f, generated, "height").bits == 20);
                CHECK(JavaField(f, generated, "x").bits == 9);
                CHECK(JavaField(f, generated, "y").bits == 11);
            }
            if (std::string_view(type) == "Landroid/widget/FrameLayout;") {
                CHECK(JavaField(f, view, "mMeasureAllChildren", "Z").bits == 1);
                CHECK(JavaField(f, view, "mForegroundGravity").bits == 0x33);
                CHECK(JavaField(f, view, "mForegroundInPadding", "Z").bits == 0);
                CHECK(JavaField(f, view, "mForeground", "Landroid/graphics/drawable/Drawable;").bits == 0);
            }
            if (std::string_view(type) == "Landroid/widget/RelativeLayout;") {
                CHECK(JavaField(f, view, "mGravity").bits == 0x55);
                CHECK(JavaField(f, view, "mIgnoreGravity").bits == 0x7f030001);
            }
            if (std::string_view(type) == "Landroid/widget/LinearLayout;") {
                CHECK(JavaField(f, view, "mOrientation").bits == 1);
                CHECK(JavaField(f, view, "mGravity").bits == 0x55);
                CHECK(JavaField(f, view, "mWeightSum", "F").bits == 0x40800000);
                CHECK(JavaField(f, view, "mBaselineAligned", "Z").bits == 0);
                CHECK(JavaField(f, view, "mUseLargestChild", "Z").bits == 1);
                CHECK(JavaField(f, view, "mShowDividers").bits == 3);
                CHECK(JavaField(f, view, "mDividerPadding").bits == 7);
                CHECK(JavaField(f, view, "mDivider", "Landroid/graphics/drawable/Drawable;").bits == 0);
            }
        }
        for (const auto* type :
             {"Landroid/view/ViewGroup$LayoutParams;", "Landroid/view/ViewGroup$MarginLayoutParams;",
              "Landroid/widget/FrameLayout$LayoutParams;", "Landroid/widget/LinearLayout$LayoutParams;",
              "Landroid/widget/RelativeLayout$LayoutParams;", "Landroid/widget/AbsoluteLayout$LayoutParams;"}) {
            INFO(std::string(type));
            const auto lp = f.New(type, "(Landroid/content/Context;Landroid/util/AttributeSet;)V",
                                  {VmValue::Ref(owner), VmValue::Ref(parser)});
            CHECK(JavaField(f, lp, "width").bits == 40);
            CHECK(JavaField(f, lp, "height").bits == 20);
            if (std::string_view(type) == "Landroid/widget/FrameLayout$LayoutParams;" ||
                std::string_view(type) == "Landroid/widget/LinearLayout$LayoutParams;")
                CHECK(JavaField(f, lp, "gravity").bits == 0x33);
            if (std::string_view(type) == "Landroid/widget/LinearLayout$LayoutParams;")
                CHECK(JavaField(f, lp, "weight", "F").bits == 0x40000000);
            if (std::string_view(type) == "Landroid/widget/AbsoluteLayout$LayoutParams;") {
                CHECK(JavaField(f, lp, "x").bits == 9);
                CHECK(JavaField(f, lp, "y").bits == 11);
            } else if (std::string_view(type) != "Landroid/view/ViewGroup$LayoutParams;") {
                CHECK(JavaField(f, lp, "leftMargin").bits == 3);
                CHECK(JavaField(f, lp, "bottomMargin").bits == 3);
            }
        }
        CHECK(f.ledger.Unimplemented().empty());
    }
}

TEST_CASE("DVM-289 container priority follows XML style default and application theme") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ContainerVm f(backend);
        std::vector<std::byte> table_bytes;
        ogplay::test::ResourceFixture(*f.context) = Table(
            {Style(0x7f020004, theme_id,
                   {{0x01010064, 1, 0, {}}, {0x01010065, 1, 0, {}}, {0x010100c4, 16, 1, {}}, {def, 1, 0x7f020005, {}}}),
             Style(0x7f020005, 0, {{0x010100c4, 16, 0, {}}}), Style(0x7f020006, 0, {{0x010100c4, 16, 1, {}}})},
            &table_bytes);
        f.context->application_theme = 0x7f020004;
        const auto owner = f.New("Landroid/app/Activity;");
        ogplay::test::AttachViewTestContext(f.vm, owner, f.context);
        f.On(owner, "setTheme", "(I)V", {VmValue::Int(static_cast<std::int32_t>(f.context->application_theme))});
        ogplay::test::ResourceFixtureEntry id;
        id.resource_id = 0x7f030001;
        id.type_name = "id";
        id.value_type = 16;
        ogplay::test::ResourceFixture(*f.context).entries.push_back(id);
        const auto parser = OpenAttributeXml(f, owner,
                                             AttributeXml({{},
                                                           {},
                                                           {{0, 1, 0x7f020006}},
                                                           {{0, 1, 0x7f020006}, {0x010100c4, 16, 0}},
                                                           {{0, 1, 0x7f020005}, {0x010100c4, 16, 1}}}),
                                             table_bytes);
        const auto roots = f.vm.ProtectReferences(std::array{owner, parser});
        const std::array<int, 5> expected{1, 0, 1, 0, 1};
        for (std::size_t i = 0; i < expected.size(); ++i) {
            if (i) CHECK(f.On(parser, "next", "()I").AsInt() == 2);
            const auto view = f.New(
                "Landroid/widget/LinearLayout;", "(Landroid/content/Context;Landroid/util/AttributeSet;I)V",
                {VmValue::Ref(owner), VmValue::Ref(parser), VmValue::Int(i == 0 ? 0 : static_cast<std::int32_t>(def))});
            CHECK(JavaField(f, view, "mOrientation").bits == static_cast<std::uint32_t>(expected[i]));
            CHECK(JavaField(f, view, "mWeightSum", "F").bits == std::bit_cast<std::uint32_t>(-1.0F));
        }
        CHECK(f.ledger.Unimplemented().empty());
    }
}

TEST_CASE("DVM-289 Relative LayoutParams read all 23 attributes from Parser") {
    const std::array<std::pair<std::uint32_t, int>, 22> rules{{
        {0x01010182, 0},   // layout_toLeftOf
        {0x01010183, 1},   // layout_toRightOf
        {0x01010184, 2},   // layout_above
        {0x01010185, 3},   // layout_below
        {0x01010186, 4},   // layout_alignBaseline
        {0x01010187, 5},   // layout_alignLeft
        {0x01010188, 6},   // layout_alignTop
        {0x01010189, 7},   // layout_alignRight
        {0x0101018a, 8},   // layout_alignBottom
        {0x0101018b, 9},   // layout_alignParentLeft
        {0x0101018c, 10},  // layout_alignParentTop
        {0x0101018d, 11},  // layout_alignParentRight
        {0x0101018e, 12},  // layout_alignParentBottom
        {0x0101018f, 13},  // layout_centerInParent
        {0x01010190, 14},  // layout_centerHorizontal
        {0x01010191, 15},  // layout_centerVertical
        {0x010103b7, 16},  // layout_toStartOf
        {0x010103b8, 17},  // layout_toEndOf
        {0x010103b9, 18},  // layout_alignStart
        {0x010103ba, 19},  // layout_alignEnd
        {0x010103bb, 20},  // layout_alignParentStart
        {0x010103bc, 21},  // layout_alignParentEnd
    }};
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ContainerVm f(backend);
        ogplay::test::ResourceFixture(*f.context) =
            Table({Style(0x7f020004, theme_id, {{0x01010064, 1, 0, {}}, {0x01010065, 1, 0, {}}})});
        f.context->application_theme = 0x7f020004;
        const auto owner = f.New("Landroid/app/Activity;");
        ogplay::test::AttachViewTestContext(f.vm, owner, f.context);
        f.On(owner, "setTheme", "(I)V", {VmValue::Int(static_cast<std::int32_t>(f.context->application_theme))});
        std::vector<XmlValue> attrs{{0x010100f4, 16, 0xfffffffe}, {0x010100f5, 16, 0xffffffff}, {0x01010192, 18, 1}};
        for (const auto& [id, verb] : rules) {
            const bool boolean = (verb >= 9 && verb <= 15) || verb >= 20;
            attrs.push_back({id, static_cast<std::uint8_t>(boolean ? 18 : 1),
                             boolean ? 1U : 0x7f030100U + static_cast<std::uint32_t>(verb)});
            if (!boolean) {
                ogplay::test::ResourceFixtureEntry value;
                value.resource_id = 0x7f030100U + static_cast<std::uint32_t>(verb);
                value.type_name = "id";
                value.value_type = 16;
                ogplay::test::ResourceFixture(*f.context).entries.push_back(value);
            }
        }
        const auto parser = OpenAttributeXml(f, owner, AttributeXml({attrs}));
        const auto roots = f.vm.ProtectReferences(std::array{owner, parser});
        const auto lp = f.New("Landroid/widget/RelativeLayout$LayoutParams;",
                              "(Landroid/content/Context;Landroid/util/AttributeSet;)V",
                              {VmValue::Ref(owner), VmValue::Ref(parser)});
        const auto values = VmObjectRef{JavaField(f, lp, "mRules", "[I").bits};
        CHECK(JavaField(f, lp, "width").bits == 0xfffffffe);
        CHECK(JavaField(f, lp, "height").bits == 0xffffffff);
        CHECK(JavaField(f, lp, "alignWithParent", "Z").bits == 1);
        for (const auto& [id, verb] : rules) {
            static_cast<void>(id);
            const bool boolean = (verb >= 9 && verb <= 15) || verb >= 20;
            CHECK(f.model.GetPrimitiveElement(values, verb) ==
                  (boolean ? 0xffffffffU : 0x7f030100U + static_cast<std::uint32_t>(verb)));
        }
        CHECK(f.ledger.Unimplemented().empty());
    }
}

TEST_CASE("DVM-289 missing dimensions and drawable resources use ROM failures") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ContainerVm f(backend);
        ogplay::test::ResourceFixture(*f.context) =
            Table({Style(0x7f020004, theme_id, {{0x01010064, 1, 0, {}}, {0x01010065, 1, 0, {}}})});
        f.context->application_theme = 0x7f020004;
        const auto owner = f.New("Landroid/app/Activity;");
        ogplay::test::AttachViewTestContext(f.vm, owner, f.context);
        f.On(owner, "setTheme", "(I)V", {VmValue::Int(0x7f020004)});
        const auto parser = OpenAttributeXml(f, owner,
                                             AttributeXml({{},
                                                           {{0x010100f4, 3, 0}, {0x010100f5, 16, 10}},
                                                           {{0x010100d4, 16, 42}},
                                                           {{0x01010109, 16, 42}},
                                                           {{0x01010129, 16, 42}},
                                                           {{0x01010064, 16, 42}},
                                                           {{0x010100de, 16, 1}}}));
        const auto roots = f.vm.ProtectReferences(std::array{owner, parser});
        // AOSP getLayoutDimension(index,name) requires both dimensions and a valid type.
        for (int i = 0; i < 2; ++i) {
            if (i) CHECK(f.On(parser, "next", "()I").AsInt() == 2);
            for (const auto* type :
                 {"Landroid/view/ViewGroup$LayoutParams;", "Landroid/view/ViewGroup$MarginLayoutParams;",
                  "Landroid/widget/FrameLayout$LayoutParams;", "Landroid/widget/LinearLayout$LayoutParams;",
                  "Landroid/widget/RelativeLayout$LayoutParams;"}) {
                const auto object = f.vm.NewIntrinsicInstance(type);
                const auto ctor = f.linker.FindDirectMethod(f.model.ObjectClass(object), "<init>",
                                                            "(Landroid/content/Context;Landroid/util/AttributeSet;)V");
                REQUIRE(ctor);
                const auto result =
                    f.vm.Call(*ctor, std::array{VmValue::Ref(object), VmValue::Ref(owner), VmValue::Ref(parser)});
                REQUIRE(result.exception.IsValid());
                CHECK(result.exception_message.find("layout_width") != std::string::npos);
            }
        }
        CHECK(f.ledger.Unimplemented().empty());
        for (const auto* type : {"Landroid/view/View;"}) {
            CHECK(f.On(parser, "next", "()I").AsInt() == 2);
            const auto object = f.vm.NewIntrinsicInstance(type);
            const auto ctor = f.linker.FindDirectMethod(f.model.ObjectClass(object), "<init>",
                                                        "(Landroid/content/Context;Landroid/util/AttributeSet;)V");
            REQUIRE(ctor);
            const auto result =
                f.vm.Call(*ctor, std::array{VmValue::Ref(object), VmValue::Ref(owner), VmValue::Ref(parser)});
            REQUIRE(result.exception.IsValid());
        }
        CHECK(f.ledger.Unimplemented().empty());
        CHECK_FALSE(f.OnOutcome(owner, "setTheme", "(I)V", {VmValue::Int(0x0103ffff)}).exception.IsValid());
    }
}

namespace {
struct InflateNode {
    std::string tag;
    std::vector<XmlValue> attrs;
    std::vector<InflateNode> children;
};

std::vector<std::byte> InflateXml(const InflateNode& root) {
    std::vector<std::string> strings{"layout_width", "layout_height", "layout",    "padding",
                                     "id",           "scaleType",     "background"};
    const auto intern = [&](const std::string& name) {
        auto it = std::ranges::find(strings, name);
        if (it == strings.end()) {
            strings.push_back(name);
            return static_cast<std::uint32_t>(strings.size() - 1);
        }
        return static_cast<std::uint32_t>(it - strings.begin());
    };
    const auto collect = [&](const auto& self, const InflateNode& node) -> void {
        intern(node.tag);
        for (const auto& child : node.children)
            self(self, child);
    };
    collect(collect, root);
    std::vector<std::byte> bytes;
    Append16(bytes, 3);
    Append16(bytes, 8);
    Append32(bytes, 0);
    const auto pool = StringPool(strings, true);
    bytes.insert(bytes.end(), pool.begin(), pool.end());
    Append16(bytes, 0x0180);
    Append16(bytes, 8);
    Append32(bytes, 36);
    for (auto id : {0x010100f4U, 0x010100f5U, 0U, 0x010100d5U, 0x010100d0U, 0x0101011dU, 0x010100d4U})
        Append32(bytes, id);
    const auto emit = [&](const auto& self, const InflateNode& node) -> void {
        const auto tag = intern(node.tag);
        Append16(bytes, 0x0102);
        Append16(bytes, 16);
        Append32(bytes, static_cast<std::uint32_t>(36 + node.attrs.size() * 20));
        Append32(bytes, 7);
        Append32(bytes, 0xffffffffU);
        Append32(bytes, 0xffffffffU);
        Append32(bytes, tag);
        Append16(bytes, 20);
        Append16(bytes, 20);
        Append16(bytes, static_cast<std::uint16_t>(node.attrs.size()));
        Append16(bytes, 0);
        Append16(bytes, 0);
        Append16(bytes, 0);
        for (const auto& attr : node.attrs) {
            const auto index = attr.id == 0x010100f4   ? 0U
                               : attr.id == 0x010100f5 ? 1U
                               : attr.id == 0          ? 2U
                               : attr.id == 0x010100d5 ? 3U
                               : attr.id == 0x0101011d ? 5U
                               : attr.id == 0x010100d4 ? 6U
                                                       : 4U;
            Append32(bytes, 0xffffffffU);
            Append32(bytes, index);
            Append32(bytes, 0xffffffffU);
            Append16(bytes, 8);
            bytes.push_back(std::byte{0});
            bytes.push_back(static_cast<std::byte>(attr.type));
            Append32(bytes, attr.data);
        }
        for (const auto& child : node.children)
            self(self, child);
        Append16(bytes, 0x0103);
        Append16(bytes, 16);
        Append32(bytes, 24);
        Append32(bytes, 7);
        Append32(bytes, 0xffffffffU);
        Append32(bytes, 0xffffffffU);
        Append32(bytes, tag);
    };
    emit(emit, root);
    Set32(bytes, 4, static_cast<std::uint32_t>(bytes.size()));
    return bytes;
}

std::vector<std::byte> InflateResourceTable(const ogplay::test::ResourceFixtureTable& table) {
    std::vector<std::string> values{"from APK"}, keys;
    for (const auto& entry : table.entries) {
        keys.push_back(entry.entry_name);
        if (entry.string_value) values.push_back(*entry.string_value);
    }
    std::vector<std::byte> chunks;
    for (const auto type : {2U, 3U, 4U, 5U, 6U}) {
        std::uint32_t count{};
        for (const auto& entry : table.entries)
            if (((entry.resource_id >> 16U) & 0xffU) == type)
                count = std::max(count, (entry.resource_id & 0xffffU) + 1);
        std::vector<std::uint32_t> offsets(count, 0xffffffffU);
        std::vector<std::byte> data;
        for (std::size_t i = 0; i < table.entries.size(); ++i) {
            const auto& entry = table.entries[i];
            if (((entry.resource_id >> 16U) & 0xffU) != type) continue;
            offsets[entry.resource_id & 0xffffU] = static_cast<std::uint32_t>(data.size());
            Append16(data, entry.is_complex ? 16 : 8);
            Append16(data, entry.is_complex ? 1 : 0);
            Append32(data, static_cast<std::uint32_t>(i));
            if (entry.is_complex) {
                Append32(data, entry.parent);
                Append32(data, static_cast<std::uint32_t>(entry.bag.size()));
                for (const auto& item : entry.bag) {
                    Append32(data, item.name);
                    Append16(data, 8);
                    data.push_back(std::byte{0});
                    data.push_back(static_cast<std::byte>(item.value_type));
                    Append32(data, item.value_data);
                }
            } else {
                Append16(data, 8);
                data.push_back(std::byte{0});
                data.push_back(static_cast<std::byte>(entry.value_type));
                Append32(data, entry.string_value ? static_cast<std::uint32_t>(
                                                        std::ranges::find(values, *entry.string_value) - values.begin())
                                                  : entry.value_data);
            }
        }
        std::vector<std::byte> chunk;
        Append16(chunk, 0x0201);
        Append16(chunk, 56);
        Append32(chunk, static_cast<std::uint32_t>(56 + offsets.size() * 4 + data.size()));
        chunk.push_back(static_cast<std::byte>(type));
        chunk.push_back(std::byte{0});
        Append16(chunk, 0);
        Append32(chunk, count);
        Append32(chunk, static_cast<std::uint32_t>(56 + offsets.size() * 4));
        Append32(chunk, 36);
        chunk.resize(56);
        for (const auto offset : offsets)
            Append32(chunk, offset);
        chunk.insert(chunk.end(), data.begin(), data.end());
        chunks.insert(chunks.end(), chunk.begin(), chunk.end());
    }
    auto global = StringPool(values, true),
         types = StringPool({"attr", "style", "layout", "id", "drawable", "color"}, true),
         names = StringPool(keys, true);
    std::vector<std::byte> package;
    Append16(package, 0x0200);
    Append16(package, 288);
    Append32(package, 0);
    Append32(package, 0x7f);
    for (char c : std::string_view("fixture"))
        Append16(package, static_cast<std::uint16_t>(c));
    package.resize(268);
    Append32(package, 288);
    Append32(package, 0);
    Append32(package, 288 + static_cast<std::uint32_t>(types.size()));
    Append32(package, 0);
    Append32(package, 0);
    for (const auto* part : {&types, &names, &chunks})
        package.insert(package.end(), part->begin(), part->end());
    Set32(package, 4, static_cast<std::uint32_t>(package.size()));
    std::vector<std::byte> bytes;
    Append16(bytes, 2);
    Append16(bytes, 12);
    Append32(bytes, static_cast<std::uint32_t>(12 + global.size() + package.size()));
    Append32(bytes, 1);
    for (const auto* part : {&global, &package})
        bytes.insert(bytes.end(), part->begin(), part->end());
    return bytes;
}

void InstallInflateLayouts(ContainerVm& f, const std::vector<InflateNode>& layouts,
                           std::vector<std::pair<std::string, std::vector<std::byte>>> files = {}) {
    std::erase_if(ogplay::test::ResourceFixture(*f.context).entries,
                  [](const auto& entry) { return entry.type_name == "layout"; });
    for (std::size_t i = 0; i < layouts.size(); ++i) {
        const auto path = "res/layout/inflate" + std::to_string(i) + ".xml";
        files.emplace_back(path, InflateXml(layouts[i]));
        ogplay::test::ResourceFixtureEntry entry;
        entry.resource_id = 0x7f030001U + static_cast<std::uint32_t>(i);
        entry.type_name = "layout";
        entry.entry_name = "inflate" + std::to_string(i);
        entry.string_value = path;
        entry.value_type = 3;
        ogplay::test::ResourceFixture(*f.context).entries.push_back(entry);
    }
    files.emplace_back("resources.arsc", InflateResourceTable(ogplay::test::ResourceFixture(*f.context)));
    std::vector<std::byte> zip, central;
    for (const auto& [path, bytes] : files) {
        const auto single = ogplay::test::file_vfs::MakeStoredZip(path, bytes);
        const auto local_size = 30U + path.size() + bytes.size();
        std::vector<std::byte> directory(single.begin() + static_cast<std::ptrdiff_t>(local_size), single.end() - 22);
        Set32(directory, 42, static_cast<std::uint32_t>(zip.size()));
        central.insert(central.end(), directory.begin(), directory.end());
        zip.insert(zip.end(), single.begin(), single.begin() + static_cast<std::ptrdiff_t>(local_size));
    }
    const auto offset = static_cast<std::uint32_t>(zip.size());
    zip.insert(zip.end(), central.begin(), central.end());
    Append32(zip, 0x06054b50);
    Append16(zip, 0);
    Append16(zip, 0);
    Append16(zip, static_cast<std::uint16_t>(files.size()));
    Append16(zip, static_cast<std::uint16_t>(files.size()));
    Append32(zip, static_cast<std::uint32_t>(central.size()));
    Append32(zip, offset);
    Append16(zip, 0);
    f.context->apk_bytes = std::move(zip);
    f.context->archive = ogplay::loader::ParseApkArchive(f.context->apk_bytes);
    const auto arsc = ogplay::loader::ReadApkEntry(f.context->apk_bytes, f.context->archive, "resources.arsc");
    ogplay::test::ResourceFixture(*f.context) = ogplay::test::ReadResourceFixture(arsc);
}

VmObjectRef InflaterActivity(ContainerVm& f) {
    ogplay::test::ResourceFixture(*f.context) = Table(
        {Style(0x7f020004, theme_id, {{0x01010064, 1, 0, {}}, {0x01010065, 1, 0, {}}, {0x0101013f, 5, 0x601, {}}})});
    ogplay::test::ResourceFixtureEntry id;
    id.resource_id = 0x7f040001;
    id.type_name = "id";
    id.entry_name = "included";
    id.value_type = 16;
    ogplay::test::ResourceFixture(*f.context).entries.push_back(id);
    f.context->application_theme = 0x7f020004;
    const auto owner = f.New("Landroid/app/Activity;");
    f.context->foreground_activity = owner;
    ogplay::test::AttachViewTestContext(f.vm, owner, f.context);
    f.On(owner, "setTheme", "(I)V", {VmValue::Int(0x7f020004)});
    return owner;
}

std::vector<XmlValue> InflateSize(std::uint32_t width, std::uint32_t height) {
    return {{0x010100f4, 16, width}, {0x010100f5, 16, height}};
}
}  // namespace

TEST_CASE("DVM-358 ROM inflater color backgrounds and bitmap admission preserve failure boundaries") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ContainerVm f(backend);
        ogplay::test::ResourceFixture(*f.context) = Table({Style(
            0x7f020004, theme_id, {{0x01010064, 1, 0, {}}, {0x01010065, 1, 0, {}}, {0x0101013f, 5, 0x601, {}}})});
        f.context->application_theme = 0x7f020004;
        constexpr std::uint32_t color = 0x7f060001, image = 0x7f050001, shape = 0x7f050002;
        ogplay::test::ResourceFixture(*f.context)
            .entries.push_back({.resource_id = color,
                                .type_name = "color",
                                .entry_name = "black",
                                .value_type = 28,
                                .value_data = 0xff000000});
        ogplay::test::ResourceFixture(*f.context)
            .entries.push_back({.resource_id = image,
                                .type_name = "drawable",
                                .entry_name = "bitmap",
                                .string_value = "res/drawable/fixture.png",
                                .value_type = 3});
        ogplay::test::ResourceFixture(*f.context)
            .entries.push_back({.resource_id = shape,
                                .type_name = "drawable",
                                .entry_name = "shape",
                                .string_value = "res/drawable/shape.xml",
                                .value_type = 3});
        std::vector<InflateNode> layouts;
        for (const auto background : std::vector<XmlValue>{{0x010100d4, 1, color},
                                                           {0x010100d4, 28, 0xff000000},
                                                           {0x010100d4, 1, image},
                                                           {0x010100d4, 1, 0x01080004},
                                                           {0x010100d4, 1, shape}}) {
            auto attrs = InflateSize(20, 10);
            attrs.push_back(background);
            layouts.push_back({"LinearLayout", attrs, {}});
        }
        InstallInflateLayouts(f, layouts,
                              {{"res/drawable/fixture.png", ogplay::test::BitmapDecodePng()},
                               {"res/drawable/shape.xml", InflateXml({"shape", {}, {}})}});
        // Publish the complete APK before Resources creates its immutable StringBlock pool.
        const auto owner = f.New("Landroid/app/Activity;");
        f.context->foreground_activity = owner;
        ogplay::test::AttachViewTestContext(f.vm, owner, f.context);
        f.On(owner, "setTheme", "(I)V", {VmValue::Int(0x7f020004)});
        const auto owner_root = f.vm.ProtectReferences(std::array{owner});
        const auto inflater = f.On(owner, "getLayoutInflater", "()Landroid/view/LayoutInflater;").ref;
        const auto inflater_root = f.vm.ProtectReferences(std::array{inflater});
        for (int i = 0; i < 5; ++i) {
            if (i == 2) {
                const auto res = f.On(owner, "getResources", "()Landroid/content/res/Resources;").ref;
                const auto background =
                    f.On(res, "getDrawable", "(I)Landroid/graphics/drawable/Drawable;", {VmValue::Int(image)}).ref;
                const auto root = f.vm.ProtectReferences(std::array{background});
                CHECK(f.linker.Class(f.model.ObjectClass(background)).descriptor ==
                      "Landroid/graphics/drawable/BitmapDrawable;");
                CHECK(f.On(background, "getIntrinsicWidth", "()I").AsInt() == 2);
                CHECK(f.On(background, "getIntrinsicHeight", "()I").AsInt() == 1);
                const auto bitmap = f.On(background, "getBitmap", "()Landroid/graphics/Bitmap;").ref;
                REQUIRE(bitmap.IsValid());
                CHECK(f.On(bitmap, "getWidth", "()I").AsInt() == 2);
                CHECK(f.On(bitmap, "getHeight", "()I").AsInt() == 1);
                CHECK(f.On(bitmap, "getDensity", "()I").AsInt() == 160);
                const auto& pixels = f.context->bitmaps.at(bitmap.Value()).pixels;
                REQUIRE(pixels);
                CHECK(pixels->Snapshot() == std::vector<std::uint32_t>{0xffff0000, 0xff0000ff});
                CHECK(f.ledger.Unimplemented().empty());
            }
            const auto gaps_before = f.ledger.Unimplemented();
            const auto result =
                f.OnOutcome(inflater, "inflate", "(ILandroid/view/ViewGroup;Z)Landroid/view/View;",
                            {VmValue::Int(0x7f030001 + i), VmValue::Ref(VmObjectRef{}), VmValue::Int(0)});
            INFO(i, result.exception_message, f.vm.DescribeThrowableCauses(result.exception));
            if (i == 3) {
                REQUIRE_FALSE(result.exception.IsValid());
                const auto bg = f.On(result.value.ref, "getBackground", "()Landroid/graphics/drawable/Drawable;").ref;
                REQUIRE(bg.IsValid());
                CHECK(f.linker.Class(f.model.ObjectClass(bg)).descriptor ==
                      "Landroid/graphics/drawable/StateListDrawable;");
                const auto gaps_after = f.ledger.Unimplemented();
                REQUIRE(gaps_after.size() == gaps_before.size());
                for (std::size_t n = 0; n < gaps_before.size(); ++n) {
                    CHECK(gaps_after[n].id == gaps_before[n].id);
                    CHECK(gaps_after[n].count == gaps_before[n].count);
                }
                continue;
            }
            if (i == 2) {
                // ROM BitmapDrawable.getOpacity lets View.computeOpaqueFlags accept a bitmap background.
                REQUIRE_FALSE(result.exception.IsValid());
                const auto bg = f.On(result.value.ref, "getBackground", "()Landroid/graphics/drawable/Drawable;").ref;
                REQUIRE(bg.IsValid());
                CHECK(f.linker.Class(f.model.ObjectClass(bg)).descriptor ==
                      "Landroid/graphics/drawable/BitmapDrawable;");
                CHECK(f.On(bg, "getOpacity", "()I").AsInt() == (i == 2 ? -3 : -1));
                CHECK(f.ledger.Unimplemented().empty());
                continue;
            }
            if (i > 2) {
                REQUIRE_FALSE(result.exception.IsValid());
                const auto bg = f.On(result.value.ref, "getBackground", "()Landroid/graphics/drawable/Drawable;").ref;
                CHECK(f.linker.Class(f.model.ObjectClass(bg)).descriptor ==
                      "Landroid/graphics/drawable/GradientDrawable;");
                continue;
            }
            CHECK_FALSE(result.exception.IsValid());
            if (result.exception.IsValid()) continue;
            const auto view_root = f.vm.ProtectReferences(std::array{result.value.ref});
            const auto background =
                f.On(result.value.ref, "getBackground", "()Landroid/graphics/drawable/Drawable;").ref;
            REQUIRE(background.IsValid());
            CHECK(f.linker.Class(f.model.ObjectClass(background)).descriptor ==
                  "Landroid/graphics/drawable/ColorDrawable;");
            CHECK(static_cast<std::uint32_t>(f.On(background, "getColor", "()I").AsInt()) == 0xff000000);
            CHECK(f.ledger.Unimplemented().empty());
        }
    }
}

TEST_CASE("DVM-290 createView cache filter exception causes and ROM factories") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ContainerVm f(backend);
        const auto owner = InflaterActivity(f);
        const auto inflater =
            f.Static("Landroid/view/LayoutInflater;", "from",
                     "(Landroid/content/Context;)Landroid/view/LayoutInflater;", {VmValue::Ref(owner)})
                .ref;
        CHECK(f.Static("Landroid/view/LayoutInflater;", "from",
                       "(Landroid/content/Context;)Landroid/view/LayoutInflater;", {VmValue::Ref(owner)})
                  .ref == inflater);
        const auto parser = OpenAttributeXml(f, owner, AttributeXml({{}}));
        const auto roots = f.vm.ProtectReferences(std::array{owner, inflater, parser});
        const auto args = VmObjectRef{JavaField(f, inflater, "mConstructorArgs", "[Ljava/lang/Object;").bits};
        f.model.SetObjectElement(args, 0, owner);
        const auto create = [&](const char* name) {
            const auto text = f.vm.NewStringUtf8(name);
            return f.OnOutcome(inflater, "createView",
                               "(Ljava/lang/String;Ljava/lang/String;Landroid/util/AttributeSet;)Landroid/view/View;",
                               {VmValue::Ref(text), VmValue::Ref(VmObjectRef{}), VmValue::Ref(parser)});
        };
        CHECK(f.Checked(create("test.InflatedLeaf")).ref.IsValid());
        const auto inflater_type = f.linker.ResolveDescriptor("Landroid/view/LayoutInflater;");
        const auto cache_field = f.linker.FindFieldRecursive(inflater_type, "sConstructorMap", "Ljava/util/HashMap;");
        REQUIRE(cache_field);
        for (const auto& [method, descriptor] : std::vector<std::pair<const char*, const char*>>{
                 {"inflate", "(ILandroid/view/ViewGroup;)Landroid/view/View;"},
                 {"inflate", "(ILandroid/view/ViewGroup;Z)Landroid/view/View;"},
                 {"inflate", "(Lorg/xmlpull/v1/XmlPullParser;Landroid/view/ViewGroup;)Landroid/view/View;"},
                 {"inflate", "(Lorg/xmlpull/v1/XmlPullParser;Landroid/view/ViewGroup;Z)Landroid/view/View;"},
                 {"rInflate", "(Lorg/xmlpull/v1/XmlPullParser;Landroid/view/View;Landroid/util/AttributeSet;Z)V"},
                 {"createViewFromTag",
                  "(Landroid/view/View;Ljava/lang/String;Landroid/util/AttributeSet;)Landroid/view/View;"}}) {
            const auto selected = f.linker.FindVtableIndex(inflater_type, method, descriptor);
            REQUIRE(selected);
            CHECK(f.linker.Method(f.linker.Class(inflater_type).vtable[*selected]).kind == MethodKind::interpreted);
        }
        const auto cache = VmObjectRef{f.linker.Class(inflater_type).static_storage[f.linker.Field(*cache_field).slot]};
        const auto name = f.vm.NewStringUtf8("test.InflatedLeaf");
        const auto cached = f.On(cache, "get", "(Ljava/lang/Object;)Ljava/lang/Object;", {VmValue::Ref(name)}).ref;
        REQUIRE(cached.IsValid());
        const auto count = f.On(cache, "size", "()I").AsInt();
        CHECK(f.Checked(create("test.InflatedLeaf")).ref.IsValid());
        CHECK(f.On(cache, "get", "(Ljava/lang/Object;)Ljava/lang/Object;", {VmValue::Ref(name)}).ref == cached);
        CHECK(f.On(cache, "size", "()I").AsInt() == count);
        CHECK(f.model.GetObjectElement(args, 1) == parser);
        const auto cause = [&](const VmCallOutcome& result, const char* type) {
            REQUIRE(result.exception.IsValid());
            CHECK(f.linker.Class(result.exception_class).descriptor == "Landroid/view/InflateException;");
            const auto nested = f.On(result.exception, "getCause", "()Ljava/lang/Throwable;").ref;
            REQUIRE(nested.IsValid());
            CHECK(f.linker.Class(f.model.ObjectClass(nested)).descriptor == type);
            return nested;
        };
        const auto missing = create("test.DoesNotExist");
        CHECK(f.linker.Class(missing.exception_class).descriptor == "Ljava/lang/ClassNotFoundException;");
        const auto no_ctor = create("test.MeasuredLeaf");
        cause(no_ctor, "Ljava/lang/NoSuchMethodException;");
        CHECK(no_ctor.exception_message == "Binary XML file line #1: Error inflating class test.MeasuredLeaf");
        const auto thrown = create("test.ThrowingInflatedLeaf");
        const auto target = cause(thrown, "Ljava/lang/reflect/InvocationTargetException;");
        const auto original = f.On(target, "getCause", "()Ljava/lang/Throwable;").ref;
        REQUIRE(original.IsValid());
        CHECK(f.linker.Class(f.model.ObjectClass(original)).descriptor == "Ljava/lang/IllegalStateException;");
        CHECK(thrown.exception_message == "Binary XML file line #1: Error inflating class test.ThrowingInflatedLeaf");
        const auto filter = f.New("Ltest/InflaterFilter;");
        f.On(inflater, "setFilter", "(Landroid/view/LayoutInflater$Filter;)V", {VmValue::Ref(filter)});
        const auto rejected = create("test.InflatedLeaf");
        const auto denial = cause(rejected, "Landroid/view/InflateException;");
        CHECK(f.vm.StringUtf8(f.On(denial, "getMessage", "()Ljava/lang/String;").ref) ==
              "Binary XML file line #1: Class not allowed to be inflated test.InflatedLeaf");
        CHECK(JavaField(f, filter, "calls").bits == 1);
        cause(create("test.InflatedLeaf"), "Landroid/view/InflateException;");
        CHECK(JavaField(f, filter, "calls").bits == 1);
        f.On(inflater, "setFilter", "(Landroid/view/LayoutInflater$Filter;)V", {VmValue::Ref(VmObjectRef{})});
        const auto phone_type = f.linker.ResolveDescriptor("Lcom/android/internal/policy/impl/PhoneLayoutInflater;");
        CHECK(f.model.ObjectClass(inflater) == phone_type);
        for (const auto& [method, descriptor] : std::vector<std::pair<const char*, const char*>>{
                 {"cloneInContext", "(Landroid/content/Context;)Landroid/view/LayoutInflater;"},
                 {"onCreateView", "(Ljava/lang/String;Landroid/util/AttributeSet;)Landroid/view/View;"}}) {
            const auto selected = f.linker.FindVtableIndex(phone_type, method, descriptor);
            REQUIRE(selected);
            CHECK(f.linker.Method(f.linker.Class(phone_type).vtable[*selected]).kind == MethodKind::interpreted);
        }
        const auto clone_context = f.New("Landroid/content/Context;");
        f.On(inflater, "setFilter", "(Landroid/view/LayoutInflater$Filter;)V", {VmValue::Ref(filter)});
        const auto factory = f.New("Ltest/InflaterFactory;");
        f.On(inflater, "setFactory2", "(Landroid/view/LayoutInflater$Factory2;)V", {VmValue::Ref(factory)});
        const auto clone = f.On(inflater, "cloneInContext", "(Landroid/content/Context;)Landroid/view/LayoutInflater;",
                                {VmValue::Ref(clone_context)})
                               .ref;
        CHECK(clone != inflater);
        CHECK(f.model.ObjectClass(clone) == phone_type);
        CHECK(f.On(clone, "getContext", "()Landroid/content/Context;").ref == clone_context);
        CHECK(f.On(inflater, "getContext", "()Landroid/content/Context;").ref == owner);
        CHECK(VmObjectRef{JavaField(f, clone, "mFilter", "Landroid/view/LayoutInflater$Filter;").bits} == filter);
        CHECK(JavaField(f, clone, "mFilterMap", "Ljava/util/HashMap;").bits !=
              JavaField(f, inflater, "mFilterMap", "Ljava/util/HashMap;").bits);
        CHECK(f.On(clone, "getFactory2", "()Landroid/view/LayoutInflater$Factory2;").ref == factory);
        f.On(clone, "setFilter", "(Landroid/view/LayoutInflater$Filter;)V", {VmValue::Ref(VmObjectRef{})});
        const auto factory2 = f.New("Ltest/InflaterFactory;");
        f.On(clone, "setFactory", "(Landroid/view/LayoutInflater$Factory;)V", {VmValue::Ref(factory2)});
        InstallInflateLayouts(f, {{"View", InflateSize(40, 20), {}}});
        const auto inflated = f.On(clone, "inflate", "(ILandroid/view/ViewGroup;Z)Landroid/view/View;",
                                   {VmValue::Int(0x7f030001), VmValue::Ref(VmObjectRef{}), VmValue::Int(0)})
                                  .ref;
        CHECK(inflated.IsValid());
        CHECK(JavaField(f, factory, "calls").bits == 1);
        CHECK(JavaField(f, factory2, "calls").bits == 0);
        const auto clone_args = VmObjectRef{JavaField(f, clone, "mConstructorArgs", "[Ljava/lang/Object;").bits};
        CHECK_FALSE(f.model.GetObjectElement(clone_args, 0).IsValid());
        CHECK_FALSE(f.model.GetObjectElement(clone_args, 1).IsValid());
        const auto plain = f.New("Lcom/android/internal/policy/impl/PhoneLayoutInflater;",
                                 "(Landroid/content/Context;)V", {VmValue::Ref(owner)});
        const auto first = f.New("Ltest/InflaterFactory;");
        f.On(plain, "setFactory", "(Landroid/view/LayoutInflater$Factory;)V", {VmValue::Ref(first)});
        const auto merged = f.On(plain, "cloneInContext", "(Landroid/content/Context;)Landroid/view/LayoutInflater;",
                                 {VmValue::Ref(owner)})
                                .ref;
        const auto second = f.New("Ltest/InflaterFactory;");
        f.On(merged, "setFactory", "(Landroid/view/LayoutInflater$Factory;)V", {VmValue::Ref(second)});
        const auto private_factory = f.New("Ltest/InflaterFactory;");
        f.On(merged, "setPrivateFactory", "(Landroid/view/LayoutInflater$Factory2;)V", {VmValue::Ref(private_factory)});
        CHECK(f.On(merged, "inflate", "(ILandroid/view/ViewGroup;)Landroid/view/View;",
                   {VmValue::Int(0x7f030001), VmValue::Ref(VmObjectRef{})})
                  .ref.IsValid());
        CHECK(JavaField(f, first, "calls").bits == 1);
        CHECK(JavaField(f, second, "calls").bits == 1);
        CHECK(JavaField(f, private_factory, "calls").bits == 1);
        CHECK(f.ledger.Unimplemented().empty());
    }
}

TEST_CASE("DVM-292 leaf bitmap and scalar colors execute ROM getters on both backends") {
    using ogplay::test::container_traversal::ContainerVm;
    using namespace ogplay::runtime::dexvm;
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ContainerVm f(backend);
        f.context->ui_density = 2;
        f.context->apk_bytes = ogplay::test::BitmapDecodeApk();
        f.context->archive = ogplay::loader::ParseApkArchive(f.context->apk_bytes);
        ogplay::test::ResourceFixture(*f.context) = Table();
        ogplay::test::ResourceFixture(*f.context).global_string_pool =
            StringPool({"res/drawable/fixture.png", "res/drawable/leaf.jpg"}, true);
        constexpr std::uint32_t image = 0x7f030001, color = 0x7f030002, alias = 0x7f030003;
        ogplay::test::ResourceFixture(*f.context)
            .entries.push_back({.resource_id = image,
                                .type_name = "drawable",
                                .entry_name = "leaf",
                                .string_value = "res/drawable/fixture.png",
                                .value_type = 3,
                                .changing_configurations = 0x100,
                                .density = 160});
        ogplay::test::ResourceFixture(*f.context)
            .entries.push_back({.resource_id = color,
                                .type_name = "color",
                                .entry_name = "color",
                                .value_type = 28,
                                .value_data = 0xff123456,
                                .changing_configurations = 0x200});
        ogplay::test::ResourceFixture(*f.context)
            .entries.push_back({.resource_id = alias,
                                .type_name = "drawable",
                                .entry_name = "alias",
                                .value_type = 1,
                                .value_data = image,
                                .changing_configurations = 0x400});
        ogplay::test::ResourceFixture(*f.context)
            .entries.push_back(Style(0x7f020020, 0, {{a, 1, alias, {}}, {b, 1, color, {}}, {c, 1, 0, {}}}));
        const auto owner = f.New("Landroid/content/Context;");
        ogplay::test::AttachViewTestContext(f.vm, owner, f.context);
        auto res = f.On(owner, "getResources", "()Landroid/content/res/Resources;").ref;
        const auto theme = f.On(res, "newTheme", "()Landroid/content/res/Resources$Theme;").ref;
        const auto roots = f.vm.ProtectReferences(std::array{owner, res, theme});
        f.On(theme, "applyStyle", "(IZ)V", {VmValue::Int(0x7f020020), VmValue::Int(1)});
        const auto attrs = f.model.NewPrimitiveArray(f.linker.ResolveDescriptor("[I"), JniPrimitiveKind::integer, 3);
        for (int i = 0; i < 3; ++i)
            f.model.SetPrimitiveElement(attrs, i, a + static_cast<std::uint32_t>(i));
        const auto attrs_root = f.vm.ProtectReferences(std::array{attrs});
        const auto typed =
            f.On(theme, "obtainStyledAttributes", "([I)Landroid/content/res/TypedArray;", {VmValue::Ref(attrs)}).ref;
        const auto typed_root = f.vm.ProtectReferences(std::array{typed});
        const auto ref_field = [&](VmObjectRef obj, const char* name, const char* type) {
            const auto id = f.linker.FindFieldRecursive(f.model.ObjectClass(obj), name, type);
            REQUIRE(id);
            return VmObjectRef{f.model.InstanceSlots(obj)[f.linker.Field(*id).slot].bits};
        };
        const auto data = ref_field(typed, "mData", "[I");
        const std::array<std::uint32_t, 18> expected{3,     0,     2, image, 0x500, 160,         28, 0xff123456, 2,
                                                     color, 0x200, 0, 0,     0,     0xffffffffU, 0,  0,          0};
        for (int i = 0; i < 18; ++i)
            CHECK(static_cast<std::uint32_t>(f.model.GetPrimitiveElement(data, i)) ==
                  expected[static_cast<std::size_t>(i)]);
        VmObjectRef drawable;
        std::weak_ptr<ogplay::runtime::raster::Bitmap> weak_pixels;
        {
            drawable = f.On(typed, "getDrawable", "(I)Landroid/graphics/drawable/Drawable;", {VmValue::Int(0)}).ref;
            const auto solid =
                f.On(typed, "getDrawable", "(I)Landroid/graphics/drawable/Drawable;", {VmValue::Int(1)}).ref;
            const auto list =
                f.On(typed, "getColorStateList", "(I)Landroid/content/res/ColorStateList;", {VmValue::Int(1)}).ref;
            const auto values_root = f.vm.ProtectReferences(std::array{drawable, solid, list});
            CHECK(f.linker.Class(f.model.ObjectClass(drawable)).descriptor ==
                  "Landroid/graphics/drawable/BitmapDrawable;");
            CHECK(f.linker.Class(f.model.ObjectClass(solid)).descriptor == "Landroid/graphics/drawable/ColorDrawable;");
            CHECK(f.On(drawable, "getIntrinsicWidth", "()I").AsInt() == 4);
            CHECK(f.On(drawable, "getIntrinsicHeight", "()I").AsInt() == 2);
            CHECK(f.On(solid, "getIntrinsicWidth", "()I").AsInt() == -1);
            CHECK(f.On(solid, "getIntrinsicHeight", "()I").AsInt() == -1);
            CHECK(static_cast<std::uint32_t>(f.On(solid, "getColor", "()I").AsInt()) == 0xff123456);
            CHECK(static_cast<std::uint32_t>(f.On(list, "getDefaultColor", "()I").AsInt()) == 0xff123456);
            CHECK_FALSE(f.On(list, "isStateful", "()Z").AsInt());
            CHECK(f.Static("Landroid/content/res/ColorStateList;", "valueOf", "(I)Landroid/content/res/ColorStateList;",
                           {VmValue::Int(static_cast<std::int32_t>(0xff123456))})
                      .ref == list);
            CHECK_FALSE(
                f.On(typed, "getDrawable", "(I)Landroid/graphics/drawable/Drawable;", {VmValue::Int(2)}).ref.IsValid());
            CHECK_FALSE(f.On(typed, "getColorStateList", "(I)Landroid/content/res/ColorStateList;", {VmValue::Int(2)})
                            .ref.IsValid());
            const auto bitmap = f.On(drawable, "getBitmap", "()Landroid/graphics/Bitmap;").ref;
            CHECK_FALSE(f.On(bitmap, "isMutable", "()Z").AsInt());
            CHECK(f.On(bitmap, "getDensity", "()I").AsInt() == 320);
            const auto& pixels = f.context->bitmaps.at(bitmap.Value()).pixels;
            REQUIRE(pixels);
            CHECK(pixels->Snapshot().size() == 8);
            CHECK(pixels->Get(0) == 0xffff0000);
            CHECK(pixels->Get(7) == 0xff0000ff);
            weak_pixels = pixels;
            static_cast<void>(f.vm.CollectGarbage("leaf-rooted"));
            CHECK_FALSE(weak_pixels.expired());
            const auto direct =
                f.On(res, "getDrawable", "(I)Landroid/graphics/drawable/Drawable;", {VmValue::Int(alias)}).ref;
            const auto direct_list =
                f.On(res, "getColorStateList", "(I)Landroid/content/res/ColorStateList;", {VmValue::Int(color)}).ref;
            const auto direct_roots = f.vm.ProtectReferences(std::array{direct, direct_list});
            CHECK(direct != drawable);
            CHECK(f.On(direct, "getIntrinsicWidth", "()I").AsInt() == 4);
            CHECK(direct_list == list);
        }
        static_cast<void>(f.vm.CollectGarbage("leaf-unrooted"));
        CHECK_FALSE(weak_pixels.expired());  // ROM Resources caches the drawable ConstantState.
        // Configuration and density selection: requested 320 chooses 240 over 160;
        // a nonmatching orientation and a newer API cannot steal the resource.
        auto variant = ogplay::test::ResourceFixture(*f.context).entries[3];
        variant.density = 240;
        variant.changing_configurations = 0x800;
        ogplay::test::ResourceFixture(*f.context).entries.push_back(variant);
        variant.density = 320;
        variant.orientation = 1;
        ogplay::test::ResourceFixture(*f.context).entries.push_back(variant);
        variant.orientation = 0;
        variant.sdk_version = 20;
        ogplay::test::ResourceFixture(*f.context).entries.push_back(variant);
        const auto chosen =
            f.On(res, "getDrawable", "(I)Landroid/graphics/drawable/Drawable;", {VmValue::Int(image)}).ref;
        const auto chosen_root = f.vm.ProtectReferences(std::array{chosen});
        CHECK(f.On(chosen, "getIntrinsicWidth", "()I").AsInt() == 3);
        for (auto& entry : ogplay::test::ResourceFixture(*f.context).entries)
            if (entry.resource_id == image) entry.density = 65535;
        // Reinstall the fixture into a fresh manager; addAssetPath overlays retain
        // the older APK's density variants by design.
        f.context->singletons.erase("resources");
        ogplay::test::MountResourceFixture(*f.context, f.vm);
        IntrinsicContext resource_call{f.vm, VmObjectRef{}, {}};
        const auto nodpiResources =
            ogplay::runtime::android_intrinsics::ResourceObject(resource_call, f.context, false);
        const auto nodpiResourceRoot = f.vm.ProtectReferences(std::array{nodpiResources});
        res = nodpiResources;
        const auto nodpi =
            f.On(nodpiResources, "getDrawable", "(I)Landroid/graphics/drawable/Drawable;", {VmValue::Int(image)}).ref;
        const auto nodpi_root = f.vm.ProtectReferences(std::array{nodpi});
        CHECK(f.On(nodpi, "getIntrinsicWidth", "()I").AsInt() == 2);
        // JPEG uses the same typed file path and immutable backing contract.
        const std::array<unsigned char, 288> jpeg_bytes{
            0xff, 0xd8, 0xff, 0xe0, 0x0,  0x10, 0x4a, 0x46, 0x49, 0x46, 0x0,  0x1,  0x1,  0x0,  0x0,  0x1,  0x0,  0x1,
            0x0,  0x0,  0xff, 0xdb, 0x0,  0x43, 0x0,  0x3,  0x2,  0x2,  0x3,  0x2,  0x2,  0x3,  0x3,  0x3,  0x3,  0x4,
            0x3,  0x3,  0x4,  0x5,  0x8,  0x5,  0x5,  0x4,  0x4,  0x5,  0xa,  0x7,  0x7,  0x6,  0x8,  0xc,  0xa,  0xc,
            0xc,  0xb,  0xa,  0xb,  0xb,  0xd,  0xe,  0x12, 0x10, 0xd,  0xe,  0x11, 0xe,  0xb,  0xb,  0x10, 0x16, 0x10,
            0x11, 0x13, 0x14, 0x15, 0x15, 0x15, 0xc,  0xf,  0x17, 0x18, 0x16, 0x14, 0x18, 0x12, 0x14, 0x15, 0x14, 0xff,
            0xdb, 0x0,  0x43, 0x1,  0x3,  0x4,  0x4,  0x5,  0x4,  0x5,  0x9,  0x5,  0x5,  0x9,  0x14, 0xd,  0xb,  0xd,
            0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14,
            0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14,
            0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0xff, 0xc0, 0x0,  0x11,
            0x8,  0x0,  0x1,  0x0,  0x2,  0x3,  0x1,  0x22, 0x0,  0x2,  0x11, 0x1,  0x3,  0x11, 0x1,  0xff, 0xc4, 0x0,
            0x15, 0x0,  0x1,  0x1,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,
            0x0,  0x8,  0xff, 0xc4, 0x0,  0x14, 0x10, 0x1,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,
            0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0xff, 0xc4, 0x0,  0x15, 0x1,  0x1,  0x1,  0x0,  0x0,  0x0,  0x0,  0x0,
            0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x7,  0x9,  0xff, 0xc4, 0x0,  0x14, 0x11, 0x1,  0x0,
            0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0xff, 0xda, 0x0,
            0xc,  0x3,  0x1,  0x0,  0x2,  0x11, 0x3,  0x11, 0x0,  0x3f, 0x0,  0x9d, 0x0,  0x6,  0x2a, 0x9b, 0xff, 0xd9};
        std::vector<std::byte> jpeg;
        for (auto byte : jpeg_bytes)
            jpeg.push_back(static_cast<std::byte>(byte));
        f.context->apk_bytes = ogplay::test::file_vfs::MakeStoredZip("res/drawable/leaf.jpg", jpeg);
        f.context->archive = ogplay::loader::ParseApkArchive(f.context->apk_bytes);
        ogplay::test::ResourceFixture(*f.context)
            .entries.push_back({.resource_id = 0x7f030010,
                                .type_name = "drawable",
                                .entry_name = "jpeg",
                                .string_value = "res/drawable/leaf.jpg",
                                .value_type = 3,
                                .value_data = 1,
                                .density = 160});
        const auto jpeg_drawable =
            f.On(res, "getDrawable", "(I)Landroid/graphics/drawable/Drawable;", {VmValue::Int(0x7f030010)}).ref;
        const auto jpeg_root = f.vm.ProtectReferences(std::array{jpeg_drawable});
        CHECK(f.On(jpeg_drawable, "getIntrinsicWidth", "()I").AsInt() == 4);
        const auto jpegBitmap = f.On(jpeg_drawable, "getBitmap", "()Landroid/graphics/Bitmap;").ref;
        CHECK(f.context->bitmaps.at(jpegBitmap.Value()).pixels->Snapshot().size() == 8);
        for (const auto& hit : f.ledger.Unimplemented())
            CHECK_MESSAGE(hit.count == 0, hit.id);
        const auto drawable_entry = f.linker.FindVtableIndex(f.model.ObjectClass(res), "getDrawable",
                                                             "(I)Landroid/graphics/drawable/Drawable;");
        REQUIRE(drawable_entry);
        CHECK(f.linker.Method(f.linker.Class(f.model.ObjectClass(res)).vtable[*drawable_entry]).kind ==
              MethodKind::interpreted);
        for (const auto& [object, name, descriptor] : std::vector<std::tuple<VmObjectRef, const char*, const char*>>{
                 {typed, "getDrawable", "(I)Landroid/graphics/drawable/Drawable;"},
                 {typed, "getColorStateList", "(I)Landroid/content/res/ColorStateList;"},
                 {res, "getColorStateList", "(I)Landroid/content/res/ColorStateList;"},
                 {chosen, "getIntrinsicWidth", "()I"}}) {
            const auto method = f.linker.FindVtableIndex(f.model.ObjectClass(object), name, descriptor);
            REQUIRE(method);
            CHECK(f.linker.Method(f.linker.Class(f.model.ObjectClass(object)).vtable[*method]).kind ==
                  MethodKind::interpreted);
        }
    }
}

// namespace

TEST_CASE("DVM-296 ImageButton XML and code honor real imageButtonStyle and explicit precedence") {
    for (auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ContainerVm f(backend);
        const auto owner = f.New("Landroid/app/Activity;");
        ogplay::test::AttachViewTestContext(f.vm, owner, f.context);
        const auto owner_root = f.vm.ProtectReferences(std::array{owner});
        ogplay::test::ResourceFixture(*f.context)
            .entries.push_back(Style(
                0x7f020071, 0, {{0x01010119, 28, 0xff884422, {}}, {0x0101011d, 16, 5, {}}, {0x0101042f, 16, 64, {}}}));
        ogplay::test::ResourceFixture(*f.context)
            .entries.push_back(Style(0x7f020072, 0, {{0x01010072, 1, 0x7f020071, {}}}));
        const auto theme = f.On(owner, "getTheme", "()Landroid/content/res/Resources$Theme;").ref;
        f.On(theme, "applyStyle", "(IZ)V", {VmValue::Int(0x7f020072), VmValue::Int(1)});
        // android:gravity is not read by ROM View/ImageView/ImageButton and is ignored, not rejected.
        const auto parser = OpenAttributeXml(
            f, owner,
            AttributeXml(
                {{{0x010100af, 17, 5}, {0x01010119, 28, 0xff223344}, {0x0101011d, 16, 1}, {0x0101042f, 16, 96}}}));
        const auto parser_root = f.vm.ProtectReferences(std::array{parser});
        const auto xml =
            f.New("Landroid/widget/ImageButton;", "(Landroid/content/Context;Landroid/util/AttributeSet;)V",
                  {VmValue::Ref(owner), VmValue::Ref(parser)});
        const auto code = f.New("Landroid/widget/ImageButton;", "(Landroid/content/Context;)V", {VmValue::Ref(owner)});
        const auto roots = f.vm.ProtectReferences(std::array{xml, code});
        CHECK(f.On(code, "getImageAlpha", "()I").AsInt() == 64);
        const auto original = f.On(code, "getDrawable", "()Landroid/graphics/drawable/Drawable;").ref;
        CHECK((f.On(original, "getColor", "()I").cat1 & 0x00ffffff) == 0x00884422);
        const auto desired = f.On(xml, "getDrawable", "()Landroid/graphics/drawable/Drawable;").ref;
        CHECK((f.On(desired, "getColor", "()I").cat1 & 0x00ffffff) == 0x00223344);
        const auto scale = f.On(xml, "getScaleType", "()Landroid/widget/ImageView$ScaleType;").ref;
        CHECK(f.On(scale, "ordinal", "()I").AsInt() == 1);
        f.On(code, "setImageDrawable", "(Landroid/graphics/drawable/Drawable;)V", {VmValue::Ref(desired)});
        f.On(code, "setScaleType", "(Landroid/widget/ImageView$ScaleType;)V", {VmValue::Ref(scale)});
        f.On(code, "setImageAlpha", "(I)V", {VmValue::Int(96)});
        for (const auto view : {xml, code}) {
            CHECK(f.On(view, "isFocusable", "()Z").AsInt() == 1);
            CHECK(f.On(view, "getDrawable", "()Landroid/graphics/drawable/Drawable;").ref == desired);
            CHECK(f.On(view, "getScaleType", "()Landroid/widget/ImageView$ScaleType;").ref == scale);
            CHECK(f.On(view, "getImageAlpha", "()I").AsInt() == 96);
            f.On(view, "measure", "(II)V", {VmValue::Int(0), VmValue::Int(0)});
            CHECK(f.On(view, "getMeasuredWidth", "()I").AsInt() == 1);
            CHECK(f.On(view, "getMeasuredHeight", "()I").AsInt() == 1);
        }
        CHECK(f.ledger.Unimplemented().empty());
    }
}

TEST_CASE("DVM-325 scrollbar references project without loading and reject unregistered IDs") {
    for (auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ContainerVm f(backend);
        const auto plain_owner = f.New("Landroid/app/Activity;");
        ogplay::test::AttachViewTestContext(f.vm, plain_owner, f.context);
        const auto plain = f.New("Landroid/view/View;", "(Landroid/content/Context;Landroid/util/AttributeSet;)V",
                                 {VmValue::Ref(plain_owner), VmValue::Ref(VmObjectRef{})});
        const auto plain_roots = f.vm.ProtectReferences(std::array{plain_owner, plain});
        CHECK((JavaField(f, plain, "mViewFlags").bits & 0x300U) == 0);
        CHECK(JavaField(f, plain, "mScrollCache", "Landroid/view/View$ScrollabilityCache;").bits == 0);
        const auto requested =
            f.model.NewPrimitiveArray(f.linker.ResolveDescriptor("[I"), JniPrimitiveKind::integer, 4);
        const auto requested_root = f.vm.ProtectReferences(std::array{requested});
        for (std::int32_t i = 0; i < 4; ++i)
            f.model.SetPrimitiveElement(requested, i, 0x01010064U + static_cast<std::uint32_t>(i));
        for (const auto [theme_id, thumb_h, thumb_v] :
             {std::array{0x01030005, 0x0108052a, 0x0108052b}, std::array{0x0103006b, 0x01080528, 0x01080528},
              std::array{0x0103006e, 0x01080529, 0x01080529}}) {
            f.On(plain_owner, "setTheme", "(I)V", {VmValue::Int(theme_id)});
            const auto values = f.On(plain_owner, "obtainStyledAttributes", "([I)Landroid/content/res/TypedArray;",
                                     {VmValue::Ref(requested)})
                                    .ref;
            const auto values_root = f.vm.ProtectReferences(std::array{values});
            for (std::int32_t i = 0; i < 4; ++i) {
                const auto expected = i == 0 ? thumb_h : i == 1 ? thumb_v : 0;
                CHECK(f.On(values, "getResourceId", "(II)I", {VmValue::Int(i), VmValue::Int(0)}).AsInt() == expected);
            }
            f.On(values, "recycle", "()V");
        }
        CHECK(f.ledger.Unimplemented().empty());
        ogplay::test::ResourceFixture(*f.context) = Table(
            {Style(0x7f020004, 0x01030005, {{0x01010064, 1, 0, {}}, {0x01010065, 1, 0, {}}, {0x010100d4, 1, 0, {}}}),
             Style(0x7f020005, 0, {{0x010100d4, 1, 0x01080004, {}}})});
        const auto owner = f.New("Landroid/app/Activity;");
        ogplay::test::AttachViewTestContext(f.vm, owner, f.context);
        // Only IDs proven by the fixed ROM are enabled. This is R.drawable.btn_default,
        // already demanded by the View background slot of the default Button style.
        f.On(owner, "setTheme", "(I)V", {VmValue::Int(0x7f020004)});
        const auto view = f.New("Landroid/view/View;", "(Landroid/content/Context;Landroid/util/AttributeSet;)V",
                                {VmValue::Ref(owner), VmValue::Ref(VmObjectRef{})});
        REQUIRE(view.IsValid());
        const auto theme = f.On(owner, "getTheme", "()Landroid/content/res/Resources$Theme;").ref;
        f.On(theme, "applyStyle", "(IZ)V", {VmValue::Int(0x7f020005), VmValue::Int(1)});
        const auto attrs = f.model.NewPrimitiveArray(f.linker.ResolveDescriptor("[I"), JniPrimitiveKind::integer, 1);
        f.model.SetPrimitiveElement(attrs, 0, 0x010100d4);
        const auto roots = f.vm.ProtectReferences(std::array{owner, view, theme, attrs});
        CHECK_FALSE(
            f.OnOutcome(owner, "obtainStyledAttributes", "([I)Landroid/content/res/TypedArray;", {VmValue::Ref(attrs)})
                .exception.IsValid());
    }
}

TEST_CASE("DVM-316 RelativeLayout inflation reports reflection target and constructor first stack") {
    for (auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ContainerVm f(backend);
        // The observed root now passes projection. This diagnostic deliberately
        // supplies an unknown framework scrollbar ID through the theme instead.
        ogplay::test::ResourceFixture(*f.context) =
            Table({Style(0x7f020004, 0x01030007, {{0x010100d4, 1, 0x0108ffff, {}}})});
        const auto owner = f.New("Landroid/app/Activity;");
        f.context->foreground_activity = owner;
        ogplay::test::AttachViewTestContext(f.vm, owner, f.context);
        f.On(owner, "setTheme", "(I)V", {VmValue::Int(0x7f020004)});
        // Root attributes from the observed movie overlay: horizontal, match_parent both axes.
        auto attrs = InflateSize(0xffffffffU, 0xffffffffU);
        attrs.push_back({0x010100c4, 16, 0});
        InstallInflateLayouts(f, {{"RelativeLayout", attrs, {}}});
        const auto inflater = f.On(owner, "getLayoutInflater", "()Landroid/view/LayoutInflater;").ref;
        const auto outcome = f.OnOutcome(inflater, "inflate", "(ILandroid/view/ViewGroup;Z)Landroid/view/View;",
                                         {VmValue::Int(0x7f030001), VmValue::Ref(VmObjectRef{}), VmValue::Int(0)});
        REQUIRE(outcome.exception.IsValid());
        CHECK(f.linker.Class(outcome.exception_class).descriptor == "Landroid/view/InflateException;");
        const auto wrapper = f.On(outcome.exception, "getCause", "()Ljava/lang/Throwable;").ref;
        REQUIRE(wrapper.IsValid());
        CHECK(f.linker.Class(f.model.ObjectClass(wrapper)).descriptor ==
              "Ljava/lang/reflect/InvocationTargetException;");
        const auto target = f.On(wrapper, "getTargetException", "()Ljava/lang/Throwable;").ref;
        REQUIRE(target.IsValid());
        const auto message = f.vm.StringUtf8(f.On(target, "getMessage", "()Ljava/lang/String;").ref);
        INFO(message);
        CHECK(message.find("108ffff") != std::string::npos);
        const auto task_type = f.linker.ResolveDescriptor("Ltest/OwnerMutation;");
        const auto run = f.linker.FindVtableIndex(task_type, "run", "()V");
        REQUIRE(run);
        auto& method = f.linker.MutableMethod(f.linker.Class(task_type).vtable[*run]);
        method.code.reset();
        method.fast_code.reset();
        method.kind = MethodKind::intrinsic;
        method.implementation = [fault = outcome.exception](IntrinsicContext&) -> VmValue {
            throw VmJavaThrow{"Landroid/view/InflateException;", "", fault};
        };
        const auto handler = f.New("Landroid/os/Handler;");
        const auto task = f.New("Ltest/OwnerMutation;");
        const auto roots = f.vm.ProtectReferences(std::array{outcome.exception, handler, task});
        f.On(handler, "post", "(Ljava/lang/Runnable;)Z", {VmValue::Ref(task)});
        const auto failure = ogplay::test::android_value::DispatchFixtureMessages(f.vm, *f.context);
        REQUIRE(failure);
        INFO(*failure);
        CHECK(failure->find("Caused by: Landroid/content/res/Resources$NotFoundException;: " + message) !=
              std::string::npos);
        CHECK(failure->find("Caused by:   at Landroid/content/res/Resources;.loadDrawable") != std::string::npos);
    }
}

TEST_CASE("API19 app and system resources load framework scalar colors and XML selectors") {
    using ogplay::test::container_traversal::ContainerVm;
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ContainerVm f(backend);
        const auto owner = f.New("Landroid/content/Context;");
        ogplay::test::AttachViewTestContext(f.vm, owner, f.context);
        const auto resources = f.On(owner, "getResources", "()Landroid/content/res/Resources;").ref;
        const auto roots = f.vm.ProtectReferences(std::array{owner, resources});
        const auto drawable =
            f.On(resources, "getDrawable", "(I)Landroid/graphics/drawable/Drawable;", {VmValue::Int(0x0106000c)}).ref;
        const auto drawable_root = f.vm.ProtectReferences(std::array{drawable});
        CHECK(f.linker.Class(f.model.ObjectClass(drawable)).descriptor == "Landroid/graphics/drawable/ColorDrawable;");
        CHECK(static_cast<std::uint32_t>(f.On(drawable, "getColor", "()I").AsInt()) == 0xff000000U);
        CHECK(f.On(drawable, "getOpacity", "()I").AsInt() == -1);
        f.On(drawable, "setColor", "(I)V", {VmValue::Int(0)});
        CHECK(f.On(drawable, "getOpacity", "()I").AsInt() == -2);
        f.On(drawable, "setColor", "(I)V", {VmValue::Int(0x12345678)});
        CHECK(f.On(drawable, "getOpacity", "()I").AsInt() == -3);
        const auto skin =
            f.On(resources, "getDrawable", "(I)Landroid/graphics/drawable/Drawable;", {VmValue::Int(0x01080016)}).ref;
        const auto skin_root = f.vm.ProtectReferences(std::array{skin});
        CHECK(f.linker.Class(f.model.ObjectClass(skin)).descriptor == "Landroid/graphics/drawable/StateListDrawable;");
        CHECK(
            f.On(resources, "getColorStateList", "(I)Landroid/content/res/ColorStateList;", {VmValue::Int(0x01060001)})
                .ref.IsValid());
        // Unknown framework IDs follow the ROM NotFoundException path.
        for (const auto id : {0x0106ffff, 0x0108ffff}) {
            const auto failed =
                f.OnOutcome(resources, "getDrawable", "(I)Landroid/graphics/drawable/Drawable;", {VmValue::Int(id)});
            REQUIRE(failed.exception.IsValid());
            CHECK(f.linker.Class(failed.exception_class).descriptor ==
                  "Landroid/content/res/Resources$NotFoundException;");
        }
        const auto system =
            f.Static("Landroid/content/res/Resources;", "getSystem", "()Landroid/content/res/Resources;").ref;
        const auto system_root = f.vm.ProtectReferences(std::array{system});
        const auto systemColor =
            f.On(system, "getDrawable", "(I)Landroid/graphics/drawable/Drawable;", {VmValue::Int(0x0106000c)}).ref;
        CHECK(static_cast<std::uint32_t>(f.On(systemColor, "getColor", "()I").AsInt()) == 0xff000000U);
    }
}

TEST_CASE("DVM-334 Resources getIdentifier parses API19 names and routes packages") {
    using ogplay::test::container_traversal::ContainerVm;
    using namespace ogplay::runtime::dexvm;
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ContainerVm f(backend);
        ogplay::test::ResourceFixture(*f.context) = Table();
        ogplay::test::ResourceFixture(*f.context).package_name = "resource.fixture";
        // Manifest identity must not substitute for the resource table's package.
        f.context->package_name = "manifest.fixture";
        constexpr auto logo = 0x7f030001;
        ogplay::test::ResourceFixture(*f.context)
            .entries.push_back({.resource_id = logo, .type_name = "drawable", .entry_name = "logo"});
        const auto owner = f.New("Landroid/content/Context;");
        ogplay::test::AttachViewTestContext(f.vm, owner, f.context);
        const auto resources = f.On(owner, "getResources", "()Landroid/content/res/Resources;").ref;
        const auto roots = f.vm.ProtectReferences(std::array{owner, resources});
        const auto lookup = [&](const char* name, const char* type, const char* package) {
            const auto text = [&](const char* value) {
                return VmValue::Ref(value ? f.vm.NewStringUtf8(value) : VmObjectRef{});
            };
            return f.OnOutcome(resources, "getIdentifier", "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)I",
                               {text(name), text(type), text(package)});
        };
        const auto id = [&](const char* name, const char* type, const char* package) {
            const auto outcome = lookup(name, type, package);
            REQUIRE_MESSAGE(!outcome.exception.IsValid(), outcome.exception_message);
            return outcome.value.AsInt();
        };
        CHECK(id("drawable/logo", nullptr, "resource.fixture") == logo);
        CHECK(id("logo", "drawable", "resource.fixture") == logo);
        CHECK(id("@drawable/logo", nullptr, "resource.fixture") == logo);
        CHECK(id("resource.fixture:drawable/logo", nullptr, nullptr) == logo);
        CHECK(id("@resource.fixture:drawable/logo", "wrong", "wrong") == logo);
        CHECK(id("@*resource.fixture:drawable/logo", nullptr, nullptr) == logo);
        CHECK(id("@*drawable/logo", nullptr, "resource.fixture") == logo);
        CHECK(id("resource.fixture:logo", "drawable", nullptr) == logo);
        CHECK(id("@drawable/logo", nullptr, nullptr) == 0);  // @ does not supply a default package.
        for (const auto& [text, expected] :
             std::array<std::pair<const char*, std::int32_t>, 7>{{{"2130903041", logo},
                                                                  {"+42", 42},
                                                                  {"-42", -42},
                                                                  {"2147483647", 2147483647},
                                                                  {"-2147483648", (-2147483647 - 1)},
                                                                  {"0", 0},
                                                                  {"00042", 42}}})
            CHECK(id(text, nullptr, nullptr) == expected);
        for (const auto* text : {"2147483648", "-2147483649", "+", "-", " 42", "42 ", "0x2a", "42x"})
            CHECK(id(text, nullptr, nullptr) == 0);
        for (const auto* text : {"", "@", "@*", "resource.fixture:drawable/", "resource.fixture:/logo"})
            CHECK(id(text, nullptr, nullptr) == 0);
        CHECK(id("logo", nullptr, "resource.fixture") == 0);
        CHECK(id("drawable/logo", nullptr, nullptr) == 0);
        CHECK(id("drawable/missing", nullptr, "resource.fixture") == 0);
        CHECK(id("Drawable/logo", nullptr, "resource.fixture") == 0);
        CHECK(id("drawable/Logo", nullptr, "resource.fixture") == 0);
        CHECK(id("other:drawable/logo", nullptr, "resource.fixture") == 0);
        CHECK(id("logo", "drawable", "manifest.fixture") == 0);
        CHECK(id("other:resource.fixture:drawable/logo", nullptr, nullptr) == 0);  // Last colon wins.
        CHECK(id("resource.fixture:wrong/drawable/logo", nullptr, nullptr) == 0);  // Last slash wins.
        const auto null_name = lookup(nullptr, nullptr, nullptr);
        REQUIRE(null_name.exception.IsValid());
        CHECK(f.linker.Class(null_name.exception_class).descriptor == "Ljava/lang/NullPointerException;");
        CHECK(null_name.exception_message == "name is null");
        CHECK(id("Theme", "style", "android") == 0x01030005);
        CHECK(id("@android:style/Theme", nullptr, nullptr) == 0x01030005);
        CHECK(id("style/Theme", nullptr, "android") == 0x01030005);
        CHECK(f.ledger.Unimplemented().empty());
        CHECK(id("android:drawable/logo", "drawable", "resource.fixture") == 0);
        CHECK(id("^type", "drawable", "resource.fixture") == 0x01000000);
        CHECK(id("logo", "drawable", "android") == 0);
        CHECK(f.ledger.Unimplemented().empty());
    }
}

TEST_CASE("P2 Context obtainStyledAttributes resolves an application style through android resources") {
    using ogplay::test::container_traversal::ContainerVm;
    using namespace ogplay::runtime::dexvm;
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ContainerVm f(backend);
        constexpr int appStyle = 0x7f020000;
        constexpr int appAttribute = 0x7f010000;
        constexpr int androidString = 0x0104000a;
        ogplay::test::ResourceFixture(*f.context).entries = {
            {.resource_id = appAttribute, .type_name = "attr", .entry_name = "message", .value_type = 16},
            Style(appStyle, 0, {{appAttribute, 1, androidString, {}}}),
        };
        const auto context = f.New("Landroid/content/Context;");
        const auto root = f.vm.ProtectReferences(std::array{context});
        const auto theme = f.On(context, "getTheme", "()Landroid/content/res/Resources$Theme;").ref;
        f.On(theme, "applyStyle", "(IZ)V", {VmValue::Int(appStyle), VmValue::Int(1)});
        const auto attrs = f.model.NewPrimitiveArray(f.linker.ResolveDescriptor("[I"), JniPrimitiveKind::integer, 1);
        f.model.SetPrimitiveElement(attrs, 0, appAttribute);
        const auto result =
            f.On(context, "obtainStyledAttributes", "([I)Landroid/content/res/TypedArray;", {VmValue::Ref(attrs)}).ref;
        const auto values = f.vm.ProtectReferences(std::array{result});
        CHECK(f.On(result, "getResourceId", "(II)I", {VmValue::Int(0), VmValue::Int(0)}).AsInt() == androidString);
        CHECK(f.vm.StringUtf8(f.On(result, "getString", "(I)Ljava/lang/String;", {VmValue::Int(0)}).ref) == "OK");
    }
}
