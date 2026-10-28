#include <doctest/doctest.h>
#include <array>
#include <cmath>
#include <limits>
#include "native_library_fixture.h"
#include "../dexvm/boot_dex.h"
#include "ogplay/runtime/bionic/bionic_profile.h"
#include "ogplay/runtime/integration/dexvm_android.h"
#include "ogplay/session/android_app_process.h"

namespace {
using namespace ogplay;
using namespace runtime::dexvm;
using namespace test::native_library;

struct IcuVm final {
    runtime::VirtualFileSystem filesystem;
    core::CapabilityLedger ledger;
    core::Logger logger;
    std::shared_ptr<runtime::DexVmAndroidContext> context{std::make_shared<runtime::DexVmAndroidContext>()};
    std::unique_ptr<session::AndroidAppProcess> app;

    explicit IcuVm(InterpreterBackend backend, std::string_view language = "en", std::string_view region = "US") {
        context->locale_language = language;
        context->locale_region = region;
        std::vector<std::vector<std::byte>> bytes;
        std::vector<runtime::BionicModuleSource> sources;
        for (const auto name : runtime::SelectBionicProfile(19).guest_libraries) {
            bytes.push_back(ReadPayloadBytes("lib/" + std::string(name)));
            sources.push_back({std::string(name), bytes.back()});
        }
        session::AndroidAppProcessRequest request;
        request.manifest = AppManifest("fixture.MainActivity");
        request.system_libraries = sources;
        request.dex_bytes = ReadDexFixture("cipher.dex");
        request.icu_data = ReadPayloadBytes("icu/icudt51l.dat");
        request.tzdata = ReadPayloadBytes("zoneinfo/tzdata");
        request.boot_dex_bytes = test::ReadBootDex();
        test::BindBootDexArchive(*context);
        request.context = context;
        request.dexvm.interpreter.backend = backend;
        request.surface_width = 64;
        request.surface_height = 36;
        request.maximum_ticks_per_call = UINT64_C(100000000);
        request.filesystem = &filesystem;
        request.ledger = &ledger;
        request.logger = &logger;
        app = session::AndroidAppProcess::Create(std::move(request));
    }

    Interpreter& Vm() { return app->DexVm().Vm(); }

    VmValue Checked(const VmCallOutcome& outcome) {
        REQUIRE_MESSAGE(!outcome.exception.IsValid(),
                        (outcome.exception.IsValid()
                             ? std::string(Vm().Linker().Class(outcome.exception_class).descriptor) + ": " +
                                   outcome.exception_message
                             : ""));
        return outcome.value;
    }

    VmValue Static(const char* owner, const char* name, const char* descriptor, std::vector<VmValue> args = {}) {
        auto& vm = Vm();
        const auto type = vm.Linker().ResolveDescriptor(owner);
        const auto method = vm.Linker().FindDirectMethod(type, name, descriptor);
        REQUIRE_MESSAGE(method, name);
        return Checked(vm.Call(*method, args));
    }

    VmCallOutcome Invoke(VmObjectRef receiver, const char* name, const char* descriptor,
                         std::vector<VmValue> args = {}) {
        auto& vm = Vm();
        const auto type = vm.Model().ObjectClass(receiver);
        const auto slot = vm.Linker().FindVtableIndex(type, name, descriptor);
        REQUIRE_MESSAGE(slot, name);
        args.insert(args.begin(), VmValue::Ref(receiver));
        return vm.Call(vm.Linker().Class(type).vtable[*slot], args);
    }

    VmValue On(VmObjectRef receiver, const char* name, const char* descriptor, std::vector<VmValue> args = {}) {
        return Checked(Invoke(receiver, name, descriptor, std::move(args)));
    }

    VmObjectRef Text(const char* text) { return Vm().NewStringUtf8(text); }

    void Construct(VmObjectRef result, const char* owner, const char* descriptor, std::vector<VmValue> args = {}) {
        const auto root = Vm().ProtectReferences(std::array{result});
        args.insert(args.begin(), VmValue::Ref(result));
        Static(owner, "<init>", descriptor, std::move(args));
    }

    VmObjectRef New(const char* owner, const char* descriptor, std::vector<VmValue> args = {}) {
        std::vector<VmObjectRef> refs;
        for (const auto& value : args)
            if (value.kind == VmValue::Kind::ref) refs.push_back(value.ref);
        const auto roots = Vm().ProtectReferences(refs);
        const auto result = Vm().NewIntrinsicInstance(owner);
        const auto root = Vm().ProtectReferences(std::array{result});
        args.insert(args.begin(), VmValue::Ref(result));
        Static(owner, "<init>", descriptor, std::move(args));
        return result;
    }

    std::string String(VmValue value) { return Vm().StringUtf8(value.ref); }

    VmObjectRef Pattern(const char* regex) {
        return Static("Ljava/util/regex/Pattern;", "compile", "(Ljava/lang/String;)Ljava/util/regex/Pattern;",
                      {VmValue::Ref(Text(regex))})
            .ref;
    }

