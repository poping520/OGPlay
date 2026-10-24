#include "container_traversal_fixture.h"
#include "file_vfs_fixture.h"
#include <bit>

using ogplay::test::container_traversal::ContainerVm;

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

struct Attribute final {
    std::uint32_t name{};
    std::uint32_t raw_value{0xffffffffU};
    std::uint8_t type{};
    std::uint32_t data{};
    std::uint32_t namespace_index{0xffffffffU};
};

std::vector<std::byte> StartElement(const std::uint32_t name, const std::vector<Attribute>& attributes) {
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

void Append(std::vector<std::byte>& destination, const std::vector<std::byte>& source) {
    destination.insert(destination.end(), source.begin(), source.end());
}

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
                                  {9, 0xffffffffU, 0x11, 0xff00ff00U},
                                  {8, 0xffffffffU, 0x04, 0x3fc00000U}});
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

struct XmlVm : ContainerVm {
    using ContainerVm::ContainerVm;

    VmObjectRef Bytes(const std::vector<std::byte>& bytes) {
        auto result = model.NewPrimitiveArray(linker.ResolveDescriptor("[B"), JniPrimitiveKind::byte,
                                              static_cast<std::int32_t>(bytes.size()));
        model.FillPrimitiveData(result, bytes);
        return result;
    }

    std::string Text(VmValue value) { return value.ref.IsValid() ? vm.StringUtf8(value.ref) : "<null>"; }

    std::uint32_t Field(VmObjectRef obj, const char* name, const char* type = "I") {
        auto f = linker.FindFieldRecursive(model.ObjectClass(obj), name, type);
        REQUIRE(f);
        return model.InstanceSlots(obj)[linker.Field(*f).slot].bits;
    }

    VmCallOutcome NativeOutcome(const char* owner, const char* name, const char* desc, std::vector<VmValue> args) {
        auto m = linker.FindDirectMethod(linker.ResolveDescriptor(owner), name, desc);
        REQUIRE(m);
        return vm.Call(*m, args);
    }
};

constexpr const char* xml = "Landroid/content/res/XmlBlock;";
constexpr const char* pool = "Landroid/content/res/StringBlock;";
}  // namespace

