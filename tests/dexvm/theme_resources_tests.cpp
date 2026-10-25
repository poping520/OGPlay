#include "container_traversal_fixture.h"
#include "file_vfs_fixture.h"
#include "ogplay/runtime/integration/theme_resources.h"
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

ogplay::loader::ArscTable Table(std::vector<ogplay::loader::ArscEntry> extra = {},
                                std::vector<std::byte>* serialized = nullptr) {
    ogplay::loader::ArscTable t;
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
    CHECK(r.Attribute(token, 0x01010064, true, table)->Slots() ==
          std::array<std::uint32_t, 6>{1, 0x01080554, 1, 0x01080554, 0, 0});
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

TEST_CASE("DVM-289 ROM attribute constructor smoke") {
    using ogplay::test::container_traversal::ContainerVm;
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ContainerVm f(backend);
        f.context->ui_test_accessibility_disabled = true;
        f.context->arsc = Table();
        f.context->arsc.entries.push_back(
            Style(0x7f020020, theme_id, {{0x01010064, 1, 0, {}}, {0x01010065, 1, 0, {}}}));
        f.context->application_theme = 0x7f020020;
        const auto owner = f.New("Landroid/app/Activity;");
        ogplay::test::AttachViewTestContext(f.vm, owner);
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
    f.context->apk_bytes = ogplay::test::file_vfs::MakeStoredZip("res/layout/attributes.xml", xml);
    if (!arsc.empty()) {
        const std::string layout_name = "res/layout/attributes.xml", table_name = "resources.arsc";
        const auto table = ogplay::test::file_vfs::MakeStoredZip(table_name, arsc);
        const auto layout_local = 30U + layout_name.size() + xml.size();
        const auto table_local = 30U + table_name.size() + arsc.size();
        std::vector<std::byte> zip(f.context->apk_bytes.begin(),
                                   f.context->apk_bytes.begin() + static_cast<std::ptrdiff_t>(layout_local));
        zip.insert(zip.end(), table.begin(), table.begin() + static_cast<std::ptrdiff_t>(table_local));
        const auto central = static_cast<std::uint32_t>(zip.size());
        zip.insert(zip.end(), f.context->apk_bytes.begin() + static_cast<std::ptrdiff_t>(layout_local),
                   f.context->apk_bytes.end() - 22);
        auto table_central =
            std::vector<std::byte>(table.begin() + static_cast<std::ptrdiff_t>(table_local), table.end() - 22);
        Set32(table_central, 42, static_cast<std::uint32_t>(layout_local));
        zip.insert(zip.end(), table_central.begin(), table_central.end());
        const auto central_size = static_cast<std::uint32_t>(zip.size()) - central;
        Append32(zip, 0x06054b50);
        Append16(zip, 0);
        Append16(zip, 0);
        Append16(zip, 2);
        Append16(zip, 2);
        Append32(zip, central_size);
        Append32(zip, central);
        Append16(zip, 0);
        f.context->apk_bytes = std::move(zip);
        const auto bytes = ogplay::loader::ReadApkEntry(
            f.context->apk_bytes, ogplay::loader::ParseApkArchive(f.context->apk_bytes), "resources.arsc");
        f.context->arsc =
            ogplay::loader::ParseArsc({reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size()});
    }
    f.context->archive = ogplay::loader::ParseApkArchive(f.context->apk_bytes);
    const auto assets = f.On(owner, "getAssets", "()Landroid/content/res/AssetManager;").ref;
    const auto parser =
        f.On(assets, "openXmlResourceParser", "(Ljava/lang/String;)Landroid/content/res/XmlResourceParser;",
             {VmValue::Ref(f.vm.NewStringUtf8("res/layout/attributes.xml"))})
            .ref;
    CHECK(f.linker.Class(f.model.ObjectClass(parser)).descriptor == "Landroid/content/res/XmlBlock$Parser;");
    CHECK(f.On(parser, "next", "()I").AsInt() == 0);
    CHECK(f.On(parser, "next", "()I").AsInt() == 2);
    return parser;
}
}  // namespace

