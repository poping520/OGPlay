#include "ogplay/runtime/integration/bitmap_pixels.h"
#include "runtime/boundary/core/boundary_symbols.h"
#include "ogplay/runtime/dexvm/big_int_runtime.h"
#include <set>
#include "ogplay/runtime/integration/native_activity_runtime.h"
#include "ogplay/runtime/dexvm/nio_runtime.h"
#include "ogplay/runtime/dexvm/io_runtime.h"
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

TEST_CASE("DVM-201 CRC32 uses BootDex state and real guest zlib on both backends") {
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
            const auto type = vm.Model().ObjectClass(object);
            const auto slot = linker.FindVtableIndex(type, name, descriptor);
            REQUIRE(slot.has_value());
            arguments.insert(arguments.begin(), VmValue::Ref(object));
            return vm.Call(linker.Class(type).vtable[*slot], arguments);
        };
        const auto invoke = [&](VmObjectRef object, const char* name, const char* descriptor,
                                std::vector<VmValue> arguments = {}) {
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
        const auto expect_exception = [&](const VmCallOutcome& result, const char* type) {
            REQUIRE(result.exception.IsValid());
            CHECK(linker.Class(result.exception_class).descriptor == type);
        };
        constexpr auto owner = "Ljava/util/zip/CRC32;";
        const auto type = linker.ResolveDescriptor(owner);
        linker.EnsureClassLinked(type);
        CHECK(linker.Class(type).is_boot_dex);
        CHECK_FALSE(linker.Class(type).is_intrinsic);
        CHECK(linker.IsAssignable(linker.ResolveDescriptor("Ljava/util/zip/Checksum;"), type));
        for (const auto method : linker.Class(type).own_virtual_methods)
            CHECK(linker.Method(method).kind == MethodKind::interpreted);
        for (const auto method : linker.Class(type).own_direct_methods)
            CHECK(linker.Method(method).kind != MethodKind::intrinsic);

        const auto crc = construct(owner);
        const auto crc_root = vm.ProtectReferences(std::array{crc});
        const auto other = construct(owner);
        const auto other_root = vm.ProtectReferences(std::array{other});
        const std::vector<std::byte> digits{std::byte{'1'}, std::byte{'2'}, std::byte{'3'},
            std::byte{'4'}, std::byte{'5'}, std::byte{'6'}, std::byte{'7'}, std::byte{'8'}, std::byte{'9'}};
        const auto input = byte_array(digits);
        const auto input_root = vm.ProtectReferences(std::array{input});
        CHECK(invoke(crc, "getValue", "()J").AsLong() == 0);
        invoke(crc, "update", "([B)V", {VmValue::Ref(input)});
        CHECK(invoke(crc, "getValue", "()J").AsLong() == INT64_C(0xcbf43926));
        const auto empty = byte_array({});
        const auto empty_root = vm.ProtectReferences(std::array{empty});
        invoke(crc, "update", "([B)V", {VmValue::Ref(empty)});
        invoke(crc, "update", "([BII)V", {VmValue::Ref(input), VmValue::Int(9), VmValue::Int(0)});
        CHECK(invoke(crc, "getValue", "()J").AsLong() == INT64_C(0xcbf43926));
        CHECK(invoke(other, "getValue", "()J").AsLong() == 0);
        for (const auto byte : digits)
            invoke(other, "update", "(I)V", {VmValue::Int(std::to_integer<int>(byte))});
        CHECK(invoke(other, "getValue", "()J").AsLong() == INT64_C(0xcbf43926));
        invoke(crc, "reset", "()V");
        CHECK(invoke(crc, "getValue", "()J").AsLong() == 0);
        invoke(crc, "update", "([BII)V", {VmValue::Ref(input), VmValue::Int(0), VmValue::Int(4)});
        static_cast<void>(vm.CollectGarbage("crc32-incremental-state"));
        invoke(crc, "update", "([BII)V", {VmValue::Ref(input), VmValue::Int(4), VmValue::Int(5)});
        CHECK(invoke(crc, "getValue", "()J").AsLong() == INT64_C(0xcbf43926));

        expect_exception(invoke_result(crc, "update", "([B)V", {VmValue::Ref(VmObjectRef{})}),
                         "Ljava/lang/NullPointerException;");
        expect_exception(invoke_result(crc, "update", "([BII)V",
            {VmValue::Ref(VmObjectRef{}), VmValue::Int(0), VmValue::Int(0)}), "Ljava/lang/NullPointerException;");
        for (const auto& [offset, count] : std::array<std::pair<int, int>, 5>{
                 {{-1, 1}, {0, -1}, {8, 2}, {10, 0}, {1, INT32_MAX}}}) {
            expect_exception(invoke_result(crc, "update", "([BII)V",
                {VmValue::Ref(input), VmValue::Int(offset), VmValue::Int(count)}),
                "Ljava/lang/ArrayIndexOutOfBoundsException;");
        }
        // Also exercise the native guard independently of the Java parameter checks.
        const auto native_failure = [&](VmObjectRef array, int offset, int count, const char* expected) {
            const auto result = direct(owner, "updateImpl", "([BIIJ)J",
                {VmValue::Ref(crc), VmValue::Ref(array), VmValue::Int(offset), VmValue::Int(count), VmValue::Long(0)});
            expect_exception(result, expected);
        };
        native_failure(input, 8, 2, "Ljava/lang/ArrayIndexOutOfBoundsException;");
        native_failure(VmObjectRef{}, 0, 0, "Ljava/lang/NullPointerException;");
        CHECK(invoke(crc, "getValue", "()J").AsLong() == INT64_C(0xcbf43926));
        for (const auto value : {-1, 255, 511}) {
            invoke(crc, "reset", "()V");
            invoke(crc, "update", "(I)V", {VmValue::Int(value)});
            CHECK(invoke(crc, "getValue", "()J").AsLong() == INT64_C(0xff000000));
        }
        // Independent host-zlib vector crosses three 4096-byte JNI chunks plus a tail.
        std::vector<std::byte> large(12289);
        for (std::size_t i = 0; i < large.size(); ++i) large[i] = static_cast<std::byte>(i & 255U);
        const auto large_input = byte_array(large);
        const auto large_root = vm.ProtectReferences(std::array{large_input});
        invoke(crc, "reset", "()V");
        invoke(crc, "update", "([B)V", {VmValue::Ref(large_input)});
        CHECK(invoke(crc, "getValue", "()J").AsLong() == INT64_C(0x8ce380a7));
        CHECK(invoke(other, "getValue", "()J").AsLong() == INT64_C(0xcbf43926));

        const auto digest = construct("Lfixture/CrcDigest;");
        const auto digest_root = vm.ProtectReferences(std::array{digest});
        const auto output = construct("Ljava/io/ByteArrayOutputStream;");
        const auto output_root = vm.ProtectReferences(std::array{output});
        const auto stream = construct("Ljava/security/DigestOutputStream;",
            "(Ljava/io/OutputStream;Ljava/security/MessageDigest;)V", {VmValue::Ref(output), VmValue::Ref(digest)});
        const auto stream_root = vm.ProtectReferences(std::array{stream});
        invoke(stream, "write", "(I)V", {VmValue::Int('1')});
        invoke(stream, "write", "([BII)V", {VmValue::Ref(input), VmValue::Int(1), VmValue::Int(8)});
        const auto sum = invoke(digest, "digest", "()[B").ref;
        CHECK(vm.Model().ReadByteRegion(sum, 0, 4) ==
            std::vector<std::byte>{std::byte{0xcb}, std::byte{0xf4}, std::byte{0x39}, std::byte{0x26}});
        const auto written = invoke(output, "toByteArray", "()[B").ref;
        CHECK(vm.Model().ReadByteRegion(written, 0, 9) == digits);
        invoke(digest, "reset", "()V");
        const auto reset = invoke(digest, "digest", "()[B").ref;
        CHECK(vm.Model().ReadByteRegion(reset, 0, 4) == std::vector<std::byte>(4));
    }
}