TEST_CASE("DVM-287 ROM XmlBlock Parser event and typed attribute semantics") {
    for (auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        XmlVm f(backend);
        auto bytes = f.Bytes(CompiledFactsFixture());
        auto block = f.New(xml, "([B)V", {VmValue::Ref(bytes)});
        auto p = f.On(block, "newParser", "()Landroid/content/res/XmlResourceParser;").ref;
        auto other = f.On(block, "newParser", "()Landroid/content/res/XmlResourceParser;").ref;
        auto roots = f.vm.ProtectReferences(std::array{block, p, other});
        CHECK(f.On(p, "getEventType", "()I").AsInt() == 0);
        CHECK(f.On(p, "getDepth", "()I").AsInt() == 0);
        CHECK(f.On(p, "getLineNumber", "()I").AsInt() == -1);
        CHECK(f.On(p, "next", "()I").AsInt() == 0);
        CHECK(f.On(p, "next", "()I").AsInt() == 2);
        CHECK(f.Text(f.On(p, "getName", "()Ljava/lang/String;")) == "root");
        CHECK(f.Text(f.On(p, "getNamespace", "()Ljava/lang/String;")) == "urn:a");
        CHECK(f.On(p, "getDepth", "()I").AsInt() == 1);
        CHECK(f.On(p, "getLineNumber", "()I").AsInt() == 41);
        CHECK(f.On(p, "getAttributeCount", "()I").AsInt() == 10);
        for (int i = 0; i < 2; ++i) {
            CHECK(f.Text(f.On(p, "getAttributeName", "(I)Ljava/lang/String;", {VmValue::Int(i)})) == "same");
            CHECK(f.Text(f.On(p, "getAttributeNamespace", "(I)Ljava/lang/String;", {VmValue::Int(i)})) ==
                  (i == 0 ? "urn:a" : "urn:b"));
            CHECK(f.On(p, "getAttributeNameResource", "(I)I", {VmValue::Int(i)}).AsInt() == 0x01010002);
        }
        auto ns = f.vm.NewStringUtf8("urn:b"), name = f.vm.NewStringUtf8("same");
        CHECK(f.Text(f.On(p, "getAttributeValue", "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;",
                          {VmValue::Ref(ns), VmValue::Ref(name)})) == "raw");
        CHECK(f.Text(f.On(p, "getAttributeValue", "(I)Ljava/lang/String;", {VmValue::Int(0)})) == "@2130772259");
        CHECK(f.Text(f.On(p, "getAttributeValue", "(I)Ljava/lang/String;", {VmValue::Int(5)})) ==
              "<null>");  // TYPE_STRING without raw: coerceToString returns null
        CHECK(f.Text(f.On(p, "getAttributeValue", "(I)Ljava/lang/String;", {VmValue::Int(6)})) == "@0");
        CHECK(f.Text(f.On(p, "getAttributeValue", "(I)Ljava/lang/String;", {VmValue::Int(7)})) == "");
        CHECK(f.On(p, "getAttributeIntValue", "(II)I", {VmValue::Int(3), VmValue::Int(77)}).AsInt() == -2);
        CHECK(f.On(p, "getAttributeIntValue", "(II)I", {VmValue::Int(0), VmValue::Int(77)}).AsInt() == 77);
        CHECK(f.On(p, "getAttributeResourceValue", "(II)I", {VmValue::Int(0), VmValue::Int(77)}).AsInt() == 0x7f010123);
        CHECK(f.On(p, "getAttributeResourceValue", "(II)I", {VmValue::Int(6), VmValue::Int(77)}).AsInt() == 0);
        CHECK(f.On(p, "getAttributeBooleanValue", "(IZ)Z", {VmValue::Int(4), VmValue::Int(0)}).AsInt() == 1);
        CHECK(f.On(p, "getAttributeBooleanValue", "(IZ)Z", {VmValue::Int(0), VmValue::Int(0)}).AsInt() == 0);
        CHECK(f.On(p, "getAttributeFloatValue", "(IF)F", {VmValue::Int(9), VmValue::Float(99.0F)}).AsFloat() == 1.5F);
        CHECK(f.OnOutcome(p, "getAttributeFloatValue", "(IF)F", {VmValue::Int(0), VmValue::Float(99.0F)})
                  .exception.IsValid());
        CHECK(f.OnOutcome(p, "getAttributeName", "(I)Ljava/lang/String;", {VmValue::Int(10)}).exception.IsValid());
        CHECK(f.OnOutcome(p, "getAttributeNamespace", "(I)Ljava/lang/String;", {VmValue::Int(-1)}).exception.IsValid());
        auto options = f.model.NewObjectArray(f.linker.ResolveDescriptor("[Ljava/lang/String;"),
                                              f.linker.ResolveDescriptor("Ljava/lang/String;"), 2);
        f.model.SetObjectElement(options, 0, f.vm.NewStringUtf8("other"));
        f.model.SetObjectElement(options, 1, f.vm.NewStringUtf8(""));
        CHECK(f.On(p, "getAttributeListValue", "(I[Ljava/lang/String;I)I",
                   {VmValue::Int(5), VmValue::Ref(options), VmValue::Int(77)})
                  .AsInt() == 1);
        CHECK(f.On(p, "getAttributeListValue", "(I[Ljava/lang/String;I)I",
                   {VmValue::Int(3), VmValue::Ref(options), VmValue::Int(77)})
                  .AsInt() == -2);
        CHECK(f.Text(f.On(p, "getIdAttribute", "()Ljava/lang/String;")) == "raw");
        CHECK(f.Text(f.On(p, "getClassAttribute", "()Ljava/lang/String;")) == "<null>");
        CHECK(f.On(p, "getStyleAttribute", "()I").AsInt() == 0);
        CHECK(f.On(other, "getDepth", "()I").AsInt() == 0);
        CHECK(f.On(p, "next", "()I").AsInt() == 2);
        CHECK(f.On(p, "getDepth", "()I").AsInt() == 2);
        CHECK(f.On(p, "next", "()I").AsInt() == 4);
        CHECK(f.Text(f.On(p, "getText", "()Ljava/lang/String;")) == "raw");
        CHECK(f.Text(f.On(p, "getName", "()Ljava/lang/String;")) == "<null>");
        CHECK(f.On(p, "getLineNumber", "()I").AsInt() == 43);
        CHECK(f.On(p, "getDepth", "()I").AsInt() == 2);
        CHECK(f.On(p, "next", "()I").AsInt() == 3);
        CHECK(f.On(p, "getDepth", "()I").AsInt() == 2);
        CHECK(f.On(p, "next", "()I").AsInt() == 3);
        CHECK(f.On(p, "getDepth", "()I").AsInt() == 1);
        CHECK(f.On(p, "getLineNumber", "()I").AsInt() == 49);
        f.On(block, "close", "()V");
        CHECK(f.On(p, "next", "()I").AsInt() == 1);
        CHECK(f.On(p, "getDepth", "()I").AsInt() == 0);
        f.On(other, "close", "()V");
        f.On(other, "close", "()V");
        CHECK(f.context->xml_cursors.LiveDocuments() == 0);
        CHECK(f.context->xml_cursors.LiveCursors() == 0);
        CHECK(f.context->xml_cursors.LiveStringPools() == 0);
        for (int style_type : {1, 2, 16}) {
            auto tag = StartElement(0, {{1, 4, 1, 0x7f010123U},
                                        {2, 5, 3, 5},
                                        {3, 0xffffffffU, static_cast<std::uint8_t>(style_type), 0x01010099U}});
            tag[30] = std::byte{1};
            tag[32] = std::byte{2};
            tag[34] = std::byte{3};
            auto document = XmlDocument(
                {StringPool({"r", "id", "class", "style", "raw-id", "raw-class"}, true), tag, EndElement(0)});
            const auto size = static_cast<int>(document.size());
            document.insert(document.begin(), 3, std::byte{0});
            document.push_back(std::byte{0});
            auto b = f.New(xml, "([BII)V", {VmValue::Ref(f.Bytes(document)), VmValue::Int(3), VmValue::Int(size)});
            auto parser = f.On(b, "newParser", "()Landroid/content/res/XmlResourceParser;").ref;
            const auto keep = f.vm.ProtectReferences(std::array{b, parser});
            f.On(parser, "next", "()I");
            f.On(parser, "next", "()I");
            CHECK(f.Text(f.On(parser, "getIdAttribute", "()Ljava/lang/String;")) == "raw-id");
            CHECK(f.Text(f.On(parser, "getClassAttribute", "()Ljava/lang/String;")) == "raw-class");
            CHECK(f.On(parser, "getStyleAttribute", "()I").AsInt() == (style_type == 16 ? 0 : 0x01010099));
            CHECK(f.On(parser, "getIdAttributeResourceValue", "(I)I", {VmValue::Int(77)}).AsInt() == 0x7f010123);
            f.On(b, "close", "()V");
            f.On(parser, "close", "()V");
        }
        CHECK(f.ledger.Unimplemented().empty());
        const auto parser_type = f.model.ObjectClass(p);
        for (const auto& [method_name, descriptor] : std::array<std::pair<const char*, const char*>, 6>{
                 {{"next", "()I"},
                  {"getDepth", "()I"},
                  {"getAttributeIntValue", "(II)I"},
                  {"getAttributeValue", "(I)Ljava/lang/String;"},
                  {"close", "()V"},
                  {"getAttributeListValue", "(I[Ljava/lang/String;I)I"}}}) {
            const auto slot = f.linker.FindVtableIndex(parser_type, method_name, descriptor);
            REQUIRE(slot);
            CHECK(f.linker.Method(f.linker.Class(parser_type).vtable[*slot]).code.has_value());
        }
        auto trace = f.vm.Trace("", 10000);
        std::ofstream output(std::string(OGPLAY_SOURCE_DIR) + "/.local/analysis/vd-09a2/parser-" +
                             (backend == InterpreterBackend::threaded ? "threaded" : "switch") + ".trace.json");
        output << RenderDexVmTraceJson(trace);

        CHECK(std::any_of(trace.begin(), trace.end(), [](const auto& e) {
            return e.kind == DexVmTraceKind::method_enter &&
                   e.class_descriptor == "Landroid/content/res/XmlBlock$Parser;" &&
                   e.method_name == "getAttributeIntValue";
        }));
    }
}

