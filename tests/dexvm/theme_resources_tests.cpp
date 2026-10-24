#include "container_traversal_fixture.h"
#include "ogplay/runtime/integration/theme_resources.h"
#include "ogplay/loader/apk.h"

#include <bit>
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

std::string Projection() {
    std::ifstream input(OGPLAY_DEXVM_FIXTURE_DIR "/view-measure.jar", std::ios::binary);
    const std::vector<char> raw{std::istreambuf_iterator<char>(input), {}};
    std::vector<std::byte> bytes(raw.size());
    std::memcpy(bytes.data(), raw.data(), raw.size());
    const auto payload = ogplay::loader::ReadApkEntry(bytes, ogplay::loader::ParseApkArchive(bytes),
                                                      "META-INF/ogplay/theme-projection.json");
    return {reinterpret_cast<const char*>(payload.data()), payload.size()};
}

constexpr std::uint32_t a = 0x7f010001, b = 0x7f010002, c = 0x7f010003, def = 0x7f010004;
constexpr std::uint32_t theme_id = 0x7f020001, default_id = 0x7f020002, xml_style_id = 0x7f020003;

ogplay::loader::ArscEntry Style(std::uint32_t id, std::uint32_t parent, std::vector<ogplay::loader::ArscBagValue> bag) {
    ogplay::loader::ArscEntry e;
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

ogplay::loader::ArscTable Table() {
    ogplay::loader::ArscTable t;
    t.package_id = 0x7f;
    t.entries.push_back(Style(theme_id, 0x01030005, {{a, 16, 10, {}}, {b, 16, 11, {}}, {def, 1, default_id, {}}}));
    t.entries.push_back(Style(default_id, 0, {{a, 16, 20, {}}, {b, 16, 21, {}}}));
    t.entries.push_back(Style(xml_style_id, default_id, {{a, 16, 30, {}}, {c, 2, b, {}}}));
    // Serialize the APK style bags and pass through the production ARSC reader.
    auto global = StringPool({"from APK"}, true);
    auto types = StringPool({"attr", "style"}, true);
    auto keys = StringPool({"theme", "default", "xml"}, true);
    std::vector<std::byte> chunk;
    Append16(chunk, 0x0201);
    Append16(chunk, 56);
    Append32(chunk, 0);
    chunk.push_back(std::byte{2});
    chunk.push_back(std::byte{0});
    Append16(chunk, 0);
    Append32(chunk, 4);
    Append32(chunk, 72);
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
    return ogplay::loader::ParseArsc(std::span{reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size()});
}

ogplay::loader::CompiledXmlAttribute Attr(std::uint32_t id, std::uint8_t type, std::uint32_t data) {
    ogplay::loader::CompiledXmlAttribute value;
    value.resource_id = id;
    value.value = {8, 0, type, data};
    return value;
}
}  // namespace

TEST_CASE("DVM-288 generated theme registration and three-state values are explicit") {
    ThemeResources r;
    r.Load(Projection());
    CHECK_NOTHROW(r.RequireRegistered("Theme"));
    CHECK_NOTHROW(r.RequireRegistered("Theme.Holo"));
    CHECK_NOTHROW(r.RequireRegistered("Theme.DeviceDefault"));
    CHECK_THROWS_WITH(r.RequireRegistered("Theme.Dialog.NoFrame"),
                      "unregistered theme Theme.Dialog.NoFrame: 无 ROM ID 绑定");
    CHECK_THROWS_WITH(r.RequireRegistered("Theme.DoesNotExist"),
                      "unregistered theme Theme.DoesNotExist: unknown theme");
    const auto token = r.New();
    const auto table = Table();
    r.Apply(token, 0x01030005, true, table);
    CHECK_FALSE(r.Attribute(token, 0x010100d5, true, table));  // padding: certified absent
    CHECK(r.Attribute(token, 0x01010063, true, table)->Slots() == std::array<std::uint32_t, 6>{5, 0x0a01, 1, 0, 0, 0});
    CHECK_THROWS(
        static_cast<void>(r.Attribute(token, 0x01010064, true, table)));  // original scrollbar drawable, not absent
    CHECK(r.Attribute(token, 0x01010066, true, table)->type == 1);        // @null remains reference in theme
    CHECK_THROWS(static_cast<void>(r.Attribute(token, 0x0101ffff, true, table)));
    CHECK_THROWS(r.Apply(token, 0x0103ffff, true, table));
    ThemeResources other;
    other.Load(Projection());
    CHECK_THROWS(other.Copy(other.New(), token));
    r.Delete(token);
    CHECK_THROWS(r.Delete(token));
    CHECK_THROWS(static_cast<void>(r.Attribute(token, a, true, table)));
    r.Shutdown();
    CHECK_THROWS(static_cast<void>(r.New()));
}

