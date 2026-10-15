#include "ogplay/runtime/integration/bitmap_pixels.h"
#include "runtime/boundary/core/boundary_symbols.h"
#include "ogplay/runtime/dexvm/big_int_runtime.h"
#include <set>
#include "ogplay/runtime/integration/native_activity_runtime.h"
#include "ogplay/runtime/dexvm/nio_runtime.h"
#include "runtime/integration/dexvm_android/shared.h"
#include "../fixtures/bitmap_decode_data.h"
#include <chrono>
#include <atomic>
#include "ogplay/runtime/vfs/sandbox_store.h"
#include "../dexvm/boot_dex.h"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <future>
#include "ogplay/runtime/dexvm/vm_monitors.h"
#include <thread>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

#include <doctest/doctest.h>
#include "ogplay/runtime/dexvm/reflection.h"

#include "ogplay/core/capability_ledger.h"
#include "ogplay/core/encoding.h"
#include "ogplay/core/logger.h"
#include "ogplay/loader/elf.h"
#include "ogplay/runtime/dexvm/access_flags.h"
#include "ogplay/runtime/dexvm/class_loader_facade.h"
#include "ogplay/runtime/integration/dexvm_android.h"
#include "ogplay/runtime/integration/dexvm_bridge.h"
#include "ogplay/runtime/integration/native_library_loader.h"
#include "ogplay/runtime/integration/apk_assets.h"
#include "ogplay/runtime/jni/jni.h"
#include "ogplay/runtime/jni/jni_java_vm.h"
#include "ogplay/runtime/jni/jni_field_store.h"
#include "ogplay/runtime/jni/jni_native_registry.h"
#include "ogplay/runtime/jni/jni_object_array.h"
#include "ogplay/runtime/jni_guest/jni_guest_abi.h"
#include "ogplay/runtime/jni_guest/jni_guest_static_calls.h"
#include "ogplay/runtime/jni_guest/jni_guest_bindings.h"
#include "ogplay/cpu/interpreter.h"
#include "ogplay/memory/bus.h"
#include "ogplay/session/android_app_process.h"
#include "ogplay/session/dex_activity_lifecycle.h"
#include "ogplay/video/fake_video_player.h"

#include "native_library_fixture.h"

using namespace ogplay::test::native_library;

TEST_CASE("DVM-219 pinned tzdata serves real Bionic UTC and Shanghai queries") {
    using namespace ogplay;
    runtime::VirtualFileSystem filesystem;
    core::CapabilityLedger ledger;
    core::Logger logger;
    std::vector<std::vector<std::byte>> contents;
    std::vector<runtime::BionicModuleSource> libraries;
    for (const auto* name : {"libc.so", "libdl.so", "libm.so", "libstdc++.so"}) {
        contents.push_back(ReadPayloadBytes(std::string("lib/") + name));
        libraries.push_back({name, contents.back()});
    }
    session::AndroidAppProcessRequest request;
    request.manifest = AppManifest("fixture.DelayedContentActivity");
    request.system_libraries = libraries;
    request.dex_bytes = ReadDexFixture("application.dex");
    request.boot_dex_bytes = test::ReadBootDex();
    request.context = std::make_shared<runtime::DexVmAndroidContext>();
    request.filesystem = &filesystem;
    request.ledger = &ledger;
    request.logger = &logger;
    request.platform.android_id = "0123456789abcdef";
    request.tzdata = ReadPayloadBytes("zoneinfo/tzdata");
    auto app = session::AndroidAppProcess::Create(std::move(request));
    auto& process = app->NativeProcess();
    CHECK(process.ProcessEnvironmentValue("TZ") == std::optional<std::string>{"UTC"});
    const auto file = filesystem.Open("/system/usr/share/zoneinfo/tzdata", {.read = true});
    filesystem.Close(file);
    CHECK_THROWS_AS(static_cast<void>(filesystem.Open(
        "/system/usr/share/zoneinfo/tzdata", {.write = true})), runtime::VfsError);
    const auto memory = process.GuestMemoryAccess();
    const auto storage = memory.allocate(96);
    std::array<std::byte, 96> bytes{};
    const std::string name = "TZ";
    const std::string zone = "Asia/Shanghai";
    std::memcpy(bytes.data() + 48, name.c_str(), name.size() + 1);
    std::memcpy(bytes.data() + 52, zone.c_str(), zone.size() + 1);
    memory.write(storage, bytes);
    const auto invoke = [&](const char* name, std::array<std::uint32_t, 4> arguments) {
        const auto target = process.FindModuleExport(0, name);
        REQUIRE(target.Value() != 0U);
        return process.Invoke({target, arguments, {}}).return_value;
    };
    const auto word = [&](std::size_t offset) {
        std::array<std::byte, 4> value{};
        memory.read(storage.Add(offset), value);
        std::uint32_t result{};
        for (std::size_t i = 0; i < value.size(); ++i)
            result |= std::to_integer<std::uint32_t>(value[i]) << (8U * i);
        return result;
    };
    CHECK(invoke("localtime_r", {storage.Value(), storage.Add(4).Value(), 0, 0}) == storage.Add(4).Value());
    CHECK(word(12) == 0U); // tm_hour
    CHECK(word(16) == 1U); // tm_mday
    CHECK(word(24) == 70U); // tm_year
    CHECK(invoke("setenv", {storage.Add(48).Value(), storage.Add(52).Value(), 1, 0}) == 0U);
    static_cast<void>(invoke("tzset", {}));
    CHECK(invoke("localtime_r", {storage.Value(), storage.Add(4).Value(), 0, 0}) == storage.Add(4).Value());
    CHECK(word(12) == 8U);
    CHECK(word(40) == 28800U); // tm_gmtoff
    CHECK(logger.Snapshot(core::LogLevel::error, "guest.stderr").empty());
    memory.release(storage, 96);
    static_cast<void>(app->Stop());
}