    VmObjectRef Matcher(VmObjectRef pattern, const char* text) {
        return On(pattern, "matcher", "(Ljava/lang/CharSequence;)Ljava/util/regex/Matcher;", {VmValue::Ref(Text(text))})
            .ref;
    }
};
}  // namespace

TEST_CASE("WU7-6 ICU51 supplies locale data decimal double digit lists and date fields") {
    for (auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        IcuVm f(backend);
        auto& vm = f.Vm();
        // TimeZone clinit compiles a platform ICU pattern. Keep the DEX
        // static-field ordering regression on the production guest path.
        const auto zone = vm.Linker().ResolveDescriptor("Ljava/util/TimeZone;");
        f.Checked(vm.EnsureClassInitialized(zone));
        for (const auto& [name, expected] : std::array{std::pair{"LONG", 1U}, std::pair{"SHORT", 0U}}) {
            const auto field = vm.Linker().FindFieldRecursive(zone, name, "I");
            REQUIRE(field);
            CHECK(vm.Linker().Class(zone).static_storage[vm.Linker().Field(*field).slot] == expected);
        }
        const auto formatter =
            f.New("Ljava/text/DecimalFormat;", "(Ljava/lang/String;)V", {VmValue::Ref(f.Text("#,##0.00"))});
        const auto formatter_root = vm.ProtectReferences(std::array{formatter});
        CHECK(f.String(f.On(formatter, "format", "(D)Ljava/lang/String;", {VmValue::Double(1234.5)})) == "1,234.50");
        CHECK(f.String(f.On(formatter, "format", "(D)Ljava/lang/String;", {VmValue::Double(-0.25)})) == "-0.25");
        CHECK(f.String(f.On(formatter, "format", "(D)Ljava/lang/String;",
                            {VmValue::Double(std::numeric_limits<double>::infinity())})) == "∞");
        CHECK(f.String(f.On(formatter, "format", "(D)Ljava/lang/String;",
                            {VmValue::Double(std::numeric_limits<double>::quiet_NaN())})) == "NaN");
        const auto big = f.New("Ljava/math/BigInteger;", "(Ljava/lang/String;)V",
                               {VmValue::Ref(f.Text("123456789012345678901234567890"))});
        const auto big_root = vm.ProtectReferences(std::array{big});
        CHECK(f.String(f.On(formatter, "format", "(Ljava/lang/Object;)Ljava/lang/String;", {VmValue::Ref(big)})) ==
              "123,456,789,012,345,678,901,234,567,890.00");
        const auto buffer = f.New("Ljava/lang/StringBuffer;", "()V");
        const auto buffer_root = vm.ProtectReferences(std::array{buffer});
        const auto position = f.New("Ljava/text/FieldPosition;", "(I)V", {VmValue::Int(1)});
        const auto position_root = vm.ProtectReferences(std::array{position});
        f.On(formatter, "format", "(DLjava/lang/StringBuffer;Ljava/text/FieldPosition;)Ljava/lang/StringBuffer;",
             {VmValue::Double(1234.5), VmValue::Ref(buffer), VmValue::Ref(position)});
        CHECK(f.On(position, "getBeginIndex", "()I").AsInt() == 6);
        CHECK(f.On(position, "getEndIndex", "()I").AsInt() == 8);
        const auto france = f.New("Ljava/util/Locale;", "(Ljava/lang/String;Ljava/lang/String;)V",
                                  {VmValue::Ref(f.Text("fr")), VmValue::Ref(f.Text("FR"))});
        const auto france_root = vm.ProtectReferences(std::array{france});
        const auto number = f.Static("Ljava/text/NumberFormat;", "getInstance",
                                     "(Ljava/util/Locale;)Ljava/text/NumberFormat;", {VmValue::Ref(france)})
                                .ref;
        const auto number_root = vm.ProtectReferences(std::array{number});
        CHECK(f.String(f.On(number, "format", "(D)Ljava/lang/String;", {VmValue::Double(1234.5)})) == "1 234,5");
        const auto date =
            f.Static("Ljava/text/DateFormat;", "getDateInstance", "(ILjava/util/Locale;)Ljava/text/DateFormat;",
                     {VmValue::Int(0), VmValue::Ref(france)})
                .ref;
        const auto date_root = vm.ProtectReferences(std::array{date});
        const auto value = f.New("Ljava/util/Date;", "(J)V", {VmValue::Long(1325462400000LL)});
        const auto value_root = vm.ProtectReferences(std::array{value});
        CHECK(f.String(f.On(date, "format", "(Ljava/util/Date;)Ljava/lang/String;", {VmValue::Ref(value)})) ==
              "lundi 2 janvier 2012");
        CHECK(f.ledger.Unimplemented().empty());
    }
}

