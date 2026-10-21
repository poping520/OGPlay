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
    explicit IcuVm(InterpreterBackend backend) {
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
        REQUIRE_MESSAGE(!outcome.exception.IsValid(), (outcome.exception.IsValid() ? std::string(Vm().Linker().Class(outcome.exception_class).descriptor) + ": " + outcome.exception_message : ""));
        return outcome.value;
    }
    VmValue Static(const char* owner, const char* name, const char* descriptor,
                   std::vector<VmValue> args = {}) {
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
    VmValue On(VmObjectRef receiver, const char* name, const char* descriptor,
               std::vector<VmValue> args = {}) { return Checked(Invoke(receiver, name, descriptor, std::move(args))); }
    VmObjectRef Text(const char* text) { return Vm().NewStringUtf8(text); }
    void Construct(VmObjectRef result, const char* owner, const char* descriptor, std::vector<VmValue> args = {}) {
        const auto root = Vm().ProtectReferences(std::array{result});
        args.insert(args.begin(), VmValue::Ref(result));
        Static(owner, "<init>", descriptor, std::move(args));
    }
    VmObjectRef New(const char* owner, const char* descriptor, std::vector<VmValue> args = {}) {
        std::vector<VmObjectRef> refs;
        for (const auto& value : args) if (value.kind == VmValue::Kind::ref) refs.push_back(value.ref);
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
                      {VmValue::Ref(Text(regex))}).ref;
    }
    VmObjectRef Matcher(VmObjectRef pattern, const char* text) {
        return On(pattern,"matcher","(Ljava/lang/CharSequence;)Ljava/util/regex/Matcher;",{VmValue::Ref(Text(text))}).ref;
    }
};
}

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
        const auto formatter = f.New("Ljava/text/DecimalFormat;", "(Ljava/lang/String;)V", {VmValue::Ref(f.Text("#,##0.00"))});
        const auto formatter_root = vm.ProtectReferences(std::array{formatter});
        CHECK(f.String(f.On(formatter,"format","(D)Ljava/lang/String;",{VmValue::Double(1234.5)})) == "1,234.50");
        CHECK(f.String(f.On(formatter,"format","(D)Ljava/lang/String;",{VmValue::Double(-0.25)})) == "-0.25");
        CHECK(f.String(f.On(formatter,"format","(D)Ljava/lang/String;",{VmValue::Double(std::numeric_limits<double>::infinity())})) == "∞");
        CHECK(f.String(f.On(formatter,"format","(D)Ljava/lang/String;",{VmValue::Double(std::numeric_limits<double>::quiet_NaN())})) == "NaN");
        const auto big = f.New("Ljava/math/BigInteger;","(Ljava/lang/String;)V",{VmValue::Ref(f.Text("123456789012345678901234567890"))});
        const auto big_root = vm.ProtectReferences(std::array{big});
        CHECK(f.String(f.On(formatter,"format","(Ljava/lang/Object;)Ljava/lang/String;",{VmValue::Ref(big)})) == "123,456,789,012,345,678,901,234,567,890.00");
        const auto buffer = f.New("Ljava/lang/StringBuffer;","()V");
        const auto buffer_root = vm.ProtectReferences(std::array{buffer});
        const auto position = f.New("Ljava/text/FieldPosition;","(I)V",{VmValue::Int(1)});
        const auto position_root = vm.ProtectReferences(std::array{position});
        f.On(formatter,"format","(DLjava/lang/StringBuffer;Ljava/text/FieldPosition;)Ljava/lang/StringBuffer;",
             {VmValue::Double(1234.5),VmValue::Ref(buffer),VmValue::Ref(position)});
        CHECK(f.On(position,"getBeginIndex","()I").AsInt() == 6);
        CHECK(f.On(position,"getEndIndex","()I").AsInt() == 8);
        const auto france = f.New("Ljava/util/Locale;","(Ljava/lang/String;Ljava/lang/String;)V",{VmValue::Ref(f.Text("fr")),VmValue::Ref(f.Text("FR"))});
        const auto france_root = vm.ProtectReferences(std::array{france});
        const auto number = f.Static("Ljava/text/NumberFormat;","getInstance","(Ljava/util/Locale;)Ljava/text/NumberFormat;",{VmValue::Ref(france)}).ref;
        const auto number_root = vm.ProtectReferences(std::array{number});
        CHECK(f.String(f.On(number,"format","(D)Ljava/lang/String;",{VmValue::Double(1234.5)})) == "1 234,5");
        const auto date = f.Static("Ljava/text/DateFormat;","getDateInstance","(ILjava/util/Locale;)Ljava/text/DateFormat;",{VmValue::Int(0),VmValue::Ref(france)}).ref;
        const auto date_root = vm.ProtectReferences(std::array{date});
        const auto value = f.New("Ljava/util/Date;","(J)V",{VmValue::Long(1325462400000LL)});
        const auto value_root = vm.ProtectReferences(std::array{value});
        CHECK(f.String(f.On(date,"format","(Ljava/util/Date;)Ljava/lang/String;",{VmValue::Ref(value)})) == "lundi 2 janvier 2012");
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
            const auto matcher = f.On(pattern,"matcher","(Ljava/lang/CharSequence;)Ljava/util/regex/Matcher;",{VmValue::Ref(vm.Model().NewString(u"😀éé𝟙!"))}).ref;
            const auto matcher_root = vm.ProtectReferences(std::array{matcher});
            CHECK(f.On(matcher,"groupCount","()I").AsInt() == 2);
            CHECK(f.On(matcher,"find","()Z").AsInt() == 1);
            CHECK(f.On(matcher,"start","()I").AsInt() == 2);
            CHECK(f.On(matcher,"end","()I").AsInt() == 6);
            CHECK(f.String(f.On(matcher,"group","(I)Ljava/lang/String;",{VmValue::Int(1)})) == "éé");
            static_cast<void>(vm.CollectGarbage("live-regex-owner"));
            f.On(matcher,"reset","(Ljava/lang/CharSequence;)Ljava/util/regex/Matcher;",{VmValue::Ref(f.Text("xx"))});
            CHECK(f.On(matcher,"find","()Z").AsInt() == 1);
            CHECK_FALSE(f.On(matcher,"group","(I)Ljava/lang/String;",{VmValue::Int(2)}).ref.IsValid());
            CHECK(f.On(matcher,"start","(I)I",{VmValue::Int(2)}).AsInt() == -1);
        }
        {
            stage = "transparent";
            const auto p = f.Pattern("a(?=Z)");
            const auto root = vm.ProtectReferences(std::array{p});
            const auto m = f.Matcher(p,"xaZ");
            const auto mr = vm.ProtectReferences(std::array{m});
            f.On(m,"region","(II)Ljava/util/regex/Matcher;",{VmValue::Int(1),VmValue::Int(2)});
            CHECK(f.On(m,"regionStart","()I").AsInt() == 1);
            CHECK(f.On(m,"regionEnd","()I").AsInt() == 2);
            CHECK(f.On(m,"hasTransparentBounds","()Z").AsInt() == 0);
            CHECK(f.On(m,"matches","()Z").AsInt() == 0);
            f.On(m,"useTransparentBounds","(Z)Ljava/util/regex/Matcher;",{VmValue::Int(1)});
            CHECK(f.On(m,"matches","()Z").AsInt() == 1);
        }
        {
            stage = "anchoring";
            const auto p = f.Pattern("^a$");
            const auto root = vm.ProtectReferences(std::array{p});
            const auto m = f.Matcher(p,"xaZ");
            const auto mr = vm.ProtectReferences(std::array{m});
            f.On(m,"region","(II)Ljava/util/regex/Matcher;",{VmValue::Int(1),VmValue::Int(2)});
            CHECK(f.On(m,"matches","()Z").AsInt() == 1);
            f.On(m,"useAnchoringBounds","(Z)Ljava/util/regex/Matcher;",{VmValue::Int(0)});
            CHECK(f.On(m,"matches","()Z").AsInt() == 0);
        }
        {
            stage = "empty";
            const auto p = f.Pattern("");
            const auto root = vm.ProtectReferences(std::array{p});
            const auto m = f.On(p,"matcher","(Ljava/lang/CharSequence;)Ljava/util/regex/Matcher;",{VmValue::Ref(vm.Model().NewString(u"😀a"))}).ref;
            const auto mr = vm.ProtectReferences(std::array{m});
            for (int expected : {0,2,3}) {
                CHECK(f.On(m,"find","()Z").AsInt() == 1);
                CHECK(f.On(m,"start","()I").AsInt() == expected);
            }
            CHECK(f.On(m,"find","()Z").AsInt() == 0);
            CHECK(f.On(m,"hitEnd","()Z").AsInt() == 1);
        }
        stage = "bad syntax";
        const auto bad = vm.Linker().FindDirectMethod(vm.Linker().ResolveDescriptor("Ljava/util/regex/Pattern;"),"compile","(Ljava/lang/String;)Ljava/util/regex/Pattern;");
        REQUIRE(bad);
        const auto error = vm.Call(*bad,std::array{VmValue::Ref(f.Text("[abc"))});
        REQUIRE(error.exception.IsValid());
        const auto eroot = vm.ProtectReferences(std::array{error.exception});
        CHECK(vm.Linker().Class(error.exception_class).descriptor == "Ljava/util/regex/PatternSyntaxException;");
        CHECK(f.String(f.On(error.exception,"getPattern","()Ljava/lang/String;")) == "[abc");
        CHECK(f.On(error.exception,"getIndex","()I").AsInt() >= 0);
        CHECK(f.String(f.On(f.Text("xxaaay"),"replaceAll","(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;",
             {VmValue::Ref(f.Text("a+")),VmValue::Ref(f.Text("_"))})) == "xx_y");
        const auto before = vm.GuestNativeResourceCount();
        static_cast<void>(vm.CollectGarbage("regex-peers-reclaimed"));
        CHECK(vm.GuestNativeResourceCount() < before);
        CHECK(f.ledger.Unimplemented().empty());
    }
}