TEST_CASE("DVM-289 XML attributes construct ROM containers and LayoutParams") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ContainerVm f(backend);
        f.context->ui_test_accessibility_disabled = true;
        f.context->arsc = Table({Style(0x7f020004, theme_id, {{0x01010064, 1, 0, {}}, {0x01010065, 1, 0, {}}})});
        f.context->application_theme = 0x7f020004;
        const auto owner = f.New("Landroid/app/Activity;");
        ogplay::test::AttachViewTestContext(f.vm, owner);
        f.On(owner, "setTheme", "(I)V", {VmValue::Int(static_cast<std::int32_t>(f.context->application_theme))});
        ogplay::loader::ArscEntry id;
        id.resource_id = 0x7f030001;
        id.type_name = "id";
        id.value_type = 16;
        f.context->arsc.entries.push_back(id);
        const auto parser = OpenAttributeXml(
            f, owner,
            AttributeXml({{
                {0x010100d0, 1, 0x7f030001},                       // id
                {0x010100dc, 16, 1},                               // invisible
                {0x010100ea, 18, 0},         {0x010100eb, 18, 0},  // group clip flags
                {0x010100ed, 18, 0},         {0x010100ee, 16, 3},         {0x010100ef, 18, 0},
                {0x010102ef, 18, 0},         {0x0101010a, 18, 1},         {0x01010200, 16, 0x33},
                {0x0101042e, 18, 0},         {0x010102d4, 18, 1},         {0x01010329, 16, 3},
                {0x0101032a, 5, 0x701},      {0x010101ff, 1, 0x7f030001}, {0x010100d5, 5, 0x501},  // padding 5dp
                {0x0101013f, 5, 0x1f01},                                                           // minWidth 31dp
                {0x01010140, 5, 0x1701},                                                           // minHeight 23dp
                {0x010100c4, 16, 1},                                                               // vertical
                {0x010100af, 16, 0x55},                                                            // gravity
                {0x01010128, 4, 0x40800000},                                                       // weightSum 4
                {0x01010126, 18, 0},  // baselineAligned false (verified against pinned IDs below)
                {0x010100f4, 5, 0x2801},     {0x010100f5, 5, 0x1401},  // width/height 40x20
                {0x010100f6, 5, 0x301},                                // margin 3
                {0x010100b3, 16, 0x33},                                // layout gravity
                {0x01010181, 4, 0x40000000}                            // layout weight 2
            }}));
        const auto roots = f.vm.ProtectReferences(std::array{owner, parser});
        for (const auto* type : {"Landroid/view/View;", "Ltest/HierarchyGroup;", "Landroid/widget/FrameLayout;",
                                 "Landroid/widget/LinearLayout;", "Landroid/widget/RelativeLayout;"}) {
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
              "Landroid/widget/RelativeLayout$LayoutParams;"}) {
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
            if (std::string_view(type) != "Landroid/view/ViewGroup$LayoutParams;") {
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
        f.context->ui_test_accessibility_disabled = true;
        std::vector<std::byte> table_bytes;
        f.context->arsc = Table(
            {Style(0x7f020004, theme_id,
                   {{0x01010064, 1, 0, {}}, {0x01010065, 1, 0, {}}, {0x010100c4, 16, 1, {}}, {def, 1, 0x7f020005, {}}}),
             Style(0x7f020005, 0, {{0x010100c4, 16, 0, {}}}), Style(0x7f020006, 0, {{0x010100c4, 16, 1, {}}})},
            &table_bytes);
        f.context->application_theme = 0x7f020004;
        const auto owner = f.New("Landroid/app/Activity;");
        ogplay::test::AttachViewTestContext(f.vm, owner);
        f.On(owner, "setTheme", "(I)V", {VmValue::Int(static_cast<std::int32_t>(f.context->application_theme))});
        ogplay::loader::ArscEntry id;
        id.resource_id = 0x7f030001;
        id.type_name = "id";
        id.value_type = 16;
        f.context->arsc.entries.push_back(id);
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
        f.context->arsc = Table({Style(0x7f020004, theme_id, {{0x01010064, 1, 0, {}}, {0x01010065, 1, 0, {}}})});
        f.context->application_theme = 0x7f020004;
        const auto owner = f.New("Landroid/app/Activity;");
        ogplay::test::AttachViewTestContext(f.vm, owner);
        f.On(owner, "setTheme", "(I)V", {VmValue::Int(static_cast<std::int32_t>(f.context->application_theme))});
        std::vector<XmlValue> attrs{{0x010100f4, 16, 0xfffffffe}, {0x010100f5, 16, 0xffffffff}, {0x01010192, 18, 1}};
        for (const auto& [id, verb] : rules) {
            const bool boolean = (verb >= 9 && verb <= 15) || verb >= 20;
            attrs.push_back({id, static_cast<std::uint8_t>(boolean ? 18 : 1),
                             boolean ? 1U : 0x7f030100U + static_cast<std::uint32_t>(verb)});
            if (!boolean) {
                ogplay::loader::ArscEntry value;
                value.resource_id = 0x7f030100U + static_cast<std::uint32_t>(verb);
                value.type_name = "id";
                value.value_type = 16;
                f.context->arsc.entries.push_back(value);
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

TEST_CASE("DVM-289 attributed trees match code trees through VD05 traversal") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ContainerVm f(backend);
        f.context->ui_test_accessibility_disabled = true;
        f.context->arsc = Table({Style(0x7f020004, theme_id, {{0x01010064, 1, 0, {}}, {0x01010065, 1, 0, {}}})});
        f.context->application_theme = 0x7f020004;
        const auto owner = f.New("Landroid/app/Activity;");
        f.context->activity = owner;
        ogplay::test::AttachViewTestContext(f.vm, owner);
        f.On(owner, "setTheme", "(I)V", {VmValue::Int(0x7f020004)});
        for (const auto* type : {"Ltest/HierarchyGroup;", "Landroid/widget/FrameLayout;",
                                 "Landroid/widget/LinearLayout;", "Landroid/widget/RelativeLayout;"}) {
            INFO(std::string(type));
            std::array<std::array<std::uint32_t, 8>, 2> geometry{};
            for (int variant = 0; variant < 2; ++variant) {
                INFO(variant);
                const auto parser = OpenAttributeXml(
                    f, owner,
                    AttributeXml({{{0x010100d5, 5, 0x501}},
                                  {{0x010100f4, 5, 0x2801}, {0x010100f5, 5, 0x1401}, {0x010100f6, 5, 0x301}}}));
                const auto parser_root = f.vm.ProtectReferences(std::array{parser});
                const auto tree = variant == 0 ? f.New(type, "(Landroid/content/Context;Landroid/util/AttributeSet;)V",
                                                       {VmValue::Ref(owner), VmValue::Ref(parser)})
                                               : f.New(type, "(Landroid/content/Context;)V", {VmValue::Ref(owner)});
                const auto tree_root = f.vm.ProtectReferences(std::array{tree});
                if (variant)
                    f.On(tree, "setPadding", "(IIII)V",
                         {VmValue::Int(5), VmValue::Int(5), VmValue::Int(5), VmValue::Int(5)});
                CHECK(f.On(parser, "next", "()I").AsInt() == 2);
                const auto child =
                    variant == 0
                        ? f.New("Landroid/view/View;", "(Landroid/content/Context;Landroid/util/AttributeSet;)V",
                                {VmValue::Ref(owner), VmValue::Ref(parser)})
                        : f.New("Landroid/view/View;", "(Landroid/content/Context;)V", {VmValue::Ref(owner)});
                const auto child_root = f.vm.ProtectReferences(std::array{child});
                auto lp =
                    f.On(tree, "generateLayoutParams",
                         "(Landroid/util/AttributeSet;)Landroid/view/ViewGroup$LayoutParams;", {VmValue::Ref(parser)})
                        .ref;
                if (variant) {
                    const auto lp_type = f.linker.Class(f.model.ObjectClass(lp)).descriptor;
                    lp = f.New(lp_type.c_str(), "(II)V", {VmValue::Int(40), VmValue::Int(20)});
                    if (std::string_view(type) != "Ltest/HierarchyGroup;")
                        f.On(lp, "setMargins", "(IIII)V",
                             {VmValue::Int(3), VmValue::Int(3), VmValue::Int(3), VmValue::Int(3)});
                }
                f.On(tree, "addView", "(Landroid/view/View;Landroid/view/ViewGroup$LayoutParams;)V",
                     {VmValue::Ref(child), VmValue::Ref(lp)});
                f.On(owner, "setContentView", "(Landroid/view/View;)V", {VmValue::Ref(tree)});
                REQUIRE(EnsureAndroidUiTraversal(f.vm, *f.context, AndroidUiTraversalReason::OwnerTick,
                                                 AndroidUiTraversalPhase::ResolveOnly, ui::Size{200, 140}));
                geometry[static_cast<std::size_t>(variant)] = {
                    JavaField(f, tree, "mMeasuredWidth").bits,  JavaField(f, tree, "mMeasuredHeight").bits,
                    JavaField(f, child, "mMeasuredWidth").bits, JavaField(f, child, "mMeasuredHeight").bits,
                    JavaField(f, child, "mLeft").bits,          JavaField(f, child, "mTop").bits,
                    JavaField(f, child, "mRight").bits,         JavaField(f, child, "mBottom").bits};
            }
            CHECK(geometry[0] == geometry[1]);
            CHECK(geometry[0][0] == 200);
            CHECK(geometry[0][1] == 140);
            CHECK(geometry[0][2] == 40);
            CHECK(geometry[0][3] == 20);
            const auto offset = std::string_view(type) == "Ltest/HierarchyGroup;" ? 0U : 8U;
            CHECK(geometry[0][4] == offset);
            CHECK(geometry[0][5] == offset);
            CHECK(geometry[0][6] == offset + 40);
            CHECK(geometry[0][7] == offset + 20);
        }
        CHECK(f.ledger.Unimplemented().empty());
    }
}

TEST_CASE("DVM-289 missing dimensions unsupported drawables and themes explicitly fail") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ContainerVm f(backend);
        f.context->ui_test_accessibility_disabled = true;
        f.context->arsc = Table({Style(0x7f020004, theme_id, {{0x01010064, 1, 0, {}}, {0x01010065, 1, 0, {}}})});
        f.context->application_theme = 0x7f020004;
        const auto owner = f.New("Landroid/app/Activity;");
        ogplay::test::AttachViewTestContext(f.vm, owner);
        f.On(owner, "setTheme", "(I)V", {VmValue::Int(0x7f020004)});
        const auto parser = OpenAttributeXml(f, owner,
                                             AttributeXml({{},
                                                           {{0x010100f4, 3, 0}, {0x010100f5, 16, 10}},
                                                           {{0x010100d4, 28, 0xff123456}},
                                                           {{0x01010109, 28, 0xff123456}},
                                                           {{0x01010129, 28, 0xff123456}},
                                                           {{0x01010064, 28, 0xff123456}},
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
        for (const auto* type : {"Landroid/view/View;", "Landroid/widget/FrameLayout;", "Landroid/widget/LinearLayout;",
                                 "Landroid/view/View;", "Landroid/view/View;"}) {
            CHECK(f.On(parser, "next", "()I").AsInt() == 2);
            const auto object = f.vm.NewIntrinsicInstance(type);
            const auto ctor = f.linker.FindDirectMethod(f.model.ObjectClass(object), "<init>",
                                                        "(Landroid/content/Context;Landroid/util/AttributeSet;)V");
            REQUIRE(ctor);
            const auto result =
                f.vm.Call(*ctor, std::array{VmValue::Ref(object), VmValue::Ref(owner), VmValue::Ref(parser)});
            REQUIRE(result.exception.IsValid());
        }
        CHECK_FALSE(f.ledger.Unimplemented().empty());
        REQUIRE(f.OnOutcome(owner, "setTheme", "(I)V", {VmValue::Int(0x0103ffff)}).exception.IsValid());
    }
}

namespace {
struct InflateNode {
    std::string tag;
    std::vector<XmlValue> attrs;
    std::vector<InflateNode> children;
};

std::vector<std::byte> InflateXml(const InflateNode& root) {
    std::vector<std::string> strings{"layout_width", "layout_height", "layout", "padding", "id", "scaleType"};
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
    Append32(bytes, 32);
    for (auto id : {0x010100f4U, 0x010100f5U, 0U, 0x010100d5U, 0x010100d0U, 0x0101011dU})
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

std::vector<std::byte> InflateResourceTable(const ogplay::loader::ArscTable& table) {
    std::vector<std::string> values{"from APK"}, keys;
    for (const auto& entry : table.entries) {
        keys.push_back(entry.entry_name);
        if (entry.string_value) values.push_back(*entry.string_value);
    }
    std::vector<std::byte> chunks;
    for (const auto type : {2U, 3U, 4U}) {
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
    auto global = StringPool(values, true), types = StringPool({"attr", "style", "layout", "id"}, true),
         names = StringPool(keys, true);
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

void InstallInflateLayouts(ContainerVm& f, const std::vector<InflateNode>& layouts) {
    std::vector<std::pair<std::string, std::vector<std::byte>>> files;
    std::erase_if(f.context->arsc.entries, [](const auto& entry) { return entry.type_name == "layout"; });
    for (std::size_t i = 0; i < layouts.size(); ++i) {
        const auto path = "res/layout/inflate" + std::to_string(i) + ".xml";
        files.emplace_back(path, InflateXml(layouts[i]));
        ogplay::loader::ArscEntry entry;
        entry.resource_id = 0x7f030001U + static_cast<std::uint32_t>(i);
        entry.type_name = "layout";
        entry.entry_name = "inflate" + std::to_string(i);
        entry.string_value = path;
        entry.value_type = 3;
        f.context->arsc.entries.push_back(entry);
    }
    files.emplace_back("resources.arsc", InflateResourceTable(f.context->arsc));
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
    f.context->arsc = ogplay::loader::ParseArsc({reinterpret_cast<const std::uint8_t*>(arsc.data()), arsc.size()});
}

VmObjectRef InflaterActivity(ContainerVm& f) {
    f.context->ui_test_accessibility_disabled = true;
    f.context->arsc = Table(
        {Style(0x7f020004, theme_id, {{0x01010064, 1, 0, {}}, {0x01010065, 1, 0, {}}, {0x0101013f, 5, 0x601, {}}})});
    ogplay::loader::ArscEntry id;
    id.resource_id = 0x7f040001;
    id.type_name = "id";
    id.entry_name = "included";
    id.value_type = 16;
    f.context->arsc.entries.push_back(id);
    f.context->application_theme = 0x7f020004;
    const auto owner = f.New("Landroid/app/Activity;");
    f.context->activity = owner;
    ogplay::test::AttachViewTestContext(f.vm, owner);
    f.On(owner, "setTheme", "(I)V", {VmValue::Int(0x7f020004)});
    return owner;
}

std::vector<XmlValue> InflateSize(std::uint32_t width, std::uint32_t height) {
    return {{0x010100f4, 16, width}, {0x010100f5, 16, height}};
}
}  // namespace

TEST_CASE("DVM-290 ROM inflater setContentView nested include merge and atomic failure") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ContainerVm f(backend);
        const auto owner = InflaterActivity(f);
        const auto full = InflateSize(0xffffffffU, 0xffffffffU), small = InflateSize(40, 20);
        std::vector<InflateNode> layouts = {
            {"FrameLayout",
             full,
             {{"LinearLayout",
               full,
               {{"RelativeLayout", full, {{"test.HierarchyGroup", full, {{"test.InflatedLeaf", small, {}}}}}}}},
              {"include", {{0, 1, 0x7f030002}}, {}}}},
            {"merge", {}, {{"View", small, {}}}},
            {"merge", {}, {{"View", small, {}}, {"TextView", small, {}}}}};
        // ImageView now constructs; MATRIX remains an explicit unsupported request.
        for (const auto* tag : {"ImageView", "Button", "EditText", "ScrollView", "ViewStub", "blink"}) {
            auto attributes = small;
            if (std::string_view(tag) == "ImageView") attributes.push_back({0x0101011d, 16, 0});
            layouts.push_back({"merge", {}, {{"View", small, {}}, {tag, attributes, {}}}});
        }
        InstallInflateLayouts(f, layouts);
        f.On(owner, "setContentView", "(I)V", {VmValue::Int(0x7f030001)});
        const auto root = f.context->content_view;
        CHECK(f.linker.Class(f.model.ObjectClass(root)).descriptor == "Landroid/widget/FrameLayout;");
        CHECK(f.On(root, "getChildCount", "()I").AsInt() == 2);
        auto child = root;
        for (const auto* type : {"Landroid/widget/LinearLayout;", "Landroid/widget/RelativeLayout;",
                                 "Ltest/HierarchyGroup;", "Ltest/InflatedLeaf;"}) {
            child = f.On(child, "getChildAt", "(I)Landroid/view/View;", {VmValue::Int(0)}).ref;
            CHECK(f.linker.Class(f.model.ObjectClass(child)).descriptor == type);
        }
        CHECK(f.On(child, "getContext", "()Landroid/content/Context;").ref == owner);
        const auto lp = f.On(child, "getLayoutParams", "()Landroid/view/ViewGroup$LayoutParams;").ref;
        CHECK(JavaField(f, lp, "width").bits == 40);
        CHECK(JavaField(f, lp, "height").bits == 20);
        REQUIRE(EnsureAndroidUiTraversal(f.vm, *f.context, AndroidUiTraversalReason::OwnerTick,
                                         AndroidUiTraversalPhase::ResolveOnly, ui::Size{200, 140}));
        CHECK(JavaField(f, child, "mMeasuredWidth").bits == 40);
        CHECK(JavaField(f, child, "mMeasuredHeight").bits == 20);
        CHECK(JavaField(f, child, "mMinWidth").bits == 6);
        CHECK(JavaField(f, child, "mLeft").bits == 0);
        CHECK(JavaField(f, child, "mRight").bits == 40);
        const auto visible = f.On(root, "getChildAt", "(I)Landroid/view/View;", {VmValue::Int(1)}).ref;
        f.On(visible, "setClickable", "(Z)V", {VmValue::Int(1)});
        const auto visible_id = *FindViewUiNode(*f.context, visible.Value());
        f.context->ui_tree.Get(visible_id)->background_color = 0x123456ff;
        REQUIRE(EnsureAndroidUiTraversal(f.vm, *f.context, AndroidUiTraversalReason::OwnerTick,
                                         AndroidUiTraversalPhase::ResolveOnly, ui::Size{200, 140}));
        const auto old_projection = f.context->ui_tree.PublishedProjection();
        CHECK(old_projection->Get(visible_id)->frame == ui::Rect{0, 0, 40, 20});
        CHECK(old_projection->HitTestProjection(1, 1) == visible_id);
        const auto pixels = ui::RasterizeUiOverlay(ui::BuildUiRenderList(*old_projection, {}), {200, 140});
        CHECK(pixels.rgba8[(1 * 200 + 1) * 4 + 3] == 255);
        CHECK(pixels.rgba8[(1 * 200 + 40) * 4 + 3] == 0);
        CHECK(f.ledger.Unimplemented().empty());
        const auto revision = f.context->ui_content_revision;
        for (std::size_t i = 2; i < layouts.size(); ++i) {
            const auto error =
                f.OnOutcome(owner, "setContentView", "(I)V", {VmValue::Int(static_cast<int>(0x7f030001 + i))});
            INFO("layout index: " << i);
            REQUIRE(error.exception.IsValid());
            CHECK(f.linker.Class(error.exception_class).descriptor == (i == layouts.size() - 1
                                                                           ? "Ljava/lang/UnsupportedOperationException;"
                                                                           : "Landroid/view/InflateException;"));
            CHECK(f.context->content_view == root);
            CHECK(f.context->ui_content_revision == revision);
            CHECK(f.context->ui_tree.PublishedProjection() == old_projection);
            CHECK(f.On(root, "getChildCount", "()I").AsInt() == 2);
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

TEST_CASE("DVM-290 ROM include nonmerge overrides and merge parent constraints") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ContainerVm f(backend);
        const auto owner = InflaterActivity(f);
        InstallInflateLayouts(
            f, {{"FrameLayout",
                 InflateSize(100, 80),
                 {{"include",
                   {{0, 1, 0x7f030002}, {0x010100f4, 16, 30}, {0x010100f5, 16, 12}, {0x010100d0, 1, 0x7f040001}},
                   {}}}},
                {"View", InflateSize(60, 40), {}},
                {"merge", {}, {{"View", InflateSize(10, 10), {}}}}});
        const auto inflater =
            f.Static("Landroid/view/LayoutInflater;", "from",
                     "(Landroid/content/Context;)Landroid/view/LayoutInflater;", {VmValue::Ref(owner)})
                .ref;
        const auto root = f.On(inflater, "inflate", "(ILandroid/view/ViewGroup;)Landroid/view/View;",
                               {VmValue::Int(0x7f030001), VmValue::Ref(VmObjectRef{})})
                              .ref;
        const auto child = f.On(root, "getChildAt", "(I)Landroid/view/View;", {VmValue::Int(0)}).ref;
        const auto lp = f.On(child, "getLayoutParams", "()Landroid/view/ViewGroup$LayoutParams;").ref;
        CHECK(JavaField(f, lp, "width").bits == 30);
        CHECK(JavaField(f, lp, "height").bits == 12);
        CHECK(f.On(child, "getId", "()I").AsInt() == 0x7f040001);
        const auto error = f.OnOutcome(inflater, "inflate", "(ILandroid/view/ViewGroup;Z)Landroid/view/View;",
                                       {VmValue::Int(0x7f030003), VmValue::Ref(VmObjectRef{}), VmValue::Int(0)});
        REQUIRE(error.exception.IsValid());
        CHECK(f.linker.Class(error.exception_class).descriptor == "Landroid/view/InflateException;");
        CHECK(error.exception_message ==
              "<merge /> can be used only with a valid ViewGroup root and attachToRoot=true");
        const auto resources = f.On(owner, "getResources", "()Landroid/content/res/Resources;").ref;
        const auto parser =
            f.On(resources, "getLayout", "(I)Landroid/content/res/XmlResourceParser;", {VmValue::Int(0x7f030002)}).ref;
        CHECK(f.On(inflater, "inflate", "(Lorg/xmlpull/v1/XmlPullParser;Landroid/view/ViewGroup;)Landroid/view/View;",
                   {VmValue::Ref(parser), VmValue::Ref(VmObjectRef{})})
                  .ref.IsValid());
        f.On(parser, "close", "()V");
        f.On(owner, "setContentView", "(Landroid/view/View;)V", {VmValue::Ref(root)});
        const auto parent = f.On(root, "getParent", "()Landroid/view/ViewParent;").ref;
        REQUIRE(parent.IsValid());
        const auto extra = f.New("Landroid/view/View;", "(Landroid/content/Context;)V", {VmValue::Ref(owner)});
        const auto params =
            f.New("Landroid/widget/FrameLayout$LayoutParams;", "(II)V", {VmValue::Int(7), VmValue::Int(9)});
        f.On(owner, "addContentView", "(Landroid/view/View;Landroid/view/ViewGroup$LayoutParams;)V",
             {VmValue::Ref(extra), VmValue::Ref(params)});
        CHECK(f.On(parent, "getChildCount", "()I").AsInt() == 2);
        CHECK(f.On(extra, "getParent", "()Landroid/view/ViewParent;").ref == parent);
        const auto window = f.On(owner, "getWindow", "()Landroid/view/Window;").ref;
        f.On(window, "setContentView", "(Landroid/view/View;Landroid/view/ViewGroup$LayoutParams;)V",
             {VmValue::Ref(extra), VmValue::Ref(params)});
        CHECK(f.context->content_view == extra);
        CHECK(f.On(parent, "getChildCount", "()I").AsInt() == 1);
        REQUIRE(EnsureAndroidUiTraversal(f.vm, *f.context, AndroidUiTraversalReason::OwnerTick,
                                         AndroidUiTraversalPhase::ResolveOnly, ui::Size{200, 140}));
        CHECK(JavaField(f, extra, "mMeasuredWidth").bits == 7);
        CHECK(JavaField(f, extra, "mMeasuredHeight").bits == 9);
        CHECK(f.ledger.Unimplemented().empty());
    }
}

TEST_CASE("DVM-292 leaf bitmap and scalar colors execute ROM getters on both backends") {
    // TextViewAppearance must retain a style ID for ROM getResourceId instead
    // of trying to materialize the bag as a scalar drawable/color.
    ThemeResources projection;
    projection.Load(Projection());
    const auto projected = projection.New();
    auto projected_table = Table();
    projection.Apply(projected, 0x01030005, true, projected_table);
    const auto appearance =
        projection.Styled(projected, 0, 0, nullptr, std::array<std::uint32_t, 1>{0x01010034}, projected_table);
    CHECK(appearance[0].type == 1);
    CHECK(appearance[0].data == 0x0103003e);
    CHECK(appearance[0].resource_id == appearance[0].data);
    projected_table.entries.push_back(Style(0x7f020020, 0, {{a, 1, default_id, {}}}));
    projection.Apply(projected, 0x7f020020, true, projected_table);
    CHECK(projection.Styled(projected, 0, 0, nullptr, std::array{a}, projected_table)[0].resource_id == default_id);
    using ogplay::test::container_traversal::ContainerVm;
    using namespace ogplay::runtime::dexvm;
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ContainerVm f(backend);
        f.context->ui_density = 2;
        f.context->apk_bytes = ogplay::test::BitmapDecodeApk();
        f.context->archive = ogplay::loader::ParseApkArchive(f.context->apk_bytes);
        f.context->arsc = Table();
        f.context->arsc.global_string_pool = StringPool({"res/drawable/fixture.png", "res/drawable/leaf.jpg"}, true);
        constexpr std::uint32_t image = 0x7f030001, color = 0x7f030002, alias = 0x7f030003;
        f.context->arsc.entries.push_back({.resource_id = image,
                                           .type_name = "drawable",
                                           .entry_name = "leaf",
                                           .string_value = "res/drawable/fixture.png",
                                           .value_type = 3,
                                           .changing_configurations = 0x100,
                                           .density = 160});
        f.context->arsc.entries.push_back({.resource_id = color,
                                           .type_name = "color",
                                           .entry_name = "color",
                                           .value_type = 28,
                                           .value_data = 0xff123456,
                                           .changing_configurations = 0x200});
        f.context->arsc.entries.push_back({.resource_id = alias,
                                           .type_name = "drawable",
                                           .entry_name = "alias",
                                           .value_type = 1,
                                           .value_data = image,
                                           .changing_configurations = 0x400});
        f.context->arsc.entries.push_back(Style(0x7f020020, 0, {{a, 1, alias, {}}, {b, 1, color, {}}, {c, 1, 0, {}}}));
        const auto owner = f.New("Landroid/content/Context;");
        ogplay::test::AttachViewTestContext(f.vm, owner);
        const auto res = f.On(owner, "getResources", "()Landroid/content/res/Resources;").ref;
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
        const std::array<std::uint32_t, 18> expected{3,     0,     1, image, 0x500, 160,         28, 0xff123456, 1,
                                                     color, 0x200, 0, 0,     0,     0xffffffffU, 0,  0,          0};
        for (int i = 0; i < 18; ++i)
            CHECK(static_cast<std::uint32_t>(f.model.GetPrimitiveElement(data, i)) ==
                  expected[static_cast<std::size_t>(i)]);
        VmObjectRef drawable;
        std::weak_ptr<const ogplay::runtime::ui::UiBitmap> weak_pixels;
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
            CHECK(f.On(bitmap, "getDensity", "()I").AsInt() == 160);
            const auto& state = f.context->ui_drawables.at(drawable.Value());
            REQUIRE(state.leaf_pixels);
            CHECK(state.leaf_pixels->rgba8 == std::vector<std::uint8_t>{255, 0, 0, 255, 0, 0, 255, 255});
            weak_pixels = state.leaf_pixels;
            CHECK(f.context->ui_bitmaps.empty());
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
            // The same selected entry facts drive both native six-slot entry points.
            ogplay::loader::CompiledXmlNode xml;
            xml.attributes = {Attr(a, 1, alias), Attr(b, 1, color), Attr(c, 1, 0)};
            const auto retrieved = f.context->theme_resources.Retrieve(xml, std::array{a, b, c}, f.context->arsc);
            for (std::size_t i = 0; i < retrieved.size(); ++i)
                CHECK(retrieved[i].Slots() == std::array<std::uint32_t, 6>{expected[i * 6], expected[i * 6 + 1],
                                                                           expected[i * 6 + 2], expected[i * 6 + 3],
                                                                           expected[i * 6 + 4], expected[i * 6 + 5]});
        }
        static_cast<void>(f.vm.CollectGarbage("leaf-unrooted"));
        CHECK(weak_pixels.expired());
        CHECK_FALSE(f.context->ui_drawables.contains(drawable.Value()));
        // Configuration and density selection: requested 320 chooses 240 over 160;
        // a nonmatching orientation and a newer API cannot steal the resource.
        auto variant = f.context->arsc.entries[3];
        variant.density = 240;
        variant.changing_configurations = 0x800;
        f.context->arsc.entries.push_back(variant);
        variant.density = 320;
        variant.orientation = 1;
        f.context->arsc.entries.push_back(variant);
        variant.orientation = 0;
        variant.sdk_version = 20;
        f.context->arsc.entries.push_back(variant);
        const auto chosen =
            f.On(res, "getDrawable", "(I)Landroid/graphics/drawable/Drawable;", {VmValue::Int(image)}).ref;
        const auto chosen_root = f.vm.ProtectReferences(std::array{chosen});
        CHECK(f.On(chosen, "getIntrinsicWidth", "()I").AsInt() == 3);
        CHECK(f.context->theme_resources.Resource(image, true, f.context->arsc).Slots() ==
              std::array<std::uint32_t, 6>{3, 0, 1, image, 0x800, 240});
        for (auto& entry : f.context->arsc.entries)
            if (entry.resource_id == image) entry.density = 65535;
        const auto nodpi =
            f.On(res, "getDrawable", "(I)Landroid/graphics/drawable/Drawable;", {VmValue::Int(image)}).ref;
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
        f.context->arsc.entries.push_back({.resource_id = 0x7f030010,
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
        CHECK(f.context->ui_drawables.at(jpeg_drawable.Value()).leaf_pixels->rgba8.size() == 8);
        for (const auto& hit : f.ledger.Unimplemented())
            CHECK_MESSAGE(hit.count == 0, hit.id);
        for (const auto& [object, name, descriptor] : std::vector<std::tuple<VmObjectRef, const char*, const char*>>{
                 {typed, "getDrawable", "(I)Landroid/graphics/drawable/Drawable;"},
                 {typed, "getColorStateList", "(I)Landroid/content/res/ColorStateList;"},
                 {res, "getDrawable", "(I)Landroid/graphics/drawable/Drawable;"},
                 {res, "getColorStateList", "(I)Landroid/content/res/ColorStateList;"},
                 {chosen, "getIntrinsicWidth", "()I"}}) {
            const auto method = f.linker.FindVtableIndex(f.model.ObjectClass(object), name, descriptor);
            REQUIRE(method);
            CHECK(f.linker.Method(f.linker.Class(f.model.ObjectClass(object)).vtable[*method]).kind ==
                  MethodKind::interpreted);
        }
    }
}

TEST_CASE("DVM-292 unsupported leaf resources fail before publishing six slots") {
    using ogplay::test::container_traversal::ContainerVm;
    using namespace ogplay::runtime::dexvm;
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ContainerVm f(backend);
        f.context->arsc = Table();
        f.context->arsc.global_string_pool = StringPool({"res/drawable/fixture.png"}, true);
        const auto owner = f.New("Landroid/content/Context;");
        ogplay::test::AttachViewTestContext(f.vm, owner);
        const auto res = f.On(owner, "getResources", "()Landroid/content/res/Resources;").ref;
        const auto theme = f.On(res, "newTheme", "()Landroid/content/res/Resources$Theme;").ref;
        const auto roots = f.vm.ProtectReferences(std::array{owner, res, theme});
        f.On(theme, "applyStyle", "(IZ)V", {VmValue::Int(theme_id), VmValue::Int(1)});
        const auto attrs = f.model.NewPrimitiveArray(f.linker.ResolveDescriptor("[I"), JniPrimitiveKind::integer, 1);
        const auto data = f.model.NewPrimitiveArray(f.linker.ResolveDescriptor("[I"), JniPrimitiveKind::integer, 6);
        const auto indices = f.model.NewPrimitiveArray(f.linker.ResolveDescriptor("[I"), JniPrimitiveKind::integer, 2);
        const auto arrays_root = f.vm.ProtectReferences(std::array{attrs, data, indices});
        f.model.SetPrimitiveElement(attrs, 0, a);
        const auto token_field = f.linker.FindFieldRecursive(f.model.ObjectClass(theme), "mTheme", "I");
        REQUIRE(token_field);
        const auto token = f.model.InstanceSlots(theme)[f.linker.Field(*token_field).slot].bits;
        const auto apply = f.linker.FindDirectMethod(f.linker.ResolveDescriptor("Landroid/content/res/AssetManager;"),
                                                     "applyStyle", "(IIII[I[I[I)Z");
        REQUIRE(apply);
        const auto before = f.context->ui_tree.PublishedProjection();
        const auto before_count = f.context->ui_drawables.size();
        for (const auto* path :
             {"res/color/selector.xml", "res/drawable/image.9.png", "res/drawable/shape.xml",
              "res/drawable/unknown.bin", "res/drawable/fixture.png", "res/drawable/optical.png", "cycle"}) {
            f.context->arsc.entries.resize(3);
            auto bytes = ogplay::test::BitmapDecodePng();
            // Hidden compiled ninepatch chunk, even when its filename is ordinary PNG.
            if (std::string_view(path).ends_with("fixture.png")) {
                const std::array<std::byte, 12> chunk{std::byte{0},   std::byte{0},   std::byte{0},   std::byte{0},
                                                      std::byte{'n'}, std::byte{'p'}, std::byte{'T'}, std::byte{'c'}};
                bytes.insert(bytes.begin() + 33, chunk.begin(), chunk.end());
            }
            if (std::string_view(path).ends_with("optical.png")) {
                std::array<std::byte, 28> chunk{};
                chunk[3] = std::byte{16};
                chunk[4] = std::byte{'n'};
                chunk[5] = std::byte{'p'};
                chunk[6] = std::byte{'L'};
                chunk[7] = std::byte{'b'};
                bytes.insert(bytes.begin() + 33, chunk.begin(), chunk.end());
            }
            f.context->apk_bytes = ogplay::test::file_vfs::MakeStoredZip(path, bytes);
            f.context->archive = ogplay::loader::ParseApkArchive(f.context->apk_bytes);
            f.context->arsc.entries.push_back(
                {.resource_id = 0x7f030001,
                 .type_name = std::string_view(path).starts_with("res/color") ? "color" : "drawable",
                 .entry_name = "unsupported",
                 .string_value = path,
                 .value_type = 3});
            if (std::string_view(path) == "cycle") {
                f.context->arsc.entries.back().value_type = 1;
                f.context->arsc.entries.back().value_data = 0x7f030001;
            }
            f.context->arsc.entries.push_back(Style(0x7f020020, 0, {{a, 1, 0x7f030001, {}}}));
            f.On(theme, "applyStyle", "(IZ)V", {VmValue::Int(0x7f020020), VmValue::Int(1)});
            for (int i = 0; i < 6; ++i)
                f.model.SetPrimitiveElement(data, i, 0x77777777);
            for (int i = 0; i < 2; ++i)
                f.model.SetPrimitiveElement(indices, i, 0x55555555);
            const auto failed = f.vm.Call(*apply, std::array{VmValue::Int(static_cast<int>(token)), VmValue::Int(0),
                                                             VmValue::Int(0), VmValue::Int(0), VmValue::Ref(attrs),
                                                             VmValue::Ref(data), VmValue::Ref(indices)});
            REQUIRE(failed.exception.IsValid());
            CHECK(failed.exception_message.size() > 0);
            for (int i = 0; i < 6; ++i)
                CHECK(f.model.GetPrimitiveElement(data, i) == 0x77777777);
            for (int i = 0; i < 2; ++i)
                CHECK(f.model.GetPrimitiveElement(indices, i) == 0x55555555);
            CHECK(f.OnOutcome(res, "getDrawable", "(I)Landroid/graphics/drawable/Drawable;", {VmValue::Int(0x7f030001)})
                      .exception.IsValid());
            CHECK(f.OnOutcome(res, "getColorStateList", "(I)Landroid/content/res/ColorStateList;",
                              {VmValue::Int(0x7f030001)})
                      .exception.IsValid());
            if (std::string_view(path) != "cycle")
                CHECK_THROWS(
                    static_cast<void>(f.context->theme_resources.Resource(0x7f030001, false, f.context->arsc)));
            CHECK(f.context->ui_drawables.size() == before_count);
            CHECK(f.context->ui_tree.PublishedProjection() == before);
            // Explicit @null masks an unsupported lower-priority reference.
            ogplay::loader::CompiledXmlNode xml;
            xml.attributes = {Attr(a, 1, 0)};
            CHECK(f.context->theme_resources.Styled({token}, 0, 0, &xml, std::array{a}, f.context->arsc)[0].type == 0);
        }
    }
}

namespace {
constexpr std::uint32_t text_theme = 0x7f020040, text_default = 0x7f020041, text_appearance = 0x7f020042;

VmObjectRef TextOwner(ContainerVm& f, const std::vector<std::byte>& pool = {}) {
    f.context->ui_test_accessibility_disabled = true;
    f.context->arsc = Table();
    const std::array styles{Style(text_theme, theme_id,
                                  {{0x01010064, 1, 0, {}},
                                   {0x01010065, 1, 0, {}},
                                   {0x01010034, 1, 0, {}},
                                   {0x01010084, 1, text_default, {}}}),
                            Style(text_default, 0, {{0x01010034, 1, text_appearance, {}}}),
                            Style(text_appearance, 0,
                                  {{0x01010098, 28, 0xff123456, {}},
                                   {0x01010095, 5, 0x1200, {}},
                                   {0x01010097, 16, 2, {}},
                                   {0x010103ac, 3, 0, std::string("serif")}})};
    f.context->arsc.entries.insert(f.context->arsc.entries.end(), styles.begin(), styles.end());
    // An application explicitly removes unused selection/hint/link presentation.
    // These are real @null overrides, never filtering in the adapter/provider.
    for (const auto id : {0x01010099U, 0x0101009aU, 0x0101009bU, 0x010102c5U, 0x010102c6U, 0x010102c7U, 0x01010314U,
                          0x01010315U, 0x0101035eU, 0x0101035fU, 0x01010374U})
        f.context->arsc.entries[3].bag.push_back({id, 1, 0, {}});
    f.context->arsc.global_string_pool =
        pool.empty() ? StringPool({"serif", "Plain text", "res/drawable/fixture.png"}, true) : pool;
    f.context->application_theme = text_theme;
    const auto owner = f.New("Landroid/app/Activity;");
    ogplay::test::AttachViewTestContext(f.vm, owner);
    f.On(owner, "setTheme", "(I)V", {VmValue::Int(text_theme)});
    return owner;
}

const ui::UiNode& TextState(ContainerVm& f, VmObjectRef view) {
    REQUIRE(f.context->object_to_ui_node.contains(view.Value()));
    return *f.context->ui_tree.Get(f.context->object_to_ui_node.at(view.Value()));
}

VmCallOutcome ConstructText(ContainerVm& f, VmObjectRef owner, VmObjectRef parser = VmObjectRef{},
                            int style = 0x01010084) {
    const auto type = f.linker.ResolveDescriptor("Landroid/widget/TextView;");
    f.Checked(f.vm.EnsureClassInitialized(type));
    const auto ctor =
        f.linker.FindDirectMethod(type, "<init>", "(Landroid/content/Context;Landroid/util/AttributeSet;I)V");
    REQUIRE(ctor);
    const auto view = f.vm.NewIntrinsicInstance("Landroid/widget/TextView;");
    return f.vm.Call(*ctor,
                     std::array{VmValue::Ref(view), VmValue::Ref(owner), VmValue::Ref(parser), VmValue::Int(style)});
}
}  // namespace

TEST_CASE("DVM-294 TextView constructors consume real theme and XML on both backends") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ContainerVm f(backend);
        const auto owner = TextOwner(f);
        const auto owner_root = f.vm.ProtectReferences(std::array{owner});
        const auto plain = f.New("Landroid/widget/TextView;", "(Landroid/content/Context;)V", {VmValue::Ref(owner)});
        const auto plain_root = f.vm.ProtectReferences(std::array{plain});
        const auto& base = TextState(f, plain);
        CHECK(base.text.empty());
        CHECK(base.text_size_px == 18);
        CHECK(base.text_color == 0x123456ff);
        CHECK(base.text_style == 2);
        CHECK(base.gravity == 0x33);
        CHECK(base.max_lines == 0x7fffffff);
        const auto face = f.context->view_adapter_references.at(plain.Value()).text_typeface;
        REQUIRE(face.IsValid());
        CHECK(f.On(face, "getStyle", "()I").AsInt() == 2);
        f.context->arsc.entries.push_back({});
        auto& string = f.context->arsc.entries.back();
        string.resource_id = 0x7f030040;
        string.type_name = "string";
        string.value_type = 3;
        string.string_value = "Plain text";
        string.value_data = 1;
        const auto parser = OpenAttributeXml(f, owner,
                                             AttributeXml({{{0x0101014f, 1, 0x7f030040},
                                                            {0x01010095, 5, 0x1a00},
                                                            {0x01010098, 28, 0xffabcdef},
                                                            {0x010100af, 16, 0x55},
                                                            {0x01010153, 16, 3},
                                                            {0x010100da, 18, 1},
                                                            {0x010100e5, 18, 1},
                                                            {0x010100e6, 18, 1}}}));
        const auto parser_root = f.vm.ProtectReferences(std::array{parser});
        const auto view = f.New("Landroid/widget/TextView;", "(Landroid/content/Context;Landroid/util/AttributeSet;)V",
                                {VmValue::Ref(owner), VmValue::Ref(parser)});
        const auto view_root = f.vm.ProtectReferences(std::array{view});
        const auto& xml = TextState(f, view);
        CHECK(xml.text == u"Plain text");
        CHECK(xml.text_size_px == 26);
        CHECK(xml.text_color == 0xabcdefff);
        CHECK(xml.gravity == 0x55);
        CHECK(xml.max_lines == 3);
        CHECK(xml.text_style == 2);
        CHECK(f.On(view, "isFocusable", "()Z").AsInt() == 1);
        CHECK(f.On(view, "isClickable", "()Z").AsInt() == 1);
        CHECK(f.On(view, "isLongClickable", "()Z").AsInt() == 1);
        for (const auto container :
             {"Landroid/widget/FrameLayout;", "Landroid/widget/LinearLayout;", "Landroid/widget/RelativeLayout;"}) {
            const auto group = f.New(container, "(Landroid/content/Context;)V", {VmValue::Ref(owner)});
            const auto group_root = f.vm.ProtectReferences(std::array{group});
            f.On(group, "addView", "(Landroid/view/View;II)V",
                 {VmValue::Ref(view), VmValue::Int(-2), VmValue::Int(-2)});
            f.context->content_view = group;
            ++f.context->ui_content_revision;
            CHECK(EnsureAndroidUiTraversal(f.vm, *f.context, AndroidUiTraversalReason::OwnerTick,
                                           AndroidUiTraversalPhase::ResolveOnly, ui::Size{96, 128}));
            const auto projected = f.context->ui_tree.PublishedProjection();
            REQUIRE(projected);
            const auto id = *FindViewUiNode(*f.context, view.Value());
            CHECK(projected->Get(id)->measured.width == f.On(view, "getMeasuredWidth", "()I").AsInt());
            REQUIRE(projected->Get(id)->text_line_plan);
            f.On(group, "removeView", "(Landroid/view/View;)V", {VmValue::Ref(view)});
        }
        f.context->content_view = VmObjectRef{};
        const auto trace = f.vm.Trace("", 10000);
        for (
            const auto signature :
            {"Landroid/view/View;-><init>(Landroid/content/Context;Landroid/util/AttributeSet;I)V",
             "Landroid/content/res/Resources$Theme;->obtainStyledAttributes(Landroid/util/AttributeSet;[III)Landroid/content/res/TypedArray;",
             "Landroid/content/res/TypedArray;->getDimensionPixelSize(II)I",
             "Landroid/content/res/TypedArray;->getText(I)Ljava/lang/CharSequence;"})
            CHECK(std::any_of(trace.begin(), trace.end(), [&](const auto& e) {
                return e.class_descriptor + "->" + e.method_name + e.method_descriptor == signature;
            }));
        static_cast<void>(f.vm.CollectGarbage("text-constructor-identity"));
        CHECK(f.On(face, "getStyle", "()I").AsInt() == 2);
        CHECK(f.ledger.Unimplemented().empty());
        const auto type = f.linker.ResolveDescriptor("Landroid/widget/TextView;");
        CHECK_FALSE(f.linker.FindFieldRecursive(type, "mTypeface", "Landroid/graphics/Typeface;"));
        for (const auto name : {"mText", "mLayout", "mEditor"}) {
            const auto& cls = f.linker.Class(type);
            const auto field = std::find_if(cls.own_instance_fields.begin(), cls.own_instance_fields.end(),
                                            [&](auto id) { return f.linker.Field(id).name == name; });
            REQUIRE(field != cls.own_instance_fields.end());
            CHECK(f.linker.Field(*field).is_unimplemented);
        }
        CHECK(f.OnOutcome(view, "getPaint", "()Landroid/text/TextPaint;").exception.IsValid());
        CHECK(f.model.StringValue(f.On(view, "getText", "()Ljava/lang/CharSequence;").ref) == u"Plain text");
        f.On(view, "onMeasure", "(II)V", {VmValue::Int(0), VmValue::Int(0)});
        CHECK(f.On(view, "getMeasuredWidth", "()I").AsInt() > 0);
    }
}

TEST_CASE("DVM-294 TextView style precedence defaults line policy and failure boundaries") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ContainerVm f(backend);
        const auto owner = TextOwner(f);
        const auto owner_root = f.vm.ProtectReferences(std::array{owner});
        f.context->arsc.entries.push_back(
            Style(0x7f020043, 0, {{0x01010095, 5, 0x1400, {}}, {0x01010098, 28, 0xff111111, {}}}));
        f.context->arsc.entries.push_back(Style(0x7f020044, 0, {{0x01010095, 5, 0x1600, {}}}));
        for (auto& entry : f.context->arsc.entries)
            if (entry.resource_id == text_theme) entry.bag.push_back({def, 1, 0x7f020043, {}});
        f.On(owner, "setTheme", "(I)V", {VmValue::Int(text_theme)});
        const auto parser = OpenAttributeXml(f, owner, AttributeXml({{{0, 1, 0x7f020044}}}));
        const auto parser_root = f.vm.ProtectReferences(std::array{parser});
        const auto custom =
            f.New("Landroid/widget/TextView;", "(Landroid/content/Context;Landroid/util/AttributeSet;I)V",
                  {VmValue::Ref(owner), VmValue::Ref(VmObjectRef{}), VmValue::Int(def)});
        CHECK(TextState(f, custom).text_size_px == 20);
        CHECK(TextState(f, custom).text_color == 0x111111ff);
        const auto explicit_style =
            f.New("Landroid/widget/TextView;", "(Landroid/content/Context;Landroid/util/AttributeSet;I)V",
                  {VmValue::Ref(owner), VmValue::Ref(parser), VmValue::Int(def)});
        CHECK(TextState(f, explicit_style).text_size_px == 22);
        CHECK(TextState(f, explicit_style).text_color == 0x111111ff);
        const auto styled =
            f.New("Landroid/widget/TextView;", "(Landroid/content/Context;Landroid/util/AttributeSet;I)V",
                  {VmValue::Ref(owner), VmValue::Ref(VmObjectRef{}), VmValue::Int(0)});
        CHECK(TextState(f, styled).text_size_px == 15);
        CHECK(TextState(f, styled).text_color == 0x000000ff);
        const auto single_parser =
            OpenAttributeXml(f, owner, AttributeXml({{{0x0101015d, 18, 1}, {0x010100ab, 16, 0}}}));
        const auto single_root = f.vm.ProtectReferences(std::array{single_parser});
        const auto single =
            f.New("Landroid/widget/TextView;", "(Landroid/content/Context;Landroid/util/AttributeSet;)V",
                  {VmValue::Ref(owner), VmValue::Ref(single_parser)});
        CHECK(TextState(f, single).max_lines == 1);
        CHECK(f.context->view_adapter_references.at(single.Value()).text_single_line);
        CHECK(f.ledger.Unimplemented().empty());
        // Each independent unsupported class is a requested, non-neutral value.
        for (const auto attr : std::vector<XmlValue>{{0x01010220, 16, 1},
                                                     {0x0101016b, 18, 1},
                                                     {0x010100ab, 16, 3},
                                                     {0x0101038c, 18, 1},
                                                     {0x01010161, 28, 0xff000001},
                                                     {0x010100b0, 16, 1},
                                                     {0x0101021d, 16, 2},
                                                     {0x01010154, 16, 2},
                                                     {0x01010157, 16, 2},
                                                     {0x01010217, 5, 0x100},
                                                     {0x01010218, 4, 0x40000000},
                                                     {0x01010171, 5, 0x100},
                                                     {0x01010392, 1, 0x7f030099},
                                                     {0x01010150, 16, 2},
                                                     {0x0101015d, 18, 1},
                                                     {0x010100af, 16, 0x800003},
                                                     {0x010105ff, 16, 1}}) {
            INFO(attr.id);
            const auto p = OpenAttributeXml(f, owner, AttributeXml({{attr}}));
            const auto root = f.vm.ProtectReferences(std::array{p});
            CHECK(ConstructText(f, owner, p).exception.IsValid());
        }
        CHECK_FALSE(f.ledger.Unimplemented().empty());
        CHECK(f.OnOutcome(owner, "setTheme", "(I)V", {VmValue::Int(0x0103ffff)}).exception.IsValid());
        ContainerVm spans(backend);
        auto styled_pool = StringPool({"serif", "Plain text", "res/drawable/fixture.png"}, true);
        styled_pool.insert(styled_pool.begin() + 40, 12, std::byte{0});
        Set32(styled_pool, 12, 3);
        Set32(styled_pool, 20, 52);
        Set32(styled_pool, 24, static_cast<std::uint32_t>(styled_pool.size()));
        Set32(styled_pool, 40, 0xffffffff);
        Set32(styled_pool, 44, 0);
        Set32(styled_pool, 48, 0xffffffff);
        for (const auto word : {0U, 0U, 1U, 0xffffffffU, 0xffffffffU, 0xffffffffU})
            Append32(styled_pool, word);
        Set32(styled_pool, 4, static_cast<std::uint32_t>(styled_pool.size()));
        CHECK(ogplay::loader::ParseCompiledStringPool(styled_pool)->HasStringStyles());
        const auto span_owner = TextOwner(spans, styled_pool);
        const auto span_failure = ConstructText(spans, span_owner);
        CHECK(span_failure.exception.IsValid());
        CHECK(span_failure.exception_message.find("spans") != std::string::npos);
    }
}