TEST_CASE("DVM-288 AOSP priority parent references force copy and null masking") {
    ThemeResources r;
    r.Load(Projection());
    auto table = Table();
    const auto token = r.New();
    r.Apply(token, theme_id, true, table);
    const std::array attrs{a, b, c};
    ogplay::loader::CompiledXmlNode xml;
    xml.style_index = 1;
    xml.attributes = {Attr(0, 1, xml_style_id), Attr(a, 16, 40)};
    auto values = r.Styled(token, def, 0x7f02ffff, &xml, attrs, table);
    CHECK(values[0].Slots() == std::array<std::uint32_t, 6>{16, 40, 0xffffffffU, 0, 0, 0});
    CHECK(values[1].data == 21);
    CHECK(values[2].data == 11);  // ?attr resolves theme, not XML/default bag
    xml.attributes.pop_back();
    CHECK(r.Styled(token, 0, default_id, &xml, attrs, table)[0].data == 30);
    xml.attributes[0] = Attr(0, 2, def);
    CHECK(r.Styled(token, 0, 0, &xml, attrs, table)[0].data == 20);
    xml.style_index = 0;
    CHECK(r.Styled(token, def, 0x7f02ffff, &xml, attrs, table)[0].data == 20);
    CHECK(r.Styled(token, 0, default_id, nullptr, attrs, table)[0].data == 20);
    CHECK(r.Styled(token, 0, 0, nullptr, attrs, table)[0].data == 10);
    xml.attributes = {Attr(a, 1, 0)};
    CHECK(r.Styled(token, def, default_id, &xml, attrs, table)[0].Slots() ==
          std::array<std::uint32_t, 6>{0, 0, 0xffffffffU, 0, 0, 0});
    xml.attributes = {Attr(a, 0, 0)};
    CHECK(r.Styled(token, 0, default_id, &xml, attrs, table)[0].data == 20);
    const auto copy = r.New();
    r.Copy(copy, token);
    r.Apply(token, default_id, false, table);
    CHECK(r.Attribute(token, a, true, table)->data == 10);
    r.Apply(token, default_id, true, table);
    CHECK(r.Attribute(token, a, true, table)->data == 20);
    CHECK(r.Attribute(copy, a, true, table)->data == 10);
    table.entries.push_back(Style(0x7f020004, 0x7f020005, {}));
    table.entries.push_back(Style(0x7f020005, 0x7f020004, {}));
    CHECK_THROWS(r.Apply(token, 0x7f020004, true, table));
    table.entries.push_back(Style(0x7f020006, 0, {{a, 2, b, {}}, {b, 2, a, {}}}));
    r.Apply(token, 0x7f020006, true, table);
    CHECK_THROWS(static_cast<void>(r.Attribute(token, a, true, table)));
    // A resource cycle is distinct from a parent or theme-attribute cycle.
    ogplay::loader::ArscEntry ref;
    ref.resource_id = 0x7f030001;
    ref.type_name = "integer";
    ref.value_type = 1;
    ref.value_data = 0x7f030002;
    table.entries.push_back(ref);
    ref.resource_id = 0x7f030002;
    ref.value_data = 0x7f030001;
    table.entries.push_back(ref);
    CHECK_THROWS(static_cast<void>(r.Resource(0x7f030001, true, table)));
    CHECK(r.Resource(0x7f030001, false, table).data == 0x7f030002);
    table.entries.back().value_type = 16;
    table.entries.back().value_data = 77;
    table.entries.back().changing_configurations = 0x200;
    table.entries.back().density = 240;
    xml.attributes = {Attr(a, 1, 0x7f030001), Attr(b, 2, c)};
    values = r.Retrieve(xml, attrs, table);
    CHECK(values[0].Slots() == std::array<std::uint32_t, 6>{16, 77, 1, 0x7f030002, 0x200, 240});
    CHECK(values[1].type == 2);
    CHECK(values[1].data == c);
    CHECK(values[2].type == 0);
    CHECK_THROWS(static_cast<void>(r.Load(Projection())));
}

TEST_CASE("DVM-288 ROM Theme and TypedArray execute on both interpreter backends") {
    using ogplay::test::container_traversal::ContainerVm;
    using namespace ogplay::runtime::dexvm;
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ContainerVm f(backend);
        f.context->arsc = Table();
        f.context->arsc.global_string_pool = StringPool({"from APK"}, true);
        f.context->arsc.entries.push_back(Style(0x7f020010, theme_id,
                                                {{a, 16, 42, {}},
                                                 {b, 18, 1, {}},
                                                 {c, 4, 0x3fc00000, {}},
                                                 {0x7f010005, 5, 0x0a01, {}},
                                                 {0x7f010006, 1, 0, {}}}));
        const auto owner = f.New("Landroid/content/Context;");
        ogplay::test::AttachViewTestContext(f.vm, owner);
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
        f.context->arsc.entries.push_back(Style(0x7f020011, 0, {{a, 3, 0, {}}}));
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
        CHECK(unbound.exception.IsValid());
        CHECK(unbound.exception_message.find("Theme.Dialog.NoFrame") != std::string::npos);
        CHECK(unbound.exception_message.find("无 ROM ID 绑定") != std::string::npos);
        CHECK(
            f.OnOutcome(theme, "applyStyle", "(IZ)V", {VmValue::Int(0x0103ffff), VmValue::Int(1)}).exception.IsValid());
        const auto assets = f.On(resources, "getAssets", "()Landroid/content/res/AssetManager;").ref;
        const auto field = f.linker.FindFieldRecursive(f.model.ObjectClass(theme), "mTheme", "I");
        REQUIRE(field);
        const auto token = f.model.InstanceSlots(theme)[f.linker.Field(*field).slot].bits;
        const auto destroy = f.linker.FindDirectMethod(f.model.ObjectClass(assets), "deleteTheme", "(I)V");
        REQUIRE(destroy);
        f.Checked(
            f.vm.Call(*destroy, std::array{VmValue::Ref(assets), VmValue::Int(static_cast<std::int32_t>(token))}));
        CHECK(f.OnOutcome(theme, "resolveAttribute", "(ILandroid/util/TypedValue;Z)Z",
                          {VmValue::Int(a), VmValue::Ref(value), VmValue::Int(1)})
                  .exception.IsValid());
    }
}