TEST_CASE("DVM-214 API19 gzip streams use guest zlib and release native state") {
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
        // Existing minimal APK fixture; compression classes come only from BootDex.
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
        const auto bytes = [&](VmObjectRef array) {
            return vm.Model().ReadByteRegion(array, 0, vm.Model().ArrayLength(array));
        };
        const auto expect_exception = [&](const VmCallOutcome& outcome, const char* type) {
            REQUIRE(outcome.exception.IsValid());
            CHECK(linker.Class(outcome.exception_class).descriptor == type);
        };
        for (const auto name : {"GZIPOutputStream", "GZIPInputStream", "DeflaterOutputStream",
                                "InflaterInputStream", "Deflater", "Inflater"}) {
            const auto type = linker.ResolveDescriptor(std::string("Ljava/util/zip/") + name + ";");
            linker.EnsureClassLinked(type);
            CHECK(linker.Class(type).is_boot_dex);
            CHECK_FALSE(linker.Class(type).is_intrinsic);
            for (const auto id : linker.Class(type).own_virtual_methods)
                CHECK(linker.Method(id).kind != MethodKind::intrinsic);
        }
        const auto gzip_encode = [&](const std::vector<std::byte>& data) {
            const auto output = construct("Ljava/io/ByteArrayOutputStream;");
            const auto output_root = vm.ProtectReferences(std::array{output});
            const auto gzip = construct("Ljava/util/zip/GZIPOutputStream;", "(Ljava/io/OutputStream;)V",
                                        {VmValue::Ref(output)});
            const auto gzip_root = vm.ProtectReferences(std::array{gzip});
            const auto input = byte_array(data);
            const auto input_root = vm.ProtectReferences(std::array{input});
            invoke(gzip, "write", "([BII)V", {VmValue::Ref(input), VmValue::Int(0),
                                              VmValue::Int(static_cast<int>(data.size()))});
            invoke(gzip, "flush", "()V");
            static_cast<void>(vm.CollectGarbage("gzip-live-stream"));
            invoke(gzip, "close", "()V");
            invoke(gzip, "close", "()V"); // The application closes the stream twice.
            expect_exception(invoke_result(gzip, "write", "(I)V", {VmValue::Int(1)}),
                             "Ljava/io/IOException;");
            return bytes(invoke(output, "toByteArray", "()[B").ref);
        };
        const auto gzip_decode = [&](const std::vector<std::byte>& compressed, bool valid) {
            const auto array = byte_array(compressed);
            const auto array_root = vm.ProtectReferences(std::array{array});
            const auto input = construct("Ljava/io/ByteArrayInputStream;", "([B)V", {VmValue::Ref(array)});
            const auto input_root = vm.ProtectReferences(std::array{input});
            const auto gzip = construct("Ljava/util/zip/GZIPInputStream;", "(Ljava/io/InputStream;)V",
                                        {VmValue::Ref(input)});
            const auto gzip_root = vm.ProtectReferences(std::array{gzip});
            const auto buffer = byte_array(std::vector<std::byte>(4096));
            const auto buffer_root = vm.ProtectReferences(std::array{buffer});
            std::vector<std::byte> result;
            bool failed = false, eof = false;
            for (int i = 0; i < 1000; ++i) {
                const auto read = invoke_result(gzip, "read", "([BII)I",
                    {VmValue::Ref(buffer), VmValue::Int(0), VmValue::Int(4096)});
                if (read.exception.IsValid()) {
                    CHECK(linker.IsAssignable(linker.ResolveDescriptor("Ljava/io/IOException;"), read.exception_class));
                    failed = true;
                    break;
                }
                const auto n = read.value.AsInt();
                if (n == -1) { eof = true; break; }
                REQUIRE(n > 0);
                const auto chunk = vm.Model().ReadByteRegion(buffer, 0, n);
                result.insert(result.end(), chunk.begin(), chunk.end());
            }
            CHECK(failed == !valid);
            CHECK(eof == valid);
            invoke(gzip, "close", "()V");
            invoke(gzip, "close", "()V");
            return result;
        };
        // Python's gzip.compress(mtime=0) provides an independent RFC1952 vector.
        const std::vector<std::byte> oracle = [] {
            std::vector<std::byte> result;
            for (const auto b : {0x1f,0x8b,0x08,0x00,0x00,0x00,0x00,0x00,0x02,0xff,0x73,0x0c,
                0xf0,0x34,0xb4,0x54,0x48,0xaf,0xca,0x2c,0x50,0x28,0xca,0x2f,0xcd,0x4b,0x51,
                0x28,0x29,0xca,0x2c,0x00,0x00,0x8d,0x71,0xb8,0x3e,0x15,0x00,0x00,0x00})
                result.push_back(static_cast<std::byte>(b));
            return result;
        }();
        const std::string text = "API19 gzip round trip";
        std::vector<std::byte> plain;
        for (const char c : text) plain.push_back(static_cast<std::byte>(static_cast<unsigned char>(c)));
        CHECK(gzip_decode(oracle, true) == plain);
        CHECK(gzip_decode(gzip_encode({}), true).empty());
        std::vector<std::byte> large(131073);
        std::uint32_t random = 1;
        for (auto& b : large) { random = random * 1664525U + 1013904223U; b = static_cast<std::byte>(random >> 24U); }
        const auto encoded = gzip_encode(large);
        REQUIRE(encoded.size() > 65536);
        CHECK(encoded[0] == std::byte{0x1f});
        CHECK(encoded[1] == std::byte{0x8b});
        // Decode the emitted raw DEFLATE with the independent loader implementation.
        // A synthetic local ZIP header reuses its existing strict CRC/size validation.
        const auto little32 = [&](std::size_t offset) {
            std::uint32_t value = 0;
            for (unsigned i = 0; i < 4; ++i)
                value |= std::to_integer<std::uint32_t>(encoded[offset+i]) << (8U*i);
            return value;
        };
        const auto crc = little32(encoded.size()-8);
        const auto size = little32(encoded.size()-4);
        CHECK(size == large.size());
        const auto compressed_size = static_cast<std::uint32_t>(encoded.size()-18);
        std::vector<std::byte> zip(31);
        Put32(zip, 0, 0x04034b50);
        Put16(zip, 8, 8);
        Put32(zip, 14, crc);
        Put32(zip, 18, compressed_size);
        Put32(zip, 22, size);
        Put16(zip, 26, 1);
        zip[30] = std::byte{'x'};
        zip.insert(zip.end(), encoded.begin()+10, encoded.end()-8);
        const loader::ApkArchive archive{{{"x", 0, 8, crc, compressed_size, size, 0}}};
        CHECK(loader::ReadApkEntry(zip, archive, "x") == large);
        CHECK(gzip_decode(encoded, true) == large);
        auto damaged = oracle;
        damaged[damaged.size() - 8] ^= std::byte{1};
        static_cast<void>(gzip_decode(damaged, false));
        damaged = oracle;
        damaged.pop_back();
        static_cast<void>(gzip_decode(damaged, false));
        damaged = oracle;
        damaged[damaged.size() - 4] ^= std::byte{1};
        static_cast<void>(gzip_decode(damaged, false));

        const auto deflater = construct("Ljava/util/zip/Deflater;");
        const auto deflater_root = vm.ProtectReferences(std::array{deflater});
        const auto inflater = construct("Ljava/util/zip/Inflater;");
        const auto inflater_root = vm.ProtectReferences(std::array{inflater});
        const auto input = byte_array(large);
        const auto input_root = vm.ProtectReferences(std::array{input});
        const auto output = byte_array(std::vector<std::byte>(200000));
        const auto output_root = vm.ProtectReferences(std::array{output});
        const auto decoded = byte_array(std::vector<std::byte>(large.size()));
        const auto decoded_root = vm.ProtectReferences(std::array{decoded});
        invoke(deflater, "setInput", "([B)V", {VmValue::Ref(input)});
        // API19 snapshots input on setInput; later writes must not change native input.
        vm.Model().WriteByteRegion(input, 0, std::vector<std::byte>{std::byte{0}});
        invoke(deflater, "finish", "()V");
        const auto count = invoke(deflater, "deflate", "([B)I", {VmValue::Ref(output)}).AsInt();
        CHECK(count > 65536);
        CHECK(invoke(deflater, "finished", "()Z").AsInt() == 1);
        CHECK(invoke(deflater, "getBytesRead", "()J").AsLong() == static_cast<std::int64_t>(large.size()));
        CHECK(invoke(deflater, "getBytesWritten", "()J").AsLong() == count);
        invoke(inflater, "setInput", "([BII)V", {VmValue::Ref(output), VmValue::Int(0), VmValue::Int(count)});
        CHECK(invoke(inflater, "inflate", "([B)I", {VmValue::Ref(decoded)}).AsInt() == static_cast<int>(large.size()));
        CHECK(bytes(decoded) == large);
        CHECK(invoke(inflater, "finished", "()Z").AsInt() == 1);
        CHECK(invoke(inflater, "getRemaining", "()I").AsInt() == 0);
        CHECK(invoke(inflater, "getBytesRead", "()J").AsLong() == count);
        CHECK(invoke(inflater, "getBytesWritten", "()J").AsLong() == static_cast<std::int64_t>(large.size()));
        invoke(deflater, "reset", "()V");
        invoke(inflater, "reset", "()V");
        CHECK(invoke(deflater, "getBytesRead", "()J").AsLong() == 0);
        CHECK(invoke(inflater, "getBytesWritten", "()J").AsLong() == 0);
        const auto dictionary = byte_array(plain);
        const auto dictionary_root = vm.ProtectReferences(std::array{dictionary});
        invoke(deflater, "setLevel", "(I)V", {VmValue::Int(9)});
        invoke(deflater, "setStrategy", "(I)V", {VmValue::Int(1)});
        // Apply changed parameters before installing a dictionary: API19 setInput calls
        // deflateParams with no output buffer; changing them after setDictionary may flush.
        invoke(deflater, "setInput", "([B)V", {VmValue::Ref(dictionary)});
        invoke(deflater, "finish", "()V");
        const auto parameter_count = invoke(deflater, "deflate", "([B)I", {VmValue::Ref(output)}).AsInt();
        invoke(inflater, "setInput", "([BII)V", {VmValue::Ref(output), VmValue::Int(0), VmValue::Int(parameter_count)});
        CHECK(invoke(inflater, "inflate", "([B)I", {VmValue::Ref(decoded)}).AsInt() == static_cast<int>(plain.size()));
        CHECK(vm.Model().ReadByteRegion(decoded, 0, static_cast<int>(plain.size())) == plain);
        invoke(deflater, "reset", "()V");
        invoke(inflater, "reset", "()V");
        invoke(deflater, "setDictionary", "([B)V", {VmValue::Ref(dictionary)});
        invoke(deflater, "setInput", "([B)V", {VmValue::Ref(dictionary)});
        invoke(deflater, "finish", "()V");
        const auto dictionary_count = invoke(deflater, "deflate", "([B)I", {VmValue::Ref(output)}).AsInt();
        invoke(inflater, "setInput", "([BII)V", {VmValue::Ref(output), VmValue::Int(0), VmValue::Int(dictionary_count)});
        CHECK(invoke(inflater, "inflate", "([B)I", {VmValue::Ref(decoded)}).AsInt() == 0);
        CHECK(invoke(inflater, "needsDictionary", "()Z").AsInt() == 1);
        CHECK(invoke(inflater, "getAdler", "()I").AsInt() == invoke(deflater, "getAdler", "()I").AsInt());
        invoke(inflater, "setDictionary", "([B)V", {VmValue::Ref(dictionary)});
        CHECK(invoke(inflater, "inflate", "([B)I", {VmValue::Ref(decoded)}).AsInt() == static_cast<int>(plain.size()));
        CHECK(vm.Model().ReadByteRegion(decoded, 0, static_cast<int>(plain.size())) == plain);
        const auto field = *linker.FindFieldRecursive(linker.ResolveDescriptor("Ljava/util/zip/Deflater;"), "streamHandle", "J");
        const auto slots = vm.Model().InstanceSlots(deflater);
        const auto slot = linker.Field(field).slot;
        const auto token = static_cast<std::int64_t>(std::uint64_t(slots[slot].bits) | (std::uint64_t(slots[slot+1].bits) << 32U));
        expect_exception(direct("Ljava/util/zip/Deflater;", "setInputImpl", "([BIIJ)V",
            {VmValue::Ref(deflater), VmValue::Ref(input), VmValue::Int(1), VmValue::Int(INT32_MAX), VmValue::Long(token)}),
            "Ljava/lang/ArrayIndexOutOfBoundsException;");
        expect_exception(direct("Ljava/util/zip/Inflater;", "getAdlerImpl", "(J)I",
            {VmValue::Ref(inflater), VmValue::Long(token)}), "Ljava/lang/IllegalStateException;");
        expect_exception(direct("Ljava/util/zip/Inflater;", "setFileInputImpl", "(Ljava/io/FileDescriptor;JIJ)I",
            {VmValue::Ref(inflater), VmValue::Ref(VmObjectRef{}), VmValue::Long(0), VmValue::Int(1), VmValue::Long(0)}),
            "Ljava/lang/UnsupportedOperationException;");
        invoke(deflater, "end", "()V");
        invoke(deflater, "end", "()V");
        invoke(inflater, "end", "()V");
        invoke(inflater, "end", "()V");
        expect_exception(invoke_result(deflater, "getAdler", "()I", {}), "Ljava/lang/IllegalStateException;");
        expect_exception(direct("Ljava/util/zip/Deflater;", "getAdlerImpl", "(J)I",
            {VmValue::Ref(deflater), VmValue::Long(token)}), "Ljava/lang/IllegalStateException;");
        static_cast<void>(vm.CollectGarbage("gzip-before-owner-test"));
        vm.RunFinalization();
        static_cast<void>(vm.CollectGarbage("gzip-before-owner-test-finalized"));
        const auto baseline = vm.GuestNativeResourceCount();
        const auto alive = construct("Ljava/util/zip/Deflater;");
        const auto alive_root = vm.ProtectReferences(std::array{alive});
        std::int64_t abandoned_token = 0;
        {
            const auto abandoned = construct("Ljava/util/zip/Deflater;");
            const auto abandoned_root = vm.ProtectReferences(std::array{abandoned});
            const auto values = vm.Model().InstanceSlots(abandoned);
            abandoned_token = static_cast<std::int64_t>(std::uint64_t(values[slot].bits) | (std::uint64_t(values[slot+1].bits) << 32U));
            CHECK(vm.GuestNativeResourceCount() == baseline + 2);
        }
        static_cast<void>(vm.CollectGarbage("gzip-abandoned-owner"));
        vm.RunFinalization();
        static_cast<void>(vm.CollectGarbage("gzip-abandoned-owner-finalized"));
        CHECK(vm.GuestNativeResourceCount() == baseline + 1);
        expect_exception(direct("Ljava/util/zip/Deflater;", "getAdlerImpl", "(J)I",
            {VmValue::Ref(alive), VmValue::Long(abandoned_token)}), "Ljava/lang/IllegalStateException;");
        CHECK(invoke(alive, "getBytesRead", "()J").AsLong() == 0);
        vm.ReleaseGuestNativeResources(true);
        CHECK(vm.GuestNativeResourceCount() == 0);
        invoke(alive, "end", "()V"); // Teardown writes API19's -1 sentinel, not zero.
        expect_exception(invoke_result(alive, "getAdler", "()I", {}), "Ljava/lang/IllegalStateException;");
    }
}