TEST_CASE("WU7-6 ICU51 regex preserves UTF16 groups regions replacement syntax errors and cleanup") {
    for (auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        IcuVm f(backend);
        auto& vm = f.Vm();
        std::string stage = "unicode";
        INFO(stage);
        {
            const auto pattern = f.Pattern("(\\p{L}+)(\\p{Nd})?");
            const auto pattern_root = vm.ProtectReferences(std::array{pattern});
            const auto matcher = f.On(pattern, "matcher", "(Ljava/lang/CharSequence;)Ljava/util/regex/Matcher;",
                                      {VmValue::Ref(vm.Model().NewString(u"😀éé𝟙!"))})
                                     .ref;
            const auto matcher_root = vm.ProtectReferences(std::array{matcher});
            CHECK(f.On(matcher, "groupCount", "()I").AsInt() == 2);
            CHECK(f.On(matcher, "find", "()Z").AsInt() == 1);
            CHECK(f.On(matcher, "start", "()I").AsInt() == 2);
            CHECK(f.On(matcher, "end", "()I").AsInt() == 6);
            CHECK(f.String(f.On(matcher, "group", "(I)Ljava/lang/String;", {VmValue::Int(1)})) == "éé");
            static_cast<void>(vm.CollectGarbage("live-regex-owner"));
            f.On(matcher, "reset", "(Ljava/lang/CharSequence;)Ljava/util/regex/Matcher;", {VmValue::Ref(f.Text("xx"))});
            CHECK(f.On(matcher, "find", "()Z").AsInt() == 1);
            CHECK_FALSE(f.On(matcher, "group", "(I)Ljava/lang/String;", {VmValue::Int(2)}).ref.IsValid());
            CHECK(f.On(matcher, "start", "(I)I", {VmValue::Int(2)}).AsInt() == -1);
        }
        {
            stage = "transparent";
            const auto p = f.Pattern("a(?=Z)");
            const auto root = vm.ProtectReferences(std::array{p});
            const auto m = f.Matcher(p, "xaZ");
            const auto mr = vm.ProtectReferences(std::array{m});
            f.On(m, "region", "(II)Ljava/util/regex/Matcher;", {VmValue::Int(1), VmValue::Int(2)});
            CHECK(f.On(m, "regionStart", "()I").AsInt() == 1);
            CHECK(f.On(m, "regionEnd", "()I").AsInt() == 2);
            CHECK(f.On(m, "hasTransparentBounds", "()Z").AsInt() == 0);
            CHECK(f.On(m, "matches", "()Z").AsInt() == 0);
            f.On(m, "useTransparentBounds", "(Z)Ljava/util/regex/Matcher;", {VmValue::Int(1)});
            CHECK(f.On(m, "matches", "()Z").AsInt() == 1);
        }
        {
            stage = "anchoring";
            const auto p = f.Pattern("^a$");
            const auto root = vm.ProtectReferences(std::array{p});
            const auto m = f.Matcher(p, "xaZ");
            const auto mr = vm.ProtectReferences(std::array{m});
            f.On(m, "region", "(II)Ljava/util/regex/Matcher;", {VmValue::Int(1), VmValue::Int(2)});
            CHECK(f.On(m, "matches", "()Z").AsInt() == 1);
            f.On(m, "useAnchoringBounds", "(Z)Ljava/util/regex/Matcher;", {VmValue::Int(0)});
            CHECK(f.On(m, "matches", "()Z").AsInt() == 0);
        }
        {
            stage = "empty";
            const auto p = f.Pattern("");
            const auto root = vm.ProtectReferences(std::array{p});
            const auto m = f.On(p, "matcher", "(Ljava/lang/CharSequence;)Ljava/util/regex/Matcher;",
                                {VmValue::Ref(vm.Model().NewString(u"😀a"))})
                               .ref;
            const auto mr = vm.ProtectReferences(std::array{m});
            for (int expected : {0, 2, 3}) {
                CHECK(f.On(m, "find", "()Z").AsInt() == 1);
                CHECK(f.On(m, "start", "()I").AsInt() == expected);
            }
            CHECK(f.On(m, "find", "()Z").AsInt() == 0);
            CHECK(f.On(m, "hitEnd", "()Z").AsInt() == 1);
        }
        stage = "bad syntax";
        const auto bad = vm.Linker().FindDirectMethod(vm.Linker().ResolveDescriptor("Ljava/util/regex/Pattern;"),
                                                      "compile", "(Ljava/lang/String;)Ljava/util/regex/Pattern;");
        REQUIRE(bad);
        const auto error = vm.Call(*bad, std::array{VmValue::Ref(f.Text("[abc"))});
        REQUIRE(error.exception.IsValid());
        const auto eroot = vm.ProtectReferences(std::array{error.exception});
        CHECK(vm.Linker().Class(error.exception_class).descriptor == "Ljava/util/regex/PatternSyntaxException;");
        CHECK(f.String(f.On(error.exception, "getPattern", "()Ljava/lang/String;")) == "[abc");
        CHECK(f.On(error.exception, "getIndex", "()I").AsInt() >= 0);
        CHECK(f.String(f.On(f.Text("xxaaay"), "replaceAll", "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;",
                            {VmValue::Ref(f.Text("a+")), VmValue::Ref(f.Text("_"))})) == "xx_y");
        const auto before = vm.GuestNativeResourceCount();
        static_cast<void>(vm.CollectGarbage("regex-peers-reclaimed"));
        try {
            vm.RunFinalization();
        } catch (const VmJavaThrow& error) {
            FAIL(error.descriptor << ": " << error.message);
        }
        static_cast<void>(vm.CollectGarbage("regex-peers-reclaimed-finalized"));
        CHECK(vm.GuestNativeResourceCount() < before);
        CHECK(f.ledger.Unimplemented().empty());
    }
}