TEST_CASE("DVM-294 absolute bitmap compounds retain identities and inflater failure is atomic") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ContainerVm f(backend);
        const auto owner = TextOwner(f);
        const auto owner_root = f.vm.ProtectReferences(std::array{owner});
        constexpr std::uint32_t image = 0x7f030050;
        f.context->arsc.entries.push_back({.resource_id = image,
                                           .type_name = "drawable",
                                           .entry_name = "compound",
                                           .string_value = "res/drawable/fixture.png",
                                           .value_type = 3,
                                           .value_data = 2,
                                           .density = 160});
        const auto parser = OpenAttributeXml(
            f, owner,
            AttributeXml(
                {{{0x0101016f, 1, image}, {0x0101016d, 1, image}, {0x01010170, 1, image}, {0x0101016e, 1, image}}}));
        const auto parser_root = f.vm.ProtectReferences(std::array{parser});
        // Parser owns its immutable document; the APK now supplies the bitmap.
        f.context->apk_bytes = ogplay::test::BitmapDecodeApk();
        f.context->archive = ogplay::loader::ParseApkArchive(f.context->apk_bytes);
        const auto view = f.New("Landroid/widget/TextView;", "(Landroid/content/Context;Landroid/util/AttributeSet;)V",
                                {VmValue::Ref(owner), VmValue::Ref(parser)});
        {
            const auto root = f.vm.ProtectReferences(std::array{view});
            const auto& refs = f.context->view_adapter_references.at(view.Value()).text_compounds;
            for (std::size_t i = 0; i < 4; ++i) {
                CHECK(TextState(f, view).compound_drawables[i].resource_id == image);
                CHECK(TextState(f, view).compound_drawables[i].width == 2);
                CHECK(TextState(f, view).compound_drawables[i].height == 1);
                REQUIRE(refs[i].IsValid());
                const auto& pixels = f.context->ui_drawables.at(refs[i].Value()).leaf_pixels;
                REQUIRE(pixels);
                CHECK(pixels->rgba8 == std::vector<std::uint8_t>{255, 0, 0, 255, 0, 0, 255, 255});
            }
            static_cast<void>(f.vm.CollectGarbage("text-compound-rooted"));
            for (const auto ref : refs)
                CHECK(f.context->ui_drawables.contains(ref.Value()));
            f.On(view, "setCompoundDrawablesWithIntrinsicBounds", "(IIII)V",
                 {VmValue::Int(image), VmValue::Int(0), VmValue::Int(image), VmValue::Int(0)});
            CHECK(f.On(view, "getCompoundPaddingLeft", "()I").AsInt() == 2);
            CHECK(f.On(view, "getCompoundPaddingTop", "()I").AsInt() == 0);
            f.On(view, "onMeasure", "(II)V", {VmValue::Int(0), VmValue::Int(0)});
            CHECK(f.On(view, "getMeasuredWidth", "()I").AsInt() == 4);
            CHECK(f.ledger.Unimplemented().empty());
        }
        static_cast<void>(f.vm.CollectGarbage("text-compound-unrooted"));
        CHECK_FALSE(f.context->view_adapter_references.contains(view.Value()));
        // Inflate into an unattached tree using the same host text input.
        f.context->activity = owner;
        // The ARSC serializer used by this fixture has no styled-string bag pool.
        for (auto& entry : f.context->arsc.entries)
            if (entry.resource_id == text_appearance)
                entry.bag = {{0x01010095, 5, 0x1200, {}}, {0x01010098, 28, 0xff123456, {}}, {0x01010096, 16, 2, {}}};
        InstallInflateLayouts(f, {{"TextView", InflateSize(20, 10), {}},
                                  {"FrameLayout",
                                   InflateSize(40, 20),
                                   {{"View", InflateSize(10, 10), {}}, {"TextView", InflateSize(20, 10), {}}}}});
        const auto inflater = f.On(owner, "getLayoutInflater", "()Landroid/view/LayoutInflater;").ref;
        const auto inflater_root = f.vm.ProtectReferences(std::array{inflater});
        const auto tree = f.On(inflater, "inflate", "(ILandroid/view/ViewGroup;Z)Landroid/view/View;",
                               {VmValue::Int(0x7f030001), VmValue::Ref(VmObjectRef{}), VmValue::Int(0)})
                              .ref;
        const auto tree_root = f.vm.ProtectReferences(std::array{tree});
        CHECK(f.linker.Class(f.model.ObjectClass(tree)).descriptor == "Landroid/widget/TextView;");
        CHECK(TextState(f, tree).text_size_px == 18);
        CHECK(f.ledger.Unimplemented().empty());
        const auto old_root =
            f.New("Landroid/widget/FrameLayout;", "(Landroid/content/Context;)V", {VmValue::Ref(owner)});
        f.On(owner, "setContentView", "(Landroid/view/View;)V", {VmValue::Ref(old_root)});
        const auto old = f.context->content_view;
        REQUIRE(old.IsValid());
        for (auto& entry : f.context->arsc.entries)
            if (entry.resource_id == text_default) entry.bag.push_back({0x01010220, 16, 1, {}});
        const auto failed = f.OnOutcome(owner, "setContentView", "(I)V", {VmValue::Int(0x7f030002)});
        CHECK(failed.exception.IsValid());
        CHECK(f.context->content_view == old);
    }
}