// The ZIP constructor now initializes the real Inflater superclass. Keep this
// behavioral regression in the guest zlib family, with the actual JNI bridge.
TEST_CASE("ZipInputStream reads guest source bytes and dispatches entry operations") {
    using namespace ogplay;
    using namespace runtime::dexvm;
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        CAPTURE(backend == InterpreterBackend::threaded ? "threaded" : "switch");
        runtime::VirtualFileSystem filesystem;
        core::CapabilityLedger ledger;
        core::Logger logger;
        std::vector<std::vector<std::byte>> contents;
        std::vector<runtime::BionicModuleSource> libraries;
        for (const auto name :
             {"libc.so", "libm.so", "libdl.so", "libstdc++.so", "libz.so", "libcrypto.so", "libssl.so", "libgabi++.so",
              "libicui18n.so", "libicuuc.so", "libstlport.so", "libogplay_jni.so"}) {
            contents.push_back(ReadPayloadBytes(std::string("lib/") + name));
            libraries.push_back({name, contents.back()});
        }
        auto context = std::make_shared<runtime::DexVmAndroidContext>();
        context->apk_bytes = {std::byte{0x50}, std::byte{0x4b}, std::byte{3}, std::byte{4}};
        session::AndroidAppProcessRequest request;
        request.manifest = AppManifest("android.app.Activity");
        request.system_libraries = libraries;
        // Existing minimal APK fixture; compression classes come only from BootDex.
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
            const auto array =
                vm.Model().NewPrimitiveArray(linker.ResolveDescriptor("[B"), runtime::JniPrimitiveKind::byte,
                                             static_cast<runtime::JniSize>(data.size()));
            vm.Model().WriteByteRegion(array, 0, data);
            return array;
        };
        const auto bytes = [&](VmObjectRef array) {
            return vm.Model().ReadByteRegion(array, 0, vm.Model().ArrayLength(array));
        };
        const auto expect_exception = [&](const VmCallOutcome& outcome, const char* type) {
            REQUIRE(outcome.exception.IsValid());
            CHECK(linker.Class(outcome.exception_class).descriptor == type);
        };

        // Stored ZIP containing save.dat = "hiok", generated independently.
        const std::array<unsigned char, 118> archive_bytes{
            0x50, 0x4b, 0x03, 0x04, 0x14, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x21, 0x00, 0xe7, 0x3f, 0xd5,
            0xa0, 0x04, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x73, 0x61, 0x76, 0x65,
            0x2e, 0x64, 0x61, 0x74, 0x68, 0x69, 0x6f, 0x6b, 0x50, 0x4b, 0x01, 0x02, 0x14, 0x03, 0x14, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x21, 0x00, 0xe7, 0x3f, 0xd5, 0xa0, 0x04, 0x00, 0x00, 0x00, 0x04, 0x00,
            0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x01, 0x00,
            0x00, 0x00, 0x00, 0x73, 0x61, 0x76, 0x65, 0x2e, 0x64, 0x61, 0x74, 0x50, 0x4b, 0x05, 0x06, 0x00, 0x00,
            0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x36, 0x00, 0x00, 0x00, 0x2a, 0x00, 0x00, 0x00, 0x00, 0x00};
        std::vector<std::byte> archive;
        for (const auto byte : archive_bytes)
            archive.push_back(static_cast<std::byte>(byte));
        const auto native_before = vm.GuestNativeResourceCount();
        const auto source = construct("Ljava/io/ByteArrayInputStream;", "([B)V", {VmValue::Ref(byte_array(archive))});
        const auto zip =
            construct("Ljava/util/zip/ZipInputStream;", "(Ljava/io/InputStream;)V", {VmValue::Ref(source)});
        const auto zip_root = vm.ProtectReferences(std::array{zip});
        CHECK(vm.GuestNativeResourceCount() == native_before + 1);
        CHECK(vm.IO().FindInput(source) == nullptr);
        const auto entry = invoke(zip, "getNextEntry", "()Ljava/util/zip/ZipEntry;").ref;
        REQUIRE(entry.IsValid());
        CHECK(vm.StringUtf8(invoke(entry, "getName", "()Ljava/lang/String;").ref) == "save.dat");
        CHECK(invoke(zip, "available", "()I").AsInt() == 1);
        CHECK(invoke(zip, "read", "()I").AsInt() == 'h');
        CHECK(invoke(zip, "skip", "(J)J", {VmValue::Long(1)}).AsLong() == 1);
        CHECK(invoke(zip, "markSupported", "()Z").AsInt() == 0);
        expect_exception(invoke_result(zip, "reset", "()V", {}), "Ljava/io/IOException;");
        const auto output = byte_array(std::vector<std::byte>(2));
        CHECK(invoke(zip, "read", "([BII)I", {VmValue::Ref(output), VmValue::Int(0), VmValue::Int(2)}).AsInt() == 2);
        CHECK(bytes(output) == std::vector<std::byte>{std::byte{'o'}, std::byte{'k'}});
        CHECK(invoke(zip, "read", "()I").AsInt() == -1);
        CHECK(invoke(zip, "available", "()I").AsInt() == 0);
        invoke(zip, "close", "()V");
        invoke(zip, "close", "()V");
        CHECK(vm.GuestNativeResourceCount() == native_before);
        expect_exception(invoke_result(zip, "read", "()I", {}), "Ljava/io/IOException;");
        filesystem.CreateDirectory("/sdcard");
        vm.IO().WriteFile("/sdcard/archive.zip", archive);
        const auto raw = construct("Ljava/io/FileInputStream;", "(Ljava/lang/String;)V",
                                   {VmValue::Ref(vm.NewStringUtf8("/sdcard/archive.zip"))});
        const auto filezip =
            construct("Ljava/util/zip/ZipInputStream;", "(Ljava/io/InputStream;)V", {VmValue::Ref(raw)});
        const auto filezip_root = vm.ProtectReferences(std::array{filezip});
        static_cast<void>(vm.CollectGarbage("zip-live-inherited-state"));
        invoke(filezip, "close", "()V");
        CHECK(invoke_result(raw, "read", "()I", {}).exception.IsValid());
        CHECK(vm.GuestNativeResourceCount() == native_before);
    }
}