TEST_CASE("DVM-126 String.format delegates Locale formatting to API19 Formatter") {
    using namespace ogplay;
    using namespace runtime::dexvm;
    for (const auto backend : {InterpreterBackend::switch_dispatch,
                               InterpreterBackend::threaded}) {
        CAPTURE(backend == InterpreterBackend::threaded ? "threaded" : "switch");
        runtime::VirtualFileSystem filesystem;
        core::CapabilityLedger ledger;
        core::Logger logger;
        std::vector<std::vector<std::byte>> contents;
        std::vector<runtime::BionicModuleSource> libraries;
        for (const auto name : {
                 "libc.so", "libm.so", "libdl.so", "libstdc++.so", "libz.so",
                 "libcrypto.so", "libssl.so", "libgabi++.so", "libicui18n.so", "libicuuc.so",
                 "libstlport.so", "libogplay_jni.so"}) {
            std::ifstream stream(std::string(OGPLAY_SOURCE_DIR) +
                                     "/data/android/19/lib/" + name,
                                 std::ios::binary);
            REQUIRE_MESSAGE(stream.good(), name);
            std::vector<char> data{std::istreambuf_iterator<char>(stream), {}};
            contents.emplace_back(data.size());
            std::transform(data.begin(), data.end(), contents.back().begin(),
                           [](const char value) {
                               return static_cast<std::byte>(value);
                           });
            libraries.push_back({name, contents.back()});
        }
        auto context = std::make_shared<runtime::DexVmAndroidContext>();
        context->apk_bytes = {
            std::byte{0x50}, std::byte{0x4b}, std::byte{3}, std::byte{4}};
        session::AndroidAppProcessRequest request;
        request.manifest = AppManifest("fixture.MainActivity");
        request.system_libraries = libraries;
        request.dex_bytes = ReadDexFixture("cipher.dex");
        request.icu_data = ReadPayloadBytes("icu/icudt51l.dat");
        request.boot_dex_bytes = test::ReadBootDex();
        request.context = context;
        request.dexvm.interpreter.backend = backend;
        request.surface_width = 64;
        request.surface_height = 36;
        request.maximum_ticks_per_call = UINT64_C(100000000);
#if defined(_WIN32)
        request.backend = {gles::AngleRenderer::d3d11,
                           gles::AngleDevice::hardware};
#elif defined(__APPLE__)
        request.backend = {gles::AngleRenderer::metal,
                           gles::AngleDevice::hardware};
#else
        request.backend = {gles::AngleRenderer::vulkan,
                           gles::AngleDevice::hardware};
#endif
        request.filesystem = &filesystem;
        request.ledger = &ledger;
        request.logger = &logger;
        auto app = session::AndroidAppProcess::Create(std::move(request));
        auto& vm = app->DexVm().Vm();
        auto& linker = vm.Linker();
        const auto direct = [&](const char* owner, const char* name,
                                const char* descriptor,
                                std::vector<VmValue> arguments) {
            const auto type = linker.ResolveDescriptor(owner);
            const auto method = linker.FindDirectMethod(
                type, name, descriptor);
            REQUIRE_MESSAGE(method.has_value(), name);
            return vm.Call(*method, arguments);
        };
        const auto require_string = [&](const VmCallOutcome& outcome) {
            REQUIRE_MESSAGE(!outcome.exception.IsValid(), outcome.exception_message);
            return vm.StringUtf8(outcome.value.ref);
        };
        const auto expect_exception = [&](const VmCallOutcome& outcome,
                                          const char* descriptor) {
            INFO(outcome.exception_message);
            REQUIRE(outcome.exception.IsValid());
            CHECK(linker.Class(outcome.exception_class).descriptor == descriptor);
        };

        const auto formatter = linker.ResolveDescriptor("Ljava/util/Formatter;");
        CHECK(linker.Class(formatter).is_boot_dex);
        const auto formatter_format = linker.FindVtableIndex(
            formatter, "format",
            "(Ljava/lang/String;[Ljava/lang/Object;)Ljava/util/Formatter;");
        REQUIRE(formatter_format.has_value());
        CHECK(linker.Method(linker.Class(formatter).vtable[*formatter_format]).kind ==
              MethodKind::interpreted);
        const auto string_class = linker.ResolveDescriptor("Ljava/lang/String;");
        const auto locale_overload = linker.FindDirectMethod(
            string_class, "format",
            "(Ljava/util/Locale;Ljava/lang/String;[Ljava/lang/Object;)Ljava/lang/String;");
        REQUIRE(locale_overload.has_value());
        CHECK(linker.Method(*locale_overload).kind == MethodKind::intrinsic);

        const auto locale_class = linker.ResolveDescriptor("Ljava/util/Locale;");
        const auto initialized = vm.EnsureClassInitialized(locale_class);
        REQUIRE_MESSAGE(!initialized.exception.IsValid(), initialized.exception_message);
        const auto us_field = linker.FindFieldRecursive(
            locale_class, "US", "Ljava/util/Locale;");
        REQUIRE(us_field.has_value());
        const auto& linked_us = linker.Field(*us_field);
        const auto us = VmObjectRef(
            linker.Class(linked_us.owner).static_storage[linked_us.slot]);
        REQUIRE(us.IsValid());
        const auto calendar = direct(
            "Ljava/util/Calendar;", "getInstance",
            "()Ljava/util/Calendar;", {}).value.ref;
        REQUIRE(calendar.IsValid());
        const auto object_class = linker.ResolveDescriptor("Ljava/lang/Object;");
        const auto object_array = linker.ResolveDescriptor("[Ljava/lang/Object;");
        const auto zone_arguments = vm.Model().NewObjectArray(
            object_array, object_class, 1);
        vm.Model().SetObjectElement(zone_arguments, 0, calendar);
        const auto zone_roots = vm.ProtectReferences(
            std::array{us, calendar, zone_arguments});
        const auto zone_result = direct(
            "Ljava/lang/String;", "format",
            "(Ljava/util/Locale;Ljava/lang/String;[Ljava/lang/Object;)Ljava/lang/String;",
            {VmValue::Ref(us), VmValue::Ref(vm.NewStringUtf8("%tZ")),
             VmValue::Ref(zone_arguments)});
        CHECK(require_string(zone_result) == "GMT");
        const auto result_roots = vm.ProtectReferences(
            std::array{zone_result.value.ref});
        static_cast<void>(vm.CollectGarbage("formatter-result"));
        CHECK(vm.StringUtf8(zone_result.value.ref) == "GMT");

        const auto integer = vm.NewIntrinsicInstance("Ljava/lang/Integer;");
        vm.Model().InstanceSlots(integer)[0] = {7U, SlotTag::cat1};
        const auto basic_arguments = vm.Model().NewObjectArray(
            object_array, object_class, 2);
        vm.Model().SetObjectElement(basic_arguments, 0, integer);
        vm.Model().SetObjectElement(
            basic_arguments, 1, vm.NewStringUtf8("ok"));
        const auto basic_roots = vm.ProtectReferences(
            std::array{integer, basic_arguments});
        CHECK(require_string(direct(
                  "Ljava/lang/String;", "format",
                  "(Ljava/util/Locale;Ljava/lang/String;[Ljava/lang/Object;)Ljava/lang/String;",
                  {VmValue::Ref(VmObjectRef{}),
                   VmValue::Ref(vm.NewStringUtf8("%02d-%s-%%")),
                   VmValue::Ref(basic_arguments)})) == "07-ok-%");
        CHECK(require_string(direct(
                  "Ljava/lang/String;", "format",
                  "(Ljava/lang/String;[Ljava/lang/Object;)Ljava/lang/String;",
                  {VmValue::Ref(vm.NewStringUtf8("%02d")),
                   VmValue::Ref(basic_arguments)})) == "07");

        expect_exception(direct(
            "Ljava/lang/String;", "format",
            "(Ljava/util/Locale;Ljava/lang/String;[Ljava/lang/Object;)Ljava/lang/String;",
            {VmValue::Ref(us), VmValue::Ref(VmObjectRef{}),
             VmValue::Ref(VmObjectRef{})}),
            "Ljava/lang/NullPointerException;");
        expect_exception(direct(
            "Ljava/lang/String;", "format",
            "(Ljava/util/Locale;Ljava/lang/String;[Ljava/lang/Object;)Ljava/lang/String;",
            {VmValue::Ref(us), VmValue::Ref(vm.NewStringUtf8("%q")),
             VmValue::Ref(VmObjectRef{})}),
            "Ljava/util/UnknownFormatConversionException;");
        const auto empty_arguments = vm.Model().NewObjectArray(
            object_array, object_class, 0);
        expect_exception(direct(
            "Ljava/lang/String;", "format",
            "(Ljava/util/Locale;Ljava/lang/String;[Ljava/lang/Object;)Ljava/lang/String;",
            {VmValue::Ref(us), VmValue::Ref(vm.NewStringUtf8("%d")),
             VmValue::Ref(empty_arguments)}),
            "Ljava/util/MissingFormatArgumentException;");
    }
}