TEST_CASE("DVM-287 XML lifetime and plain StringBlock reject invalid tokens and spans") {
    for (auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        XmlVm f(backend);
        auto block = f.New(xml, "([B)V", {VmValue::Ref(f.Bytes(CompiledFactsFixture()))});
        auto p = f.On(block, "newParser", "()Landroid/content/res/XmlResourceParser;").ref;
        auto block_root = f.vm.ProtectReferences(std::array{block, p});
        auto doc = f.Field(block, "mNative"), token = f.Field(p, "mParseState");
        auto sb = f.Field(block, "mStrings", "Landroid/content/res/StringBlock;");
        auto pool_token = f.Field(VmObjectRef(sb), "mNative");
        f.On(p, "next", "()I");
        f.On(p, "next", "()I");
        f.On(p, "close", "()V");
        f.On(p, "close", "()V");
        CHECK(f.OnOutcome(p, "getName", "()Ljava/lang/String;").exception.IsValid());
        CHECK(
            f.NativeOutcome(xml, "nativeGetName", "(I)I", {VmValue::Int(static_cast<int>(token))}).exception.IsValid());
        CHECK(f.NativeOutcome(xml, "nativeDestroyParseState", "(I)V", {VmValue::Int(static_cast<int>(token))})
                  .exception.IsValid());
        XmlVm other_context(backend);
        CHECK(other_context.NativeOutcome(xml, "nativeGetName", "(I)I", {VmValue::Int(static_cast<int>(token))})
                  .exception.IsValid());
        CHECK(other_context.NativeOutcome(xml, "nativeCreateParseState", "(I)I", {VmValue::Int(static_cast<int>(doc))})
                  .exception.IsValid());
        CHECK(other_context.NativeOutcome(pool, "nativeGetSize", "(I)I", {VmValue::Int(static_cast<int>(pool_token))})
                  .exception.IsValid());
        XmlCursorRegistry foreign;
        CHECK_THROWS_AS(static_cast<void>(foreign.Document(XmlDocumentToken{doc})), std::invalid_argument);
        CHECK_THROWS_AS(static_cast<void>(foreign.StringPool(XmlStringPoolToken{pool_token})), std::invalid_argument);
        CHECK_THROWS_AS(static_cast<void>(foreign.Current(XmlCursorToken{token})), std::invalid_argument);
        auto q = f.On(block, "newParser", "()Landroid/content/res/XmlResourceParser;").ref;
        CHECK(f.Field(q, "mParseState") != token);
        f.context->xml_cursors.RetireOwner(XmlDocumentToken{doc});
        CHECK(f.OnOutcome(q, "getName", "()Ljava/lang/String;").exception.IsValid());
        CHECK(f.NativeOutcome(pool, "nativeGetSize", "(I)I", {VmValue::Int(static_cast<int>(pool_token))})
                  .exception.IsValid());
        CHECK(f.NativeOutcome(xml, "nativeCreateParseState", "(I)I", {VmValue::Int(static_cast<int>(doc))})
                  .exception.IsValid());
        CHECK(f.NativeOutcome(xml, "nativeCreate", "([BII)I",
                              {VmValue::Ref(f.Bytes(CompiledFactsFixture())), VmValue::Int(-1), VmValue::Int(1)})
                  .exception.IsValid());
        auto poolbytes = StringPool({"hello", ""}, true);
        auto strings = f.New(pool, "([BZ)V", {VmValue::Ref(f.Bytes(poolbytes)), VmValue::Int(0)});
        const auto strings_root = f.vm.ProtectReferences(std::array{strings});
        auto st = f.Field(strings, "mNative");
        CHECK(f.Static(pool, "nativeGetSize", "(I)I", {VmValue::Int(static_cast<int>(st))}).AsInt() == 2);
        const auto cached = f.On(strings, "get", "(I)Ljava/lang/CharSequence;", {VmValue::Int(0)}).ref;
        CHECK(f.vm.StringUtf8(cached) == "hello");
        CHECK(f.On(strings, "get", "(I)Ljava/lang/CharSequence;", {VmValue::Int(0)}).ref == cached);
        auto large =
            f.New(pool, "([BZ)V",
                  {VmValue::Ref(f.Bytes(StringPool(std::vector<std::string>(251, "x"), true))), VmValue::Int(1)});
        const auto sparse_cached = f.On(large, "get", "(I)Ljava/lang/CharSequence;", {VmValue::Int(250)}).ref;
        CHECK(f.On(large, "get", "(I)Ljava/lang/CharSequence;", {VmValue::Int(250)}).ref == sparse_cached);
        CHECK(f.vm.StringUtf8(sparse_cached) == "x");
        CHECK(f.Text(f.On(strings, "get", "(I)Ljava/lang/CharSequence;", {VmValue::Int(1)})) == "");
        CHECK(f.OnOutcome(strings, "get", "(I)Ljava/lang/CharSequence;", {VmValue::Int(-1)}).exception.IsValid());
        CHECK(f.OnOutcome(strings, "get", "(I)Ljava/lang/CharSequence;", {VmValue::Int(2)}).exception.IsValid());
        // UTF16 lone surrogate is preserved in the Java String, not replaced.
        auto wide = StringPool({"x"}, false);
        wide[34] = std::byte{0x00};
        wide[35] = std::byte{0xd8};
        auto w = f.New(pool, "([BZ)V", {VmValue::Ref(f.Bytes(wide)), VmValue::Int(0)});
        CHECK(f.model.StringValue(f.On(w, "get", "(I)Ljava/lang/CharSequence;", {VmValue::Int(0)}).ref) ==
              std::u16string{static_cast<char16_t>(0xd800)});
        // WTF-8 encoding of the same lone surrogate.
        auto utf8 = StringPool({"xxx"}, true);
        utf8[32] = std::byte{1};
        utf8[34] = std::byte{0xed};
        utf8[35] = std::byte{0xa0};
        utf8[36] = std::byte{0x80};
        auto u = f.New(pool, "([BZ)V", {VmValue::Ref(f.Bytes(utf8)), VmValue::Int(0)});
        CHECK(f.model.StringValue(f.On(u, "get", "(I)Ljava/lang/CharSequence;", {VmValue::Int(0)}).ref) ==
              std::u16string{static_cast<char16_t>(0xd800)});
        CHECK(f.ledger.Unimplemented().empty());
        CHECK(f.NativeOutcome(pool, "nativeGetStyle", "(II)[I", {VmValue::Int(static_cast<int>(st)), VmValue::Int(0)})
                  .exception.IsValid());
        CHECK_FALSE(f.ledger.Unimplemented().empty());
        f.Static(pool, "nativeDestroy", "(I)V", {VmValue::Int(static_cast<int>(st))});
        CHECK(f.OnOutcome(strings, "get", "(I)Ljava/lang/CharSequence;", {VmValue::Int(0)}).exception.IsValid());
        CHECK(f.NativeOutcome(pool, "nativeDestroy", "(I)V", {VmValue::Int(static_cast<int>(st))}).exception.IsValid());
        auto styled = StringPool({"hello"}, true);
        Set32(styled, 12, 1);
        styled.insert(styled.begin() + 32, 4, std::byte{0});
        Set32(styled, 20, 36);
        Set32(styled, 24, static_cast<std::uint32_t>(styled.size()));
        Append32(styled, 0);
        Append32(styled, 0);
        Append32(styled, 4);
        Append32(styled, 0xffffffffU);
        Set32(styled, 4, static_cast<std::uint32_t>(styled.size()));
        auto styled_block = f.New(pool, "([BZ)V", {VmValue::Ref(f.Bytes(styled)), VmValue::Int(0)});
        CHECK(f.OnOutcome(styled_block, "get", "(I)Ljava/lang/CharSequence;", {VmValue::Int(0)}).exception.IsValid());
        f.context->xml_cursors.Shutdown();
        CHECK(f.context->xml_cursors.LiveStringPools() == 0);
    }
}