TEST_CASE("WU7-6 charset enumeration exposes only supported ICU codecs in an immutable case insensitive map") {
    for (auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        IcuVm f(backend);
        auto& vm = f.Vm();
        const auto map = f.Static("Ljava/nio/charset/Charset;", "availableCharsets", "()Ljava/util/SortedMap;").ref;
        const auto root = vm.ProtectReferences(std::array{map});
        CHECK(f.On(map, "size", "()I").AsInt() == 6);
        for (const char* name : {"US-ASCII", "ISO-8859-1", "UTF-8", "UTF-16", "UTF-16BE", "UTF-16LE"}) {
            const auto item =
                f.On(map, "get", "(Ljava/lang/Object;)Ljava/lang/Object;", {VmValue::Ref(f.Text(name))}).ref;
            REQUIRE(item.IsValid());
            CHECK(f.String(f.On(item, "name", "()Ljava/lang/String;")) == name);
        }
        CHECK(f.On(map, "containsKey", "(Ljava/lang/Object;)Z", {VmValue::Ref(f.Text("utf-8"))}).AsInt() == 1);
        const auto rejected = f.Invoke(map, "put", "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;",
                                       {VmValue::Ref(f.Text("fake")), VmValue::Ref(map)});
        REQUIRE(rejected.exception.IsValid());
        CHECK(vm.Linker().Class(rejected.exception_class).descriptor == "Ljava/lang/UnsupportedOperationException;");
        CHECK(f.ledger.Unimplemented().empty());
    }
}

TEST_CASE("WU7-6 JNI chars leases keep strings alive across guest cleanup GC") {
    for (auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        IcuVm f(backend);
        auto& vm = f.Vm();
        auto& strings = f.app->NativeProcess().Strings();
        const auto text = f.Text("leased");
        const auto identity = vm.Model().ToIdentity(text);
        const auto access = strings.Acquire(identity, runtime::JniStringAccessKind::chars);
        static_cast<void>(vm.CollectGarbage("active-string-lease"));
        CHECK(strings.Contains(identity));
        CHECK(vm.StringUtf8(text) == "leased");
        strings.Release(identity, access.token, access.kind);
        static_cast<void>(vm.CollectGarbage("released-string-lease"));
        CHECK_FALSE(strings.Contains(identity));
    }
}

TEST_CASE("DVM-183 SQL date value types use API 19 BootDex") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        CAPTURE(backend == InterpreterBackend::threaded ? "threaded" : "switch");
        IcuVm fixture(backend);
        auto& vm = fixture.Vm();
        auto& linker = vm.Linker();
        const auto util_date = linker.ResolveDescriptor("Ljava/util/Date;");
        const auto sql_date = linker.ResolveDescriptor("Ljava/sql/Date;");
        const auto sql_time = linker.ResolveDescriptor("Ljava/sql/Time;");
        const auto sql_timestamp = linker.ResolveDescriptor("Ljava/sql/Timestamp;");
        for (const auto type : {sql_date, sql_time, sql_timestamp}) {
            linker.EnsureClassLinked(type);
            const auto& linked = linker.Class(type);
            CHECK(linked.is_boot_dex);
            CHECK_FALSE(linked.is_intrinsic);
            CHECK(linked.super.has_value());
            CHECK(*linked.super == util_date);
            CHECK(linker.IsAssignable(util_date, type));
            for (const auto method : linked.own_virtual_methods) {
                CHECK(linker.Method(method).kind != MethodKind::intrinsic);
            }
            for (const auto method : linked.own_direct_methods) {
                CHECK(linker.Method(method).kind != MethodKind::intrinsic);
            }
        }

        const auto date = vm.NewIntrinsicInstance("Ljava/sql/Date;");
        fixture.Construct(date, "Ljava/sql/Date;", "(J)V", {VmValue::Long(0)});
        auto outcome = fixture.Invoke(date, "getTime", "()J");
        fixture.Checked(outcome);
        CHECK(outcome.value.AsLong() == 0);
        outcome = fixture.Invoke(date, "toString", "()Ljava/lang/String;");
        fixture.Checked(outcome);
        CHECK(vm.StringUtf8(outcome.value.ref) == "1970-01-01");
        outcome = fixture.Invoke(date, "getHours", "()I");
        REQUIRE(outcome.exception.IsValid());
        CHECK(linker.Class(outcome.exception_class).descriptor == "Ljava/lang/IllegalArgumentException;");

        const auto kept = vm.NewIntrinsicInstance("Ljava/sql/Date;");
        fixture.Construct(kept, "Ljava/sql/Date;", "(J)V", {VmValue::Long(12345)});
        outcome = fixture.Invoke(kept, "getTime", "()J");
        fixture.Checked(outcome);
        CHECK(outcome.value.AsLong() == 12345);

        const auto time = vm.NewIntrinsicInstance("Ljava/sql/Time;");
        fixture.Construct(time, "Ljava/sql/Time;", "(J)V", {VmValue::Long(0)});
        outcome = fixture.Invoke(time, "getTime", "()J");
        fixture.Checked(outcome);
        CHECK(outcome.value.AsLong() == 0);
        outcome = fixture.Invoke(time, "toString", "()Ljava/lang/String;");
        fixture.Checked(outcome);
        CHECK(vm.StringUtf8(outcome.value.ref) == "00:00:00");

        constexpr auto millis = 1'704'067'200'123LL;
        const auto timestamp = vm.NewIntrinsicInstance("Ljava/sql/Timestamp;");
        fixture.Construct(timestamp, "Ljava/sql/Timestamp;", "(J)V", {VmValue::Long(millis)});
        outcome = fixture.Invoke(timestamp, "getTime", "()J");
        fixture.Checked(outcome);
        CHECK(outcome.value.AsLong() == millis);
        outcome = fixture.Invoke(timestamp, "getNanos", "()I");
        fixture.Checked(outcome);
        CHECK(outcome.value.AsInt() == 123000000);
        fixture.Checked(fixture.Invoke(timestamp, "setNanos", "(I)V", {VmValue::Int(7)}));
        outcome = fixture.Invoke(timestamp, "getNanos", "()I");
        fixture.Checked(outcome);
        CHECK(outcome.value.AsInt() == 7);
        outcome = fixture.Invoke(timestamp, "getTime", "()J");
        fixture.Checked(outcome);
        CHECK(outcome.value.AsLong() == 1'704'067'200'000LL);
    }
}