TEST_CASE("DVM-215 API19 decimal JSON uses bounded guest BIGNUM arithmetic") {
    using namespace ogplay;
    using namespace runtime::dexvm;
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        CAPTURE(backend == InterpreterBackend::threaded ? "threaded" : "switch");
        runtime::VirtualFileSystem filesystem;
        core::CapabilityLedger ledger;
        core::Logger logger;
        std::vector<std::vector<std::byte>> contents;
        std::vector<runtime::BionicModuleSource> libraries;
        for (const auto name : {"libc.so", "libm.so", "libdl.so", "libstdc++.so", "libz.so",
                               "libcrypto.so", "libssl.so", "libgabi++.so", "libicui18n.so",
                               "libicuuc.so", "libstlport.so", "libogplay_jni.so"}) {
            contents.push_back(ReadPayloadBytes(std::string("lib/") + name));
            libraries.push_back({name, contents.back()});
        }
        auto context = std::make_shared<runtime::DexVmAndroidContext>();
        context->apk_bytes = {std::byte{0x50}, std::byte{0x4b}, std::byte{3}, std::byte{4}};
        session::AndroidAppProcessRequest request;
        request.manifest = AppManifest("android.app.Activity");
        request.system_libraries = libraries;
        // Minimal APK fixture; math/JSON classes come from the audited BootDex.
        request.dex_bytes = ReadDexFixture("crc32.dex");
        request.icu_data = ReadPayloadBytes("icu/icudt51l.dat");
        request.boot_dex_bytes = test::ReadBootDex();
        request.context = context;
        request.dexvm.interpreter.backend = backend;
        request.surface_width = 64;
        request.surface_height = 36;
        request.maximum_ticks_per_call = UINT64_C(100000000);
#if defined(_WIN32)
        request.backend = {gles::AngleRenderer::d3d11, gles::AngleDevice::hardware};
#elif defined(__APPLE__)
        request.backend = {gles::AngleRenderer::metal, gles::AngleDevice::hardware};
#else
        request.backend = {gles::AngleRenderer::vulkan, gles::AngleDevice::hardware};
#endif
        request.filesystem = &filesystem;
        request.ledger = &ledger;
        request.logger = &logger;
        auto app = session::AndroidAppProcess::Create(std::move(request));
        auto& vm = app->DexVm().Vm();
        auto& linker = vm.Linker();
        const auto direct = [&](const char* owner, const char* name, const char* descriptor,
                                std::vector<VmValue> arguments) {
            const auto method = linker.FindDirectMethod(linker.ResolveDescriptor(owner), name, descriptor);
            REQUIRE(method.has_value());
            return vm.Call(*method, arguments);
        };
        const auto invoke_result = [&](VmObjectRef object, const char* name, const char* descriptor,
                                       std::vector<VmValue> arguments) {
            const auto slot = linker.FindVtableIndex(vm.Model().ObjectClass(object), name, descriptor);
            REQUIRE(slot.has_value());
            arguments.insert(arguments.begin(), VmValue::Ref(object));
            return vm.Call(linker.Class(vm.Model().ObjectClass(object)).vtable[*slot], arguments);
        };
        const auto invoke = [&](VmObjectRef object, const char* name, const char* descriptor,
                                std::vector<VmValue> arguments = {}) {
            INFO((std::string{name} + descriptor));
            const auto outcome = invoke_result(object, name, descriptor, std::move(arguments));
            REQUIRE_MESSAGE(!outcome.exception.IsValid(), outcome.exception_message);
            return outcome.value;
        };
        const auto construct = [&](const char* owner, const char* descriptor = "()V",
                                   std::vector<VmValue> arguments = {}) {
            const auto object = vm.NewIntrinsicInstance(owner);
            const auto root = vm.ProtectReferences(std::array{object});
            arguments.insert(arguments.begin(), VmValue::Ref(object));
            const auto outcome = direct(owner, "<init>", descriptor, std::move(arguments));
            REQUIRE_MESSAGE(!outcome.exception.IsValid(), outcome.exception_message);
            return object;
        };
        const auto byte_array = [&](const std::vector<std::byte>& data) {
            const auto array = vm.Model().NewPrimitiveArray(linker.ResolveDescriptor("[B"),
                runtime::JniPrimitiveKind::byte, static_cast<runtime::JniSize>(data.size()));
            vm.Model().WriteByteRegion(array, 0, data);
            return array;
        };
        const auto expect_exception = [&](const VmCallOutcome& outcome, const char* type) {
            REQUIRE(outcome.exception.IsValid());
            CHECK(linker.Class(outcome.exception_class).descriptor == type);
        };

        // BigDecimal's exponent parsing uses the VM String char-region value boundary.
        const auto chars = vm.Model().NewPrimitiveArray(linker.ResolveDescriptor("[C"),
            runtime::JniPrimitiveKind::character, 4);
        const auto chars_root = vm.ProtectReferences(std::array{chars});
        vm.Model().SetPrimitiveElement(chars, 0, 'x');
        vm.Model().SetPrimitiveElement(chars, 1, 0xd83d);
        vm.Model().SetPrimitiveElement(chars, 2, 0xde00);
        vm.Model().SetPrimitiveElement(chars, 3, 'y');
        const auto region = [&](VmObjectRef array, int offset, int count) {
            return direct("Ljava/lang/String;", "valueOf", "([CII)Ljava/lang/String;",
                          {VmValue::Ref(array),VmValue::Int(offset),VmValue::Int(count)});
        };
        const auto copied = region(chars, 1, 2);
        REQUIRE_FALSE(copied.exception.IsValid());
        const auto copied_root = vm.ProtectReferences(std::array{copied.value.ref});
        vm.Model().SetPrimitiveElement(chars, 1, 'a');
        CHECK(vm.Model().StringValue(copied.value.ref) == u"\U0001f600");
        CHECK(vm.Model().StringValue(region(chars, 4, 0).value.ref).empty());
        expect_exception(region(chars, -1, 1), "Ljava/lang/StringIndexOutOfBoundsException;");
        expect_exception(region(chars, 1, INT32_MAX), "Ljava/lang/StringIndexOutOfBoundsException;");
        expect_exception(region(VmObjectRef{}, 0, 0), "Ljava/lang/NullPointerException;");
        for (const auto owner : {"Ljava/math/Multiplication;", "Ljava/math/BigDecimal$1;"}) {
            const auto type = linker.ResolveDescriptor(owner);
            linker.EnsureClassLinked(type);
            CHECK(linker.Class(type).is_boot_dex);
            CHECK_FALSE(linker.Class(type).is_intrinsic);
        }
        struct Vector { const char* text; std::uint64_t bits; const char* json; };
        // IEEE754 patterns from Python float(text), except the two documented API19 cases.
        const Vector vectors[] = {
            {"0", UINT64_C(0x0000000000000000), "0"},
            {"1", UINT64_C(0x3ff0000000000000), "1"},
            {"-7", UINT64_C(0xc01c000000000000), "-7"},
            {"0.1", UINT64_C(0x3fb999999999999a), "0.1"},
            {"-0.1", UINT64_C(0xbfb999999999999a), "-0.1"},
            {"1.234567890123456789", UINT64_C(0x3ff3c0ca428c59fb), "1.234567890123456789"},
            {"1E-40", UINT64_C(0x37a16c262777579c), "1E-40"},
            {"1E+40", UINT64_C(0x483d6329f1c35ca5), "1E+40"},
            {"1E-308", UINT64_C(0x000730d67819e8d2), "1E-308"},
            {"4.9E-324", UINT64_C(0x0000000000000001), "4.9E-324"},
            {"1E-325", UINT64_C(0x0000000000000000), "0"},
            {"9007199254740993", UINT64_C(0x4340000000000000), "9007199254740993"},
            {"18446744073709551616", UINT64_C(0x43f0000000000000), "18446744073709551616"},
            {"-9223372036854775809", UINT64_C(0xc3e0000000000000), "-9223372036854775809"},
            {"123.4500", UINT64_C(0x405edccccccccccd), "123.4500"},
            // API19 BigDecimal discards the source sign for zero.
            {"-0.0", UINT64_C(0x0000000000000000), "0"},
            {"1.00000000000000011102230246251565404236316680908203125", UINT64_C(0x3ff0000000000000), "1"},
            // Pinned API19 source on an independent JVM/JDK integer backend yields 1.0 here.
            {"1.00000000000000011102230246251565404236316680908203126", UINT64_C(0x3ff0000000000000), "1"},
        };
        for (const auto& v : vectors) {
            CAPTURE(v.text);
            const auto decimal = construct("Ljava/math/BigDecimal;", "(Ljava/lang/String;)V",
                                           {VmValue::Ref(vm.NewStringUtf8(v.text))});
            const auto decimal_root = vm.ProtectReferences(std::array{decimal});
            static_cast<void>(vm.CollectGarbage("decimal-json-live-value"));
            CHECK(std::bit_cast<std::uint64_t>(invoke(decimal, "doubleValue", "()D").AsDouble()) == v.bits);
            const auto number = direct("Lorg/json/JSONObject;", "numberToString",
                "(Ljava/lang/Number;)Ljava/lang/String;", {VmValue::Ref(decimal)});
            REQUIRE_MESSAGE(!number.exception.IsValid(), number.exception_message);
            CHECK(vm.StringUtf8(number.value.ref) == v.json);
            const auto object = construct("Lorg/json/JSONObject;");
            const auto object_root = vm.ProtectReferences(std::array{object});
            invoke(object, "put", "(Ljava/lang/String;Ljava/lang/Object;)Lorg/json/JSONObject;",
                   {VmValue::Ref(vm.NewStringUtf8("metric")), VmValue::Ref(decimal)});
            CHECK(vm.StringUtf8(invoke(object, "toString", "()Ljava/lang/String;").ref) ==
                  std::string("{\"metric\":") + v.json + "}");
        }
        const auto bn = [&](const char* name, const char* descriptor, std::vector<VmValue> arguments) {
            return direct("Ljava/math/NativeBN;", name, descriptor, std::move(arguments));
        };
        const auto success = [&](const char* name, const char* descriptor, std::vector<VmValue> arguments) {
            const auto outcome = bn(name, descriptor, std::move(arguments));
            REQUIRE_MESSAGE(!outcome.exception.IsValid(), outcome.exception_message);
        };
        const auto new_number = [&](const char* decimal) {
            const auto token = vm.BigInts().New();
            vm.BigInts().Require(token).SetDecimal(decimal);
            return VmValue::Long(static_cast<std::int64_t>(token));
        };
        const auto value = [&](VmValue token) { return vm.BigInts().Require(static_cast<std::uint64_t>(token.AsLong())).String(10); };
        const auto a = new_number("-12345678901234567890"), b = new_number("123456789"),
                   q = new_number("99"), r = new_number("88"), zero = new_number("0");
        success("BN_div", "(JJJJ)V", {q,r,a,b});
        CHECK(value(q) == "-100000000010"); CHECK(value(r) == "0");
        vm.BigInts().Require(static_cast<std::uint64_t>(a.AsLong())).SetDecimal("-100");
        vm.BigInts().Require(static_cast<std::uint64_t>(b.AsLong())).SetDecimal("7");
        success("BN_div", "(JJJJ)V", {a,b,a,b}); // Both destinations alias inputs, which were snapshotted.
        CHECK(value(a) == "-14"); CHECK(value(b) == "-2");
        success("BN_div", "(JJJJ)V", {VmValue::Long(0),r,a,b}); CHECK(value(r) == "0");
        success("BN_add", "(JJJ)V", {a,a,b}); CHECK(value(a) == "-16");
        success("BN_mul", "(JJJ)V", {a,a,b}); CHECK(value(a) == "32");
        success("BN_mul_word", "(JI)V", {a,VmValue::Int(-1)}); CHECK(value(a) == "137438953440");
        success("BN_shift", "(JJI)V", {a,a,VmValue::Int(65)}); CHECK(value(a) == "5070602399732325985269401518080");
        success("BN_shift", "(JJI)V", {a,a,VmValue::Int(-65)}); CHECK(value(a) == "137438953440");
        success("BN_shift", "(JJI)V", {a,a,VmValue::Int(INT32_MIN)}); CHECK(value(a) == "0");
        success("BN_mul", "(JJJ)V", {a,a,b}); CHECK(value(a) == "0");
        success("BN_shift", "(JJI)V", {a,a,VmValue::Int(INT32_MAX)}); CHECK(value(a) == "0");
        CHECK_FALSE(vm.BigInts().Require(static_cast<std::uint64_t>(a.AsLong())).negative);
        expect_exception(bn("BN_div", "(JJJJ)V", {q,r,b,zero}), "Ljava/lang/ArithmeticException;");
        CHECK(value(q) == "-100000000010"); CHECK(value(r) == "0");
        expect_exception(bn("BN_div", "(JJJJ)V", {q,VmValue::Long(INT64_MAX),b,a}), "Ljava/lang/IllegalStateException;");
        CHECK(value(q) == "-100000000010");
        expect_exception(bn("BN_div", "(JJJJ)V", {q,q,b,b}), "Ljava/lang/IllegalArgumentException;");
        success("BN_exp", "(JJJ)V", {b,b,b}); // API19 BN_exp ignores exponent sign: (-2)^abs(-2).
        CHECK(value(b) == "4");
        success("BN_exp", "(JJJ)V", {q,b,zero}); CHECK(value(q) == "1");
        const auto huge = new_number("2147483647");
        expect_exception(bn("BN_exp", "(JJJ)V", {q,b,huge}), "Ljava/lang/UnsupportedOperationException;");
        CHECK(value(q) == "1");
        expect_exception(bn("BN_shift", "(JJI)V", {q,b,VmValue::Int(INT32_MAX)}), "Ljava/lang/UnsupportedOperationException;");
        CHECK(value(q) == "1");
        // Work rejection happens before guest OpenSSL computation; values fit the codec limit.
        success("BN_shift", "(JJI)V", {r,b,VmValue::Int(131072)});
        expect_exception(bn("BN_mul", "(JJJ)V", {q,r,r}), "Ljava/lang/UnsupportedOperationException;");
        CHECK(value(q) == "1");
        expect_exception(bn("BN_gcd", "(JJJ)V", {q,b,b}), "Ljava/lang/UnsatisfiedLinkError;");
        const auto minus_one = new_number("-1"), wide_exponent = new_number("18446744073709551617"),
                   minus_three = new_number("-3"), ten = new_number("10"), many = new_number("10000");
        success("BN_exp", "(JJJ)V", {q,minus_one,wide_exponent}); CHECK(value(q) == "-1");
        success("BN_exp", "(JJJ)V", {q,zero,zero}); CHECK(value(q) == "1");
        success("BN_exp", "(JJJ)V", {q,minus_three,minus_three}); CHECK(value(q) == "-27");
        success("BN_shift", "(JJI)V", {q,minus_three,VmValue::Int(-1)}); CHECK(value(q) == "-1");
        expect_exception(bn("BN_exp", "(JJJ)V", {q,ten,many}), "Ljava/lang/UnsupportedOperationException;");
        CHECK(value(q) == "-1");
        const auto oversized = byte_array(std::vector<std::byte>(1048578));
        const auto oversized_root = vm.ProtectReferences(std::array{oversized});
        expect_exception(direct("Lorg/ogplay/math/NativeBigInteger;", "shift", "([BI)[B",
                                {VmValue::Ref(oversized),VmValue::Int(1)}), "Ljava/lang/UnsupportedOperationException;");
        const auto hits = ledger.Unimplemented();
        CHECK(std::any_of(hits.begin(), hits.end(), [](const auto& hit) {
            return hit.id == "dexvm.native_bn_arithmetic.budget" && hit.count >= 4;
        }));
        const auto stale = new_number("1"); vm.BigInts().Free(static_cast<std::uint64_t>(stale.AsLong()));
        expect_exception(bn("BN_add", "(JJJ)V", {q,stale,b}), "Ljava/lang/IllegalStateException;");
        const auto native = [&](const char* name, const char* descriptor, std::vector<VmValue> args) {
            return direct("Lorg/ogplay/math/NativeBigInteger;", name, descriptor, std::move(args));
        };
        const auto missing_sign = byte_array({});
        const auto missing_sign_root = vm.ProtectReferences(std::array{missing_sign});
        expect_exception(native("shift", "([BI)[B", {VmValue::Ref(missing_sign),VmValue::Int(1)}),
                         "Ljava/lang/IllegalArgumentException;");
        const auto wrong_sign = byte_array({std::byte{2},std::byte{1}});
        const auto wrong_sign_root = vm.ProtectReferences(std::array{wrong_sign});
        expect_exception(native("shift", "([BI)[B", {VmValue::Ref(wrong_sign),VmValue::Int(1)}),
                         "Ljava/lang/IllegalArgumentException;");
        expect_exception(native("shift", "([BI)[B", {VmValue::Ref(VmObjectRef{}),VmValue::Int(1)}),
                         "Ljava/lang/NullPointerException;");
        for (const auto t : {a,b,q,r,zero,huge,minus_one,wide_exponent,minus_three,ten,many}) vm.BigInts().Free(static_cast<std::uint64_t>(t.AsLong()));
        static_cast<void>(vm.CollectGarbage("decimal-json-dead-values"));
        // Original Java cache roots remain live through collection.
        const auto again = construct("Ljava/math/BigDecimal;", "(Ljava/lang/String;)V",
                                    {VmValue::Ref(vm.NewStringUtf8("0.1"))});
        const auto again_root = vm.ProtectReferences(std::array{again});
        CHECK(std::bit_cast<std::uint64_t>(invoke(again, "doubleValue", "()D").AsDouble()) == UINT64_C(0x3fb999999999999a));
        // Scope teardown releases VM-owned tokens; native temporaries never persist.
    }
}