TEST_CASE("WU7-6 charset enumeration exposes only supported ICU codecs in an immutable case insensitive map") {
    for (auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        IcuVm f(backend);
        auto& vm = f.Vm();
        const auto map = f.Static("Ljava/nio/charset/Charset;","availableCharsets","()Ljava/util/SortedMap;").ref;
        const auto root = vm.ProtectReferences(std::array{map});
        CHECK(f.On(map,"size","()I").AsInt() == 6);
        for (const char* name : {"US-ASCII","ISO-8859-1","UTF-8","UTF-16","UTF-16BE","UTF-16LE"}) {
            const auto item = f.On(map,"get","(Ljava/lang/Object;)Ljava/lang/Object;",{VmValue::Ref(f.Text(name))}).ref;
            REQUIRE(item.IsValid());
            CHECK(f.String(f.On(item,"name","()Ljava/lang/String;")) == name);
        }
        CHECK(f.On(map,"containsKey","(Ljava/lang/Object;)Z",{VmValue::Ref(f.Text("utf-8"))}).AsInt() == 1);
        const auto rejected = f.Invoke(map,"put","(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;",{VmValue::Ref(f.Text("fake")),VmValue::Ref(map)});
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
    for (const auto backend : {InterpreterBackend::switch_dispatch,
                               InterpreterBackend::threaded}) {
        CAPTURE(backend == InterpreterBackend::threaded ? "threaded" : "switch");
        IcuVm fixture(backend);
        auto& vm = fixture.Vm();
        auto& linker = vm.Linker();
        const auto util_date =
            linker.ResolveDescriptor("Ljava/util/Date;");
        const auto sql_date =
            linker.ResolveDescriptor("Ljava/sql/Date;");
        const auto sql_time =
            linker.ResolveDescriptor("Ljava/sql/Time;");
        const auto sql_timestamp =
            linker.ResolveDescriptor("Ljava/sql/Timestamp;");
        for (const auto type : {sql_date, sql_time, sql_timestamp}) {
            linker.EnsureClassLinked(type);
            const auto& linked = linker.Class(type);
            CHECK(linked.is_boot_dex);
            CHECK_FALSE(linked.is_intrinsic);
            CHECK(linked.super.has_value());
            CHECK(*linked.super == util_date);
            CHECK(linker.IsAssignable(util_date, type));
            for (const auto method : linked.own_virtual_methods) {
                CHECK(linker.Method(method).kind !=
                      MethodKind::intrinsic);
            }
            for (const auto method : linked.own_direct_methods) {
                CHECK(linker.Method(method).kind !=
                      MethodKind::intrinsic);
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
        CHECK(linker.Class(outcome.exception_class).descriptor ==
              "Ljava/lang/IllegalArgumentException;");

        const auto kept = vm.NewIntrinsicInstance("Ljava/sql/Date;");
        fixture.Construct(kept, "Ljava/sql/Date;", "(J)V",
                          {VmValue::Long(12345)});
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
        const auto timestamp =
            vm.NewIntrinsicInstance("Ljava/sql/Timestamp;");
        fixture.Construct(timestamp, "Ljava/sql/Timestamp;", "(J)V",
                          {VmValue::Long(millis)});
        outcome = fixture.Invoke(timestamp, "getTime", "()J");
        fixture.Checked(outcome);
        CHECK(outcome.value.AsLong() == millis);
        outcome = fixture.Invoke(timestamp, "getNanos", "()I");
        fixture.Checked(outcome);
        CHECK(outcome.value.AsInt() == 123000000);
        fixture.Checked(fixture.Invoke(timestamp, "setNanos", "(I)V",
                                          {VmValue::Int(7)}));
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
        const auto set = [&](VmObjectRef zone) { f.Static("Ljava/util/TimeZone;", "setDefault", "(Ljava/util/TimeZone;)V", {VmValue::Ref(zone)}); };
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