TEST_CASE("WU7-6 guest timezone defaults clone reset and isolate production VM state") {
    for (auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        IcuVm f(backend);
        auto& vm = f.Vm();
        const auto get = [&] { return f.Static("Ljava/util/TimeZone;", "getDefault", "()Ljava/util/TimeZone;").ref; };
        const auto set = [&](VmObjectRef zone) {
            f.Static("Ljava/util/TimeZone;", "setDefault", "(Ljava/util/TimeZone;)V", {VmValue::Ref(zone)});
        };
        const auto zone = get();
        const auto root = vm.ProtectReferences(std::array{zone});
        f.On(zone, "setRawOffset", "(I)V", {VmValue::Int(28800000)});
        CHECK(f.On(get(), "getRawOffset", "()I").AsInt() == 0);
        set(zone);
        f.On(zone, "setRawOffset", "(I)V", {VmValue::Int(0)});
        CHECK(f.On(get(), "getRawOffset", "()I").AsInt() == 28800000);
        IcuVm separate(backend);
        const auto other = separate.Static("Ljava/util/TimeZone;", "getDefault", "()Ljava/util/TimeZone;").ref;
        CHECK(separate.On(other, "getRawOffset", "()I").AsInt() == 0);
        f.On(zone, "setRawOffset", "(I)V", {VmValue::Int(-12600000)});
        set(zone);
        CHECK(f.On(get(), "getRawOffset", "()I").AsInt() == -12600000);
        set(VmObjectRef{});
        CHECK(f.On(get(), "getRawOffset", "()I").AsInt() == 0);
        const auto method = vm.Linker().FindDirectMethod(vm.Linker().ResolveDescriptor("Ljava/util/TimeZone;"),
                                                         "getTimeZone", "(Ljava/lang/String;)Ljava/util/TimeZone;");
        REQUIRE(method);
        const auto named = vm.Call(*method, std::array{VmValue::Ref(f.Text("CET"))});
        REQUIRE(named.exception.IsValid());
        CHECK(vm.Linker().Class(named.exception_class).descriptor == "Ljava/lang/UnsupportedOperationException;");
    }
}