TEST_CASE("DVM-287 APK XML entry executes ROM Parser and GC retires native state") {
    for (auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        XmlVm f(backend);
        auto fixture = CompiledFactsFixture();
        f.context->apk_bytes = ogplay::test::file_vfs::MakeStoredZip("res/layout/example.xml", fixture);
        f.context->archive = ogplay::loader::ParseApkArchive(f.context->apk_bytes);
        auto owner = f.New("Landroid/content/Context;");
        auto assets = f.On(owner, "getAssets", "()Landroid/content/res/AssetManager;").ref;
        auto name = f.vm.NewStringUtf8("res/layout/example.xml");
        const auto roots = f.vm.ProtectReferences(std::array{owner, assets, name});
        {
            auto p = f.On(assets, "openXmlResourceParser",
                          "(Ljava/lang/String;)Landroid/content/res/XmlResourceParser;", {VmValue::Ref(name)})
                         .ref;
            const auto root = f.vm.ProtectReferences(std::array{p});
            f.On(p, "next", "()I");
            CHECK(f.On(p, "next", "()I").AsInt() == 2);
            CHECK(f.Text(f.On(p, "getName", "()Ljava/lang/String;")) == "root");
            static_cast<void>(f.vm.CollectGarbage());
            CHECK(f.context->xml_cursors.LiveDocuments() == 1);
            CHECK(f.context->xml_cursors.LiveCursors() == 1);
            f.On(p, "close", "()V");
            CHECK(f.context->xml_cursors.LiveDocuments() == 0);
        }
        {
            auto p = f.On(assets, "openXmlResourceParser",
                          "(Ljava/lang/String;)Landroid/content/res/XmlResourceParser;", {VmValue::Ref(name)})
                         .ref;
            const auto root = f.vm.ProtectReferences(std::array{p});
            static_cast<void>(f.vm.CollectGarbage());
            CHECK(f.context->xml_cursors.LiveCursors() == 1);
        }
        f.On(owner, "hashCode", "()I");  // clear the VM exit-result root from openXmlResourceParser
        static_cast<void>(f.vm.CollectGarbage());
        f.vm.RunFinalization();
        static_cast<void>(f.vm.CollectGarbage());
        CHECK(f.context->xml_cursors.LiveDocuments() == 0);
        CHECK(f.context->xml_cursors.LiveCursors() == 0);
        CHECK(f.context->xml_cursors.LiveStringPools() == 0);
        {
            auto strings =
                f.New(pool, "([BZ)V", {VmValue::Ref(f.Bytes(StringPool({"standalone"}, true))), VmValue::Int(0)});
            const auto root = f.vm.ProtectReferences(std::array{strings});
            f.On(strings, "get", "(I)Ljava/lang/CharSequence;", {VmValue::Int(0)});
            static_cast<void>(f.vm.CollectGarbage());
            CHECK(f.context->xml_cursors.LiveStringPools() == 1);
        }
        f.On(owner, "hashCode", "()I");
        static_cast<void>(f.vm.CollectGarbage());
        f.vm.RunFinalization();
        static_cast<void>(f.vm.CollectGarbage());
        CHECK(f.context->xml_cursors.LiveStringPools() == 0);
        CHECK(f.ledger.Unimplemented().empty());
        auto system = f.Static("Landroid/content/res/Resources;", "getSystem", "()Landroid/content/res/Resources;").ref;
        auto system_assets = f.On(system, "getAssets", "()Landroid/content/res/AssetManager;").ref;
        CHECK(f.OnOutcome(system_assets, "openXmlResourceParser",
                          "(Ljava/lang/String;)Landroid/content/res/XmlResourceParser;", {VmValue::Ref(name)})
                  .exception.IsValid());
        CHECK_FALSE(f.ledger.Unimplemented().empty());
    }
}
