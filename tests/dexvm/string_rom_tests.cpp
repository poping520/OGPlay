#include "interpreter_fixture.h"

#include <array>
#include <limits>
#include <random>
#include <set>
#include <tuple>

#include "ogplay/runtime/dexvm/reflection.h"

namespace {
using namespace ogplay::runtime;
using namespace ogplay::runtime::dexvm;
using namespace ogplay::test::interpreter;

constexpr auto kString = "Ljava/lang/String;";
constexpr auto kFixture = "LStringRom;";

VmValue Virtual(Vm& fixture, VmObjectRef receiver, const char* name, const char* signature,
                std::initializer_list<VmValue> parameters = {}) {
    const auto type = fixture.model.ObjectClass(receiver);
    const auto slot = fixture.linker.FindVtableIndex(type, name, signature);
    REQUIRE(slot);
    std::vector arguments{VmValue::Ref(receiver)};
    arguments.insert(arguments.end(), parameters.begin(), parameters.end());
    const auto result = fixture.interpreter.Call(fixture.linker.Class(type).vtable[*slot], arguments);
    REQUIRE_MESSAGE(!result.exception.IsValid(), result.exception_message);
    return result.value;
}

VmValue Static(Vm& fixture, const char* type, const char* name, const char* signature,
               std::vector<VmValue> parameters = {}) {
    const auto result = fixture.CallStatic(type, name, signature, std::move(parameters));
    REQUIRE_MESSAGE(!result.exception.IsValid(), result.exception_message);
    return result.value;
}

struct StringFields final {
    VmObjectRef value;
    std::int32_t offset;
    std::int32_t count;
    std::int32_t hash;
};

StringFields ReadStringFields(Vm& fixture, VmObjectRef string) {
    auto& reflection = fixture.interpreter.Reflection();
    const auto read = [&](const char* name) {
        const auto field = reflection.FindDeclaredField(fixture.model.ObjectClass(string), name);
        REQUIRE(field);
        const auto primitive =
            fixture.linker.Class(field->type).descriptor == "I" ? std::optional{field->type} : std::nullopt;
        return reflection.GetField(*field, string, primitive, std::nullopt, true);
    };
    return {read("value").ref, read("offset").AsInt(), read("count").AsInt(), read("hashCode").AsInt()};
}

TEST_CASE("API19 ROM String reflection exposes exactly the core.jar declarations") {
    WithEachBackend([](const InterpreterConfig config) {
        Vm fixture(config);
        const auto type = fixture.linker.ResolveDescriptor(kString);
        const auto& cls = fixture.linker.Class(type);
        REQUIRE(cls.is_boot_dex);
        const auto comparator = fixture.linker.ResolveDescriptor("Ljava/lang/String$CaseInsensitiveComparator;");
        CHECK(fixture.linker.Class(comparator).is_boot_dex);
        const auto charsets = fixture.linker.ResolveDescriptor("Ljava/nio/charset/Charsets;");
        REQUIRE(fixture.linker.Class(charsets).is_boot_dex);
        std::size_t charset_natives = 0;
        for (const auto method_id : fixture.linker.MethodsOf(charsets)) {
            const auto& method = fixture.linker.Method(method_id);
            if (method.access_flags & kAccNative) {
                CHECK(static_cast<bool>(method.implementation));
                ++charset_natives;
            }
        }
        CHECK(charset_natives == 5);
        for (const auto* dependency :
             {"Ljava/lang/AbstractStringBuilder;", "Ljava/lang/IntegralToString;", "Ljava/lang/CaseMapper;",
              "Ljava/lang/StringIndexOutOfBoundsException;", "Ljava/util/Formatter;", "Ljava/util/regex/Pattern;",
              "Ljava/util/regex/Splitter;"})
            CHECK(fixture.linker.Class(fixture.linker.ResolveDescriptor(dependency)).is_boot_dex);

        const auto& jars = ogplay::test::BootClasspathJars();
        const auto core =
            std::ranges::find_if(jars, [](const auto& jar) { return jar.guest_path.ends_with("/core.jar"); });
        REQUIRE(core != jars.end());
        const auto dexes = ogplay::runtime::BootClasspathDexFiles(std::span{&*core, 1});
        REQUIRE(dexes.size() == 1);
        const auto image = ogplay::loader::ParseDex(dexes.front());
        const auto def = std::ranges::find_if(
            image.classes, [&](const auto& item) { return image.types[item.class_type_index].descriptor == kString; });
        REQUIRE(def != image.classes.end());
        const auto data = ogplay::loader::ReadDexClassData(dexes.front(), image);
        const auto& rom = data[static_cast<std::size_t>(def - image.classes.begin())];
        const auto name = [&](std::uint32_t index) {
            const auto& text = image.strings[index].value;
            return std::string(text.begin(), text.end());
        };
        using Declaration = std::tuple<std::string, std::string, std::uint32_t>;
        std::multiset<Declaration> fields, methods, constructors;
        std::vector<std::string> instance_fields;
        for (const auto& field : rom.instance_fields)
            instance_fields.push_back(name(image.fields[field.field_index].name_string_index));
        std::vector<std::string> linked_instance_fields;
        for (const auto field : cls.own_instance_fields)
            linked_instance_fields.push_back(fixture.linker.Field(field).name);
        CHECK(linked_instance_fields == instance_fields);
        CHECK(std::set<std::string>(instance_fields.begin(), instance_fields.end()) ==
              std::set<std::string>{"value", "offset", "count", "hashCode"});

        for (const auto* list : {&rom.instance_fields, &rom.static_fields})
            for (const auto& field : *list) {
                const auto& id = image.fields[field.field_index];
                fields.emplace(name(id.name_string_index), image.types[id.type_index].descriptor, field.access_flags);
            }
        for (const auto* list : {&rom.direct_methods, &rom.virtual_methods})
            for (const auto& method : *list) {
                const auto& id = image.methods[method.method_index];
                const auto& proto = image.prototypes[id.prototype_index];
                const auto method_name = name(id.name_string_index);
                if (method_name == "<clinit>") continue;
                std::string signature{"("};
                for (const auto arg : proto.parameter_type_indices)
                    signature += image.types[arg].descriptor;
                signature += ")" + image.types[proto.return_type_index].descriptor;
                auto& members = method_name == "<init>" ? constructors : methods;
                members.emplace(method_name, std::move(signature), method.access_flags);
            }
        const auto mirror = fixture.model.ClassObject(type);
        auto& reflection = fixture.interpreter.Reflection();
        const auto declared_fields = Virtual(fixture, mirror, "getDeclaredFields", "()[Ljava/lang/reflect/Field;").ref;
        const auto fields_root = fixture.interpreter.ProtectReferences(std::array{declared_fields});
        const auto declared_methods =
            Virtual(fixture, mirror, "getDeclaredMethods", "()[Ljava/lang/reflect/Method;").ref;
        const auto methods_root = fixture.interpreter.ProtectReferences(std::array{declared_methods});
        const auto declared_ctors =
            Virtual(fixture, mirror, "getDeclaredConstructors", "()[Ljava/lang/reflect/Constructor;").ref;
        for (JniSize i = 0; i < fixture.model.ArrayLength(declared_fields); ++i) {
            const auto& meta = reflection.FieldMetadata(fixture.model.GetObjectElement(declared_fields, i));
            const auto& field = fixture.linker.Field(meta.field);
            CHECK(fields.erase({field.name, field.descriptor, meta.access_flags}) == 1);
        }
        std::size_t native_count = 0;
        for (JniSize i = 0; i < fixture.model.ArrayLength(declared_methods); ++i) {
            const auto& meta = reflection.MethodMetadata(fixture.model.GetObjectElement(declared_methods, i));
            const auto& method = fixture.linker.Method(meta.method);
            CHECK(methods.erase({method.name, method.descriptor, meta.access_flags}) == 1);
            if (method.access_flags & kAccNative) {
                CHECK(static_cast<bool>(method.implementation));
                ++native_count;
            } else {
                CHECK(method.kind == MethodKind::interpreted);
                CHECK(method.code.has_value());
            }
        }
        for (JniSize i = 0; i < fixture.model.ArrayLength(declared_ctors); ++i) {
            const auto& meta = reflection.ConstructorMetadata(fixture.model.GetObjectElement(declared_ctors, i));
            const auto& method = fixture.linker.Method(meta.method);
            CHECK(constructors.erase({method.name, method.descriptor, meta.access_flags}) == 1);
            CHECK(method.kind == MethodKind::interpreted);
        }
        CHECK(fields.empty());
        CHECK(methods.empty());
        CHECK(constructors.empty());
        CHECK(native_count == 4);
    });
}

TEST_CASE("API19 ROM String shares value slices and caches Java hash codes") {
    WithEachBackend([](const InterpreterConfig config) {
        Vm fixture(config, {}, {}, "string_rom.dex");
        auto& model = fixture.model;
        const auto text = model.NewString(u"xabc");
        const auto slice =
            Virtual(fixture, text, "substring", "(II)Ljava/lang/String;", {VmValue::Int(1), VmValue::Int(4)}).ref;
        const auto original = ReadStringFields(fixture, text);
        const auto fields = ReadStringFields(fixture, slice);
        CHECK(fields.value == original.value);
        CHECK(fields.offset == 1);
        CHECK(fields.count == 3);
        CHECK(fields.hash == 0);
        CHECK(model.StringChars(slice).data() == model.StringChars(text).data() + 1);
        CHECK(Static(fixture, kFixture, "hashCode", "(Ljava/lang/String;)I", {VmValue::Ref(slice)}).AsInt() == 96354);
        CHECK(ReadStringFields(fixture, slice).hash == 96354);

        const auto builder = fixture.interpreter.NewIntrinsicInstance("Ljava/lang/StringBuilder;");
        const auto ctor = fixture.linker.FindDirectMethod(model.ObjectClass(builder), "<init>", "(I)V");
        REQUIRE(ctor);
        REQUIRE_FALSE(
            fixture.interpreter.Call(*ctor, std::array{VmValue::Ref(builder), VmValue::Int(4)}).exception.IsValid());
        Virtual(fixture, builder, "append", "(Ljava/lang/String;)Ljava/lang/StringBuilder;", {VmValue::Ref(slice)});
        const auto frozen = Virtual(fixture, builder, "toString", "()Ljava/lang/String;").ref;
        const auto builder_value = fixture.linker.FindFieldRecursive(model.ObjectClass(builder), "value", "[C");
        REQUIRE(builder_value);
        CHECK(ReadStringFields(fixture, frozen).value.Value() ==
              model.InstanceSlots(builder)[fixture.linker.Field(*builder_value).slot].bits);
        Virtual(fixture, builder, "setCharAt", "(IC)V", {VmValue::Int(0), VmValue::Int('z')});
        CHECK(model.StringChars(frozen) == u"abc");

        const auto canonical = model.NewString(u"string-rom-intern");
        CHECK(Virtual(fixture, canonical, "intern", "()Ljava/lang/String;").ref == canonical);
        CHECK(Static(fixture, kFixture, "literal", "()Ljava/lang/String;").ref == canonical);
        const auto weak = model.NewString(u"weak-string-rom");
        const auto weak_id = model.ToIdentity(weak);
        CHECK(Virtual(fixture, weak, "intern", "()Ljava/lang/String;").ref == weak);
        static_cast<void>(fixture.interpreter.CollectGarbage());
        CHECK_FALSE(model.Contains(weak_id));
        CHECK(model.IsValidRef(canonical));
        const auto value = model.NewString(u"abc");
        const auto outcome =
            fixture.CallStatic(kFixture, "charAt", "(Ljava/lang/String;I)C", {VmValue::Ref(value), VmValue::Int(-1)});
        REQUIRE(outcome.exception.IsValid());
        CHECK(fixture.linker.Class(outcome.exception_class).descriptor ==
              "Ljava/lang/StringIndexOutOfBoundsException;");
        CHECK(outcome.exception_message == "length=3; index=-1");
    });
}

TEST_CASE("API19 ROM String uses Charsets natives and Java UTF-8 decoding") {
    WithEachBackend([](const InterpreterConfig config) {
        Vm fixture(config);
        auto& model = fixture.model;
        const auto string = fixture.linker.ResolveDescriptor(kString);
        const auto ctor = fixture.linker.FindDirectMethod(string, "<init>", "([BIILjava/lang/String;)V");
        REQUIRE(ctor);
        const auto byte_array = fixture.linker.ResolveDescriptor("[B");
        const std::array data{std::byte{0x00}, std::byte{0xff}, std::byte{0x41}, std::byte{0x00}};
        const auto array = model.NewPrimitiveArray(byte_array, JniPrimitiveKind::byte, 4);
        model.WriteByteRegion(array, 0, data);
        for (const auto& [encoding, first, encoded] :
             {std::tuple{"UTF-8", u'\ufffd',
                         std::vector<std::byte>{std::byte{0xef}, std::byte{0xbf}, std::byte{0xbd}, std::byte{0x41}}},
              std::tuple{"US-ASCII", u'\ufffd', std::vector<std::byte>{std::byte{'?'}, std::byte{0x41}}},
              std::tuple{"ISO-8859-1", u'\u00ff', std::vector<std::byte>{std::byte{0xff}, std::byte{0x41}}}}) {
            CAPTURE(encoding);
            const auto value = fixture.interpreter.NewIntrinsicInstance(kString);
            const auto charset = fixture.interpreter.NewStringUtf8(encoding);
            const auto outcome =
                fixture.interpreter.Call(*ctor, std::array{VmValue::Ref(value), VmValue::Ref(array), VmValue::Int(1),
                                                           VmValue::Int(2), VmValue::Ref(charset)});
            REQUIRE_MESSAGE(!outcome.exception.IsValid(), outcome.exception_message);
            CHECK(model.StringChars(value) == std::u16string{first, u'A'});
            const auto bytes =
                Virtual(fixture, value, "getBytes", "(Ljava/lang/String;)[B", {VmValue::Ref(charset)}).ref;
            CHECK(model.ReadByteRegion(bytes, 0, model.ArrayLength(bytes)) == encoded);
        }
        for (const auto* encoding : {"UTF-16", "UTF-16BE", "UTF-16LE"}) {
            CAPTURE(encoding);
            const auto value = model.NewString(u"A\U0001f600");
            const auto charset = fixture.interpreter.NewStringUtf8(encoding);
            const auto bytes =
                Virtual(fixture, value, "getBytes", "(Ljava/lang/String;)[B", {VmValue::Ref(charset)}).ref;
            const auto output = model.ReadByteRegion(bytes, 0, model.ArrayLength(bytes));
            if (std::string_view(encoding) == "UTF-16")
                CHECK(output == std::vector<std::byte>{std::byte{0xfe}, std::byte{0xff}, std::byte{0}, std::byte{'A'},
                                                       std::byte{0xd8}, std::byte{0x3d}, std::byte{0xde},
                                                       std::byte{0}});
            const auto decoded = fixture.interpreter.NewIntrinsicInstance(kString);
            const auto outcome = fixture.interpreter.Call(
                *ctor, std::array{VmValue::Ref(decoded), VmValue::Ref(bytes), VmValue::Int(0),
                                  VmValue::Int(model.ArrayLength(bytes)), VmValue::Ref(charset)});
            REQUIRE_MESSAGE(!outcome.exception.IsValid(), outcome.exception_message);
            CHECK(model.StringChars(decoded) == u"A\U0001f600");
        }
        constexpr char16_t surrogates[]{0xd800, 0xd83d, 0xde00, 0xdc00};
        const auto value = model.NewString({surrogates, 4});
        const auto utf8 = Virtual(fixture, value, "getBytes", "()[B").ref;
        CHECK(model.ReadByteRegion(utf8, 0, model.ArrayLength(utf8)) ==
              std::vector<std::byte>{std::byte{'?'}, std::byte{0xf0}, std::byte{0x9f}, std::byte{0x98}, std::byte{0x80},
                                     std::byte{'?'}});
    });
}

TEST_CASE("API19 ROM String fast invokes match switch bytecode across boundaries and random input") {
    const InterpreterConfig switch_config{.backend = InterpreterBackend::switch_dispatch};
    const InterpreterConfig threaded_config{.backend = InterpreterBackend::threaded};
    Vm reference(switch_config, {}, {}, "string_rom.dex");
    Vm accelerated(threaded_config, {}, {}, "string_rom.dex");
    VmThreadRuntime reference_threads(reference.interpreter);
    VmThreadRuntime accelerated_threads(accelerated.interpreter);
    const auto string = accelerated.linker.ResolveDescriptor(kString);
    const auto calls = accelerated.linker.MethodsOf(accelerated.linker.ResolveDescriptor(kFixture));
    std::minstd_rand rng(0x19);
    constexpr std::array<char16_t, 10> alphabet{u'\0', u'a', u'A', u'b', u'B', u'Z', u'0', u'1', 0xd83d, 0xde00};
    const auto random_text = [&]() {
        std::u16string text;
        const auto size = rng() % 12;
        while (text.size() < size)
            text.push_back(alphabet[rng() % alphabet.size()]);
        return text;
    };
    std::size_t fast_targets = 0;
    for (const auto id : accelerated.linker.MethodsOf(string))
        if (accelerated.linker.Method(id).fast_op != StringFastOp::none) ++fast_targets;
    CHECK(fast_targets == 22);
    for (const auto id : calls) {
        const auto& linked = accelerated.linker.Method(id);
        if (linked.name == "literal") continue;
        const auto name = linked.name;
        const auto signature = linked.descriptor;
        const auto oracle = reference.Static(kFixture, name, signature);
        CAPTURE(name);
        CAPTURE(signature);
        const auto run = [&](Vm& f, VmMethodId method, const std::u16string& left, const std::u16string& right,
                             int null_case, std::array<int, 4> numbers) {
            const auto self = null_case == 1 ? VmObjectRef{} : f.model.NewString(left);
            std::vector args{VmValue::Ref(self)};
            const auto start = signature.find(';') + 1;
            std::size_t number = 0;
            for (std::size_t i = start; signature[i] != ')'; ++i) {
                if (signature[i] != 'L') {
                    args.push_back(VmValue::Int(numbers[number++]));
                    continue;
                }
                const auto other = null_case == 2 ? VmObjectRef{} : f.model.NewString(right);
                args.push_back(VmValue::Ref(other));
                i = signature.find(';', i);
            }
            const auto result = f.interpreter.Call(method, args);
            return std::tuple{result.exception.IsValid() ? f.linker.Class(result.exception_class).descriptor : "",
                              result.exception.IsValid() ? 0 : result.value.AsInt(), result.exception_message};
        };
        const auto compare = [&](const std::u16string& a, const std::u16string& b, int null_case,
                                 std::array<int, 4> n) {
            INFO("null=", null_case, ", numbers=", n[0], ",", n[1], ",", n[2], ",", n[3]);
            CHECK(run(accelerated, id, a, b, null_case, n) == run(reference, oracle, a, b, null_case, n));
        };
        constexpr std::array starts{INT32_MIN, -2, -1, 0, 1, 3, 4, 5, 100, INT32_MAX};
        constexpr std::array code_points{-1, 0, 'a' + 0, 0xd83d, 0xde00, 0x1f600, 0x10ffff, 0x110000, INT32_MAX};
        for (const auto n : starts) {
            compare(u"", u"", 0, {n, n, n, n});
            compare(u"aAa", u"Aa", 0, {n, 0, 0, 2});
            compare(u"aAa", u"", 0, {0, n, n, -1});
            compare(u"A", u"a", 0, {1, 0, 0, 1});
        }
        for (const auto cp : code_points)
            compare(std::u16string(u"\0a\U0001f600a", 5), u"a", 0, {cp, INT32_MAX, 1, 1});
        compare(u"abc", u"abc", 1, {0, 0, 0, 0});
        compare(u"abc", u"abc", 2, {0, 0, 0, 0});
        for (int trial = 0; trial < 24; ++trial) {
            const auto a = random_text();
            const auto b = trial % 3 == 0 ? a : random_text();
            std::array<int, 4> numbers;
            for (auto& number : numbers)
                number = static_cast<int>(rng() % 15) - 4;
            compare(a, b, 0, numbers);
        }
    }
    const auto& code =
        accelerated.linker.FastCodeFor(accelerated.Static(kFixture, "indexOf", "(Ljava/lang/String;II)I"));
    REQUIRE(code.invokes.size() == 1);
    CHECK(code.invokes.front().fast_op == StringFastOp::index_char_from);
    // Call indexOf as the entry method to exercise its actual ROM invoke-direct.
    // The public indexOf call-site above is tested independently.
    const auto native = accelerated.linker.FindDirectMethod(string, "fastIndexOf", "(II)I");
    REQUIRE(native);
    const auto index = [&](Vm& f) {
        const auto type = f.linker.ResolveDescriptor(kString);
        const auto slot = f.linker.FindVtableIndex(type, "indexOf", "(II)I");
        REQUIRE(slot);
        return f.linker.Class(type).vtable[*slot];
    };
    const auto reference_index = index(reference);
    const auto accelerated_index = index(accelerated);
    constexpr std::array starts{INT32_MIN, -1, 0, 2, 4, INT32_MAX};
    const auto compare_direct = [&](const std::u16string& text, int ch, int start) {
        const auto call = [&](Vm& f, VmMethodId method) {
            return f.interpreter.Call(
                method, std::array{VmValue::Ref(f.model.NewString(text)), VmValue::Int(ch), VmValue::Int(start)});
        };
        const auto expected = call(reference, reference_index);
        REQUIRE_FALSE(expected.exception.IsValid());
        ExpectInt(call(accelerated, accelerated_index), expected.value.AsInt());
    };
    for (const auto start : starts) {
        compare_direct(u"aaba", 'a', start);
        compare_direct(u"", 0, start);
        compare_direct(u"\U0001f600", 0xde00, start);
        compare_direct(u"a", -1, start);
    }
    for (int trial = 0; trial < 24; ++trial)
        compare_direct(random_text(), alphabet[rng() % alphabet.size()], static_cast<int>(rng() % 15) - 4);
    const auto& rom = accelerated.linker.FastCodeFor(accelerated_index);
    CHECK(std::ranges::any_of(rom.invokes,
                              [](const auto& invoke) { return invoke.fast_op == StringFastOp::fast_index_of; }));
}

TEST_CASE("API19 ROM String method trace sees calls eligible for acceleration") {
    InterpreterConfig config{.backend = InterpreterBackend::threaded};
    config.diagnostics.trace_capacity = 256;
    Vm fixture(config, {}, {}, "string_rom.dex");
    const auto text = fixture.model.NewString(u"trace");
    CHECK(Static(fixture, kFixture, "length", "(Ljava/lang/String;)I", {VmValue::Ref(text)}).AsInt() == 5);
    const auto entries = fixture.interpreter.Trace({}, 256);
    CHECK(std::ranges::any_of(entries, [](const auto& event) {
        return event.kind == DexVmTraceKind::method_enter && event.class_descriptor == kString &&
               event.method_name == "length";
    }));
}
}  // namespace