TEST_CASE("DVM-260 guest String backing preserves char array builder substring and value semantics") {
    for (auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        IcuVm f(backend);
        auto& vm = f.Vm();
        auto& model = vm.Model();
        const auto array =
            model.NewPrimitiveArray(vm.Linker().ResolveDescriptor("[C"), runtime::JniPrimitiveKind::character, 3);
        const auto array_root = vm.ProtectReferences(std::array{array});
        for (runtime::JniSize i = 0; i < 3; ++i)
            model.SetPrimitiveElement(array, i, static_cast<std::uint64_t>('A' + i));
        const auto text = f.New("Ljava/lang/String;", "([C)V", {VmValue::Ref(array)});
        const auto text_root = vm.ProtectReferences(std::array{text});
        const auto sub = f.On(text, "substring", "(II)Ljava/lang/String;", {VmValue::Int(0), VmValue::Int(2)}).ref;
        const auto sub_root = vm.ProtectReferences(std::array{sub});
        const auto builder = f.New("Ljava/lang/StringBuilder;", "()V");
        const auto builder_root = vm.ProtectReferences(std::array{builder});
        f.On(builder, "append", "([C)Ljava/lang/StringBuilder;", {VmValue::Ref(array)});
        const auto snapshot = f.On(builder, "toString", "()Ljava/lang/String;").ref;
        const auto snapshot_root = vm.ProtectReferences(std::array{snapshot});
        model.SetPrimitiveElement(array, 0, 'X');
        f.On(builder, "setCharAt", "(IC)V", {VmValue::Int(0), VmValue::Int('Y')});
        CHECK(vm.StringUtf8(text) == "ABC");
        CHECK(vm.StringUtf8(sub) == "AB");
        CHECK(vm.StringUtf8(snapshot) == "ABC");
        CHECK(f.On(text, "hashCode", "()I").AsInt() == 64578);
        CHECK(f.On(text, "equals", "(Ljava/lang/Object;)Z", {VmValue::Ref(snapshot)}).AsInt() == 1);
        CHECK(f.On(text, "intern", "()Ljava/lang/String;").ref == f.On(snapshot, "intern", "()Ljava/lang/String;").ref);
        auto& strings = f.app->NativeProcess().Strings();
        const auto a = strings.Acquire(model.ToIdentity(text), runtime::JniStringAccessKind::chars);
        const auto b = strings.Acquire(model.ToIdentity(sub), runtime::JniStringAccessKind::critical);
        const auto c = strings.Acquire(model.ToIdentity(snapshot), runtime::JniStringAccessKind::chars);
        CHECK(a.address != b.address);
        CHECK(a.address != c.address);
        CHECK(b.address != c.address);
        strings.Release(model.ToIdentity(text), 0, runtime::JniStringAccessKind::chars);
        strings.Release(model.ToIdentity(sub), 0, runtime::JniStringAccessKind::chars);
        strings.Release(model.ToIdentity(snapshot), 0, runtime::JniStringAccessKind::critical);
    }
}

TEST_CASE("DVM-331 orchestrated ROM FrameLayout construction uses guest ICU regex without a window") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        OrchestratedApp fixture("fixture.ResultCaller", true, false, {}, {}, true, backend);
        auto& vm = fixture.app->DexVm().Vm();
        const auto owner = vm.NewIntrinsicInstance("Landroid/content/Context;");
        test::AttachViewTestContext(vm, owner);
        const auto frame = vm.NewIntrinsicInstance("Landroid/widget/FrameLayout;");
        const auto roots = vm.ProtectReferences(std::array{owner, frame});
        const auto type = vm.Linker().ResolveDescriptor("Landroid/widget/FrameLayout;");
        const auto initialized = vm.EnsureClassInitialized(type);
        REQUIRE_MESSAGE(!initialized.exception.IsValid(), initialized.exception_message);
        const auto constructor = vm.Linker().FindDirectMethod(type, "<init>", "(Landroid/content/Context;)V");
        REQUIRE(constructor);
        // The same ROM constructor reached from lifecycle onCreate initializes
        // Rect, whose static regex and matcher must execute through guest ICU.
        const auto constructed = vm.Call(*constructor, std::array{VmValue::Ref(frame), VmValue::Ref(owner)});
        REQUIRE_MESSAGE(!constructed.exception.IsValid(), constructed.exception_message);
        const auto rect_type = vm.Linker().ResolveDescriptor("Landroid/graphics/Rect;");
        const auto parse = vm.Linker().FindDirectMethod(rect_type, "unflattenFromString",
                                                        "(Ljava/lang/String;)Landroid/graphics/Rect;");
        REQUIRE(parse);
        const auto parsed = vm.Call(*parse, std::array{VmValue::Ref(vm.NewStringUtf8("1 2 3 4"))});
        REQUIRE_MESSAGE(!parsed.exception.IsValid(), parsed.exception_message);
        REQUIRE(parsed.value.ref.IsValid());
        for (const auto& [field, expected] :
             std::array{std::pair{"left", 1}, std::pair{"top", 2}, std::pair{"right", 3}, std::pair{"bottom", 4}}) {
            const auto id = vm.Linker().FindFieldRecursive(rect_type, field, "I");
            REQUIRE(id);
            CHECK(static_cast<std::int32_t>(
                      vm.Model().InstanceSlots(parsed.value.ref)[vm.Linker().Field(*id).slot].bits) == expected);
        }
        CHECK(fixture.ledger.Unimplemented().empty());
    }
}