TEST_CASE("DVM-296 ImageView XML APK src matches code Java state and measurement") {
    for (auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ContainerVm f(backend);
        const auto owner = TextOwner(f);
        const auto owner_root = f.vm.ProtectReferences(std::array{owner});
        constexpr std::uint32_t resource = 0x7f030060;
        f.context->arsc.entries.push_back({.resource_id = resource,
                                           .type_name = "drawable",
                                           .entry_name = "image",
                                           .string_value = "res/drawable/fixture.png",
                                           .value_type = 3,
                                           .value_data = 2,
                                           .density = 160});
        const auto parser = OpenAttributeXml(
            f, owner, AttributeXml({{{0x01010119, 1, resource}, {0x0101011d, 16, 6}, {0x0101042f, 16, 96}}}));
        const auto parser_root = f.vm.ProtectReferences(std::array{parser});
        f.context->apk_bytes = ogplay::test::BitmapDecodeApk();
        f.context->archive = ogplay::loader::ParseApkArchive(f.context->apk_bytes);
        const auto xml = f.New("Landroid/widget/ImageView;", "(Landroid/content/Context;Landroid/util/AttributeSet;)V",
                               {VmValue::Ref(owner), VmValue::Ref(parser)});
        const auto code = f.New("Landroid/widget/ImageView;", "(Landroid/content/Context;)V", {VmValue::Ref(owner)});
        const auto roots = f.vm.ProtectReferences(std::array{xml, code});
        f.On(code, "setImageResource", "(I)V", {VmValue::Int(resource)});
        const auto st = f.On(xml, "getScaleType", "()Landroid/widget/ImageView$ScaleType;").ref;
        f.On(code, "setScaleType", "(Landroid/widget/ImageView$ScaleType;)V", {VmValue::Ref(st)});
        f.On(code, "setImageAlpha", "(I)V", {VmValue::Int(96)});
        for (const auto view : {xml, code}) {
            const auto d = f.On(view, "getDrawable", "()Landroid/graphics/drawable/Drawable;").ref;
            CHECK(f.linker.Class(f.model.ObjectClass(d)).descriptor == "Landroid/graphics/drawable/BitmapDrawable;");
            CHECK(f.On(d, "getIntrinsicWidth", "()I").AsInt() == 2);
            CHECK(f.On(d, "getIntrinsicHeight", "()I").AsInt() == 1);
            CHECK(JavaField(f, view, "mDrawable", "Landroid/graphics/drawable/Drawable;").bits == d.Value());
            CHECK(f.On(view, "getImageAlpha", "()I").AsInt() == 96);
            CHECK(f.On(view, "getScaleType", "()Landroid/widget/ImageView$ScaleType;").ref == st);
            const auto matrix = VmObjectRef{JavaField(f, view, "mMatrix", "Landroid/graphics/Matrix;").bits};
            CHECK(f.On(matrix, "isIdentity", "()Z").AsInt() == 1);
            const auto& state = *f.context->ui_tree.Get(*FindViewUiNode(*f.context, view.Value()));
            REQUIRE(state.image_bitmap);
            CHECK(state.image_bitmap->rgba8 == std::vector<std::uint8_t>{255, 0, 0, 255, 0, 0, 255, 255});
        }
        for (auto mode : {0U, 0x40000000U, 0x80000000U}) {
            for (auto view : {xml, code})
                f.On(view, "measure", "(II)V",
                     {VmValue::Int(static_cast<int>(mode | 10)), VmValue::Int(static_cast<int>(mode | 8))});
            CHECK(f.On(xml, "getMeasuredWidth", "()I").AsInt() == f.On(code, "getMeasuredWidth", "()I").AsInt());
            CHECK(f.On(xml, "getMeasuredHeight", "()I").AsInt() == f.On(code, "getMeasuredHeight", "()I").AsInt());
        }
        CHECK(f.ledger.Unimplemented().empty());
        // Requests rejected in both XML and the public API; neutral defaults are source-proven.
        for (const auto attr : std::array<XmlValue, 10>{{{0x0101011d, 16, 0},
                                                         {0x0101011d, 16, 2},
                                                         {0x0101011d, 16, 4},
                                                         {0x0101011e, 18, 1},
                                                         {0x0101011f, 5, 0x1000},
                                                         {0x01010120, 5, 0x1000},
                                                         {0x01010121, 28, 0xffff0000},
                                                         {0x0101031c, 5, 0x1000},
                                                         {0x01010122, 18, 1},
                                                         {0x01010123, 18, 1}}}) {
            const auto p = OpenAttributeXml(f, owner, AttributeXml({{attr}}));
            const auto object = f.vm.NewIntrinsicInstance("Landroid/widget/ImageView;");
            const auto ctor =
                f.linker.FindDirectMethod(f.linker.ResolveDescriptor("Landroid/widget/ImageView;"), "<init>",
                                          "(Landroid/content/Context;Landroid/util/AttributeSet;)V");
            const auto outcome =
                f.vm.Call(*ctor, std::array{VmValue::Ref(object), VmValue::Ref(owner), VmValue::Ref(p)});
            CHECK(outcome.exception.IsValid());
        }
    }
}