TEST_CASE("DVM-342 Locale executes ROM Java with injected device facts and ICU51") {
    using namespace ogplay;
    using namespace runtime::dexvm;
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        CAPTURE(backend == InterpreterBackend::threaded ? "threaded" : "switch");
        IcuVm f(backend, "fr", "CA");
        auto& vm = f.Vm();
        auto& linker = vm.Linker();
        const auto direct = [&](const char* owner, const char* name, const char* desc, std::vector<VmValue> args) {
            const auto method = linker.FindDirectMethod(linker.ResolveDescriptor(owner), name, desc);
            REQUIRE(method.has_value());
            return vm.Call(*method, args);
        };
        const auto require_string = [&](const VmCallOutcome& result) {
            REQUIRE_MESSAGE(!result.exception.IsValid(), result.exception_message);
            return vm.StringUtf8(result.value.ref);
        };
        const auto expect_exception = [&](const VmCallOutcome& result, const char* descriptor) {
            REQUIRE(result.exception.IsValid());
            CHECK(linker.Class(result.exception_class).descriptor == descriptor);
        };
        const auto on = [&](VmObjectRef receiver, const char* name, const char* desc, std::vector<VmValue> args = {}) {
            const auto owner = vm.Model().ObjectClass(receiver);
            const auto slot = linker.FindVtableIndex(owner, name, desc);
            REQUIRE_MESSAGE(slot.has_value(), name);
            args.insert(args.begin(), VmValue::Ref(receiver));
            return vm.Call(linker.Class(owner).vtable[*slot], args);
        };
        const auto locale_type = linker.ResolveDescriptor("Ljava/util/Locale;");
        CHECK(linker.Class(locale_type).is_boot_dex);
        const auto make = [&](const char* language, const char* country = "", const char* variant = "") {
            const auto value = vm.NewIntrinsicInstance("Ljava/util/Locale;");
            const auto root = vm.ProtectReferences(std::array{value});
            const auto l = vm.NewStringUtf8(language);
            const auto lr = vm.ProtectReferences(std::array{l});
            const auto c = vm.NewStringUtf8(country);
            const auto cr = vm.ProtectReferences(std::array{c});
            const auto v = vm.NewStringUtf8(variant);
            const auto result = *country == '\0' && *variant == '\0'
                                    ? direct("Ljava/util/Locale;", "<init>", "(Ljava/lang/String;)V",
                                             {VmValue::Ref(value), VmValue::Ref(l)})
                                    : direct("Ljava/util/Locale;", "<init>",
                                             "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)V",
                                             {VmValue::Ref(value), VmValue::Ref(l), VmValue::Ref(c), VmValue::Ref(v)});
            REQUIRE_MESSAGE(!result.exception.IsValid(), result.exception_message);
            return value;
        };
        const auto def = direct("Ljava/util/Locale;", "getDefault", "()Ljava/util/Locale;", {});
        REQUIRE_FALSE(def.exception.IsValid());
        CHECK(require_string(on(def.value.ref, "toString", "()Ljava/lang/String;")) == "fr_CA");
        CHECK(vm.GetSystemProperty("user.language") == "fr");
        CHECK(vm.GetSystemProperty("user.region") == "CA");
        const auto english_field = linker.FindFieldRecursive(locale_type, "ENGLISH", "Ljava/util/Locale;");
        REQUIRE(english_field.has_value());
        const auto& field = linker.Field(*english_field);
        const VmObjectRef english(linker.Class(field.owner).static_storage[field.slot]);
        CHECK(require_string(on(english, "toString", "()Ljava/lang/String;")) == "en");
        for (const auto [input, expected] : {std::pair{"he", "iw"}, {"id", "in"}, {"yi", "ji"}}) {
            const auto value = make(input);
            const auto root = vm.ProtectReferences(std::array{value});
            CHECK(require_string(on(value, "getLanguage", "()Ljava/lang/String;")) == expected);
        }
        const auto french_language = make("fr");
        const auto french_language_root = vm.ProtectReferences(std::array{french_language});
        CHECK(require_string(on(french_language, "getISO3Language", "()Ljava/lang/String;")) == "fra");
        const auto french = make("fr", "FR");
        const auto french_root = vm.ProtectReferences(std::array{french});
        CHECK(require_string(on(french, "getISO3Language", "()Ljava/lang/String;")) == "fra");
        CHECK(require_string(on(french, "getISO3Country", "()Ljava/lang/String;")) == "FRA");
        const auto cloned = on(french, "clone", "()Ljava/lang/Object;");
        REQUIRE_FALSE(cloned.exception.IsValid());
        const auto clone_root = vm.ProtectReferences(std::array{cloned.value.ref});
        CHECK(cloned.value.ref != french);
        CHECK(on(french, "equals", "(Ljava/lang/Object;)Z", {cloned.value}).value.AsInt() == 1);
        CHECK(on(french, "hashCode", "()I").value.AsInt() == on(cloned.value.ref, "hashCode", "()I").value.AsInt());
        CHECK(require_string(on(french, "getDisplayLanguage", "(Ljava/util/Locale;)Ljava/lang/String;",
                                {VmValue::Ref(english)})) == "French");
        CHECK(require_string(on(french, "getDisplayCountry", "(Ljava/util/Locale;)Ljava/lang/String;",
                                {VmValue::Ref(english)})) == "France");
        CHECK(require_string(on(french, "getDisplayName", "(Ljava/util/Locale;)Ljava/lang/String;",
                                {VmValue::Ref(english)})) == "French (France)");
        const auto variant = make("en", "US", "POSIX");
        const auto variant_root = vm.ProtectReferences(std::array{variant});
        CHECK_FALSE(require_string(on(variant, "getDisplayVariant", "(Ljava/util/Locale;)Ljava/lang/String;",
                                      {VmValue::Ref(english)}))
                        .empty());
        const auto empty = make("");
        const auto empty_root = vm.ProtectReferences(std::array{empty});
        CHECK(require_string(on(empty, "getISO3Language", "()Ljava/lang/String;")).empty());
        CHECK(require_string(on(empty, "getISO3Country", "()Ljava/lang/String;")).empty());
        const auto unknown = make("zz", "ZZ");
        const auto unknown_root = vm.ProtectReferences(std::array{unknown});
        expect_exception(on(unknown, "getISO3Language", "()Ljava/lang/String;"),
                         "Ljava/util/MissingResourceException;");
        expect_exception(on(unknown, "getISO3Country", "()Ljava/lang/String;"), "Ljava/util/MissingResourceException;");
        CHECK(require_string(on(unknown, "getDisplayLanguage", "(Ljava/util/Locale;)Ljava/lang/String;",
                                {VmValue::Ref(english)})) == "zz");
        const auto available = direct("Ljava/util/Locale;", "getAvailableLocales", "()[Ljava/util/Locale;", {});
        REQUIRE_FALSE(available.exception.IsValid());
        const auto available_root = vm.ProtectReferences(std::array{available.value.ref});
        REQUIRE(vm.Model().ArrayLength(available.value.ref) > 0);
        vm.Model().SetObjectElement(available.value.ref, 0, VmObjectRef{});
        const auto second = direct("Ljava/util/Locale;", "getAvailableLocales", "()[Ljava/util/Locale;", {});
        REQUIRE_FALSE(second.exception.IsValid());
        CHECK(second.value.ref != available.value.ref);
        CHECK(vm.Model().GetObjectElement(second.value.ref, 0).IsValid());
        // DVM-113: ISO tables remain cached in Java and each public result is cloned.
        for (const auto* name : {"getISOLanguages", "getISOCountries"}) {
            const auto first = direct("Ljava/util/Locale;", name, "()[Ljava/lang/String;", {});
            REQUIRE_FALSE(first.exception.IsValid());
            const auto first_root = vm.ProtectReferences(std::array{first.value.ref});
            CHECK(vm.Model().ArrayLength(first.value.ref) == (std::string_view(name) == "getISOLanguages" ? 559 : 249));
            vm.Model().SetObjectElement(first.value.ref, 0, VmObjectRef{});
            const auto next = direct("Ljava/util/Locale;", name, "()[Ljava/lang/String;", {});
            REQUIRE_FALSE(next.exception.IsValid());
            CHECK(next.value.ref != first.value.ref);
            CHECK(vm.Model().GetObjectElement(next.value.ref, 0).IsValid());
        }
        const auto changed =
            direct("Ljava/util/Locale;", "setDefault", "(Ljava/util/Locale;)V", {VmValue::Ref(french)});
        REQUIRE_FALSE(changed.exception.IsValid());
        CHECK(direct("Ljava/util/Locale;", "getDefault", "()Ljava/util/Locale;", {}).value.ref == french);
        // Configuration's existing layout-direction boundary supports en/zh.
        REQUIRE_FALSE(direct("Ljava/util/Locale;", "setDefault", "(Ljava/util/Locale;)V", {VmValue::Ref(english)})
                          .exception.IsValid());
        const auto resources = vm.NewIntrinsicInstance("Landroid/content/res/Resources;");
        const auto resources_root = vm.ProtectReferences(std::array{resources});
        const auto config = on(resources, "getConfiguration", "()Landroid/content/res/Configuration;");
        REQUIRE_FALSE(config.exception.IsValid());
        const auto config_root = vm.ProtectReferences(std::array{config.value.ref});
        const auto locale_field =
            linker.FindFieldRecursive(vm.Model().ObjectClass(config.value.ref), "locale", "Ljava/util/Locale;");
        REQUIRE(locale_field.has_value());
        CHECK(vm.Model().InstanceSlots(config.value.ref)[linker.Field(*locale_field).slot].bits == english.Value());
        CHECK(require_string(on(vm.NewStringUtf8("Ä"), "toLowerCase", "(Ljava/util/Locale;)Ljava/lang/String;",
                                {VmValue::Ref(english)})) == "ä");
        const auto turkish = make("tr");
        const auto turkish_root = vm.ProtectReferences(std::array{turkish});
        CHECK(require_string(on(vm.NewStringUtf8("I"), "toLowerCase", "(Ljava/util/Locale;)Ljava/lang/String;",
                                {VmValue::Ref(turkish)})) == "ı");
        const auto chinese_field = linker.FindFieldRecursive(locale_type, "CHINESE", "Ljava/util/Locale;");
        REQUIRE(chinese_field.has_value());
        const auto& cf = linker.Field(*chinese_field);
        const VmObjectRef chinese(linker.Class(cf.owner).static_storage[cf.slot]);
        REQUIRE_FALSE(direct("Ljava/util/Locale;", "setDefault", "(Ljava/util/Locale;)V", {VmValue::Ref(chinese)})
                          .exception.IsValid());
        REQUIRE_FALSE(on(resources, "getConfiguration", "()Landroid/content/res/Configuration;").exception.IsValid());
        CHECK(vm.Model().InstanceSlots(config.value.ref)[linker.Field(*locale_field).slot].bits == chinese.Value());
        static_cast<void>(f.app->Stop());
    }
}