TEST_CASE("DVM-296 ImageButton XML and code honor real imageButtonStyle and explicit precedence") {
    for (auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ContainerVm f(backend);
        const auto owner = TextOwner(f);
        const auto owner_root = f.vm.ProtectReferences(std::array{owner});
        f.context->arsc.entries.push_back(Style(
            0x7f020071, 0, {{0x01010119, 28, 0xff884422, {}}, {0x0101011d, 16, 5, {}}, {0x0101042f, 16, 64, {}}}));
        f.context->arsc.entries.push_back(Style(0x7f020072, 0, {{0x01010072, 1, 0x7f020071, {}}}));
        const auto theme = f.On(owner, "getTheme", "()Landroid/content/res/Resources$Theme;").ref;
        f.On(theme, "applyStyle", "(IZ)V", {VmValue::Int(0x7f020072), VmValue::Int(1)});
        const auto parser = OpenAttributeXml(
            f, owner, AttributeXml({{{0x01010119, 28, 0xff223344}, {0x0101011d, 16, 1}, {0x0101042f, 16, 96}}}));
        const auto parser_root = f.vm.ProtectReferences(std::array{parser});
        const auto xml =
            f.New("Landroid/widget/ImageButton;", "(Landroid/content/Context;Landroid/util/AttributeSet;)V",
                  {VmValue::Ref(owner), VmValue::Ref(parser)});
        const auto code = f.New("Landroid/widget/ImageButton;", "(Landroid/content/Context;)V", {VmValue::Ref(owner)});
        const auto roots = f.vm.ProtectReferences(std::array{xml, code});
        CHECK(f.On(code, "getImageAlpha", "()I").AsInt() == 64);
        const auto original = f.On(code, "getDrawable", "()Landroid/graphics/drawable/Drawable;").ref;
        CHECK(f.On(original, "getColor", "()I").cat1 == 0xff884422);
        const auto desired = f.On(xml, "getDrawable", "()Landroid/graphics/drawable/Drawable;").ref;
        CHECK(f.On(desired, "getColor", "()I").cat1 == 0xff223344);
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
        f.context->ui_test_accessibility_disabled = true;
        const auto plain_owner = f.New("Landroid/app/Activity;");
        ogplay::test::AttachViewTestContext(f.vm, plain_owner);
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
             {std::array{0x01030005, 0x01080554, 0x01080555}, std::array{0x0103006b, 0x01080552, 0x01080552},
              std::array{0x0103006e, 0x01080553, 0x01080553}}) {
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
        CHECK(f.context->ui_drawables.empty());
        CHECK(f.ledger.Unimplemented().empty());
        f.context->arsc = Table(
            {Style(0x7f020004, 0x01030005, {{0x01010064, 1, 0, {}}, {0x01010065, 1, 0, {}}, {0x010100d4, 1, 0, {}}}),
             Style(0x7f020005, 0, {{0x010100d4, 1, 0x01080004, {}}})});
        const auto owner = f.New("Landroid/app/Activity;");
        ogplay::test::AttachViewTestContext(f.vm, owner);
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
        const auto array =
            f.On(owner, "obtainStyledAttributes", "([I)Landroid/content/res/TypedArray;", {VmValue::Ref(attrs)}).ref;
        const auto roots = f.vm.ProtectReferences(std::array{owner, view, theme, attrs, array});
        CHECK(f.On(array, "getResourceId", "(II)I", {VmValue::Int(0), VmValue::Int(0)}).AsInt() == 0x01080004);
        CHECK(f.context->ui_drawables.empty());
        CHECK(f.ledger.Unimplemented().empty());
        const auto failed =
            f.OnOutcome(array, "getDrawable", "(I)Landroid/graphics/drawable/Drawable;", {VmValue::Int(0)});
        REQUIRE(failed.exception.IsValid());
        CHECK_FALSE(failed.exception_message.empty());
    }
}

TEST_CASE("DVM-316 RelativeLayout inflation reports reflection target and constructor first stack") {
    for (auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ContainerVm f(backend);
        f.context->ui_test_accessibility_disabled = true;
        // The observed root now passes projection. This diagnostic deliberately
        // supplies an unknown framework scrollbar ID through the theme instead.
        f.context->arsc = Table({Style(0x7f020004, 0x01030007, {{0x01010064, 1, 0x0108ffff, {}}})});
        const auto owner = f.New("Landroid/app/Activity;");
        f.context->activity = owner;
        ogplay::test::AttachViewTestContext(f.vm, owner);
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
        CHECK(message == "unsupported projected resource reference " + std::to_string(0x0108ffff));
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
        const auto failure = PumpJavaThreads(f.vm, *f.context);
        REQUIRE(failure);
        INFO(*failure);
        CHECK(failure->find("Caused by: Ljava/lang/UnsupportedOperationException;: " + message) != std::string::npos);
        CHECK(failure->find("Caused by:   at Landroid/content/res/Resources$Theme;.obtainStyledAttributes") !=
              std::string::npos);
    }
}
