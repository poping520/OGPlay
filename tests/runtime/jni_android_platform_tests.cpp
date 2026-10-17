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

TEST_CASE("DVM-237 APS-11 real ARM Android asset surface and sensor boundaries share platform facts") {
    using namespace ogplay;
    using namespace runtime::dexvm;
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
      for (const bool compressed : {false, true}) {
        runtime::VirtualFileSystem filesystem;
        core::CapabilityLedger ledger;
        core::Logger logger;
        auto context = std::make_shared<runtime::DexVmAndroidContext>();
        struct ObservedSensorDirectory final : runtime::AndroidSensorDirectory {
            mutable std::size_t queries{};
            std::size_t Count() const noexcept override { ++queries; return 0; }
        };
        const auto directory_facts = std::make_shared<ObservedSensorDirectory>();
        context->sensor_directory = directory_facts;
        // Local ZIP fixture exercises the same metadata/CRC/range reader as a
        // parsed sealed archive. The FD still exposes the complete ZIP backing.
        const std::string entry = "assets/hello";
        const std::vector<std::byte> payload{std::byte{'a'}, std::byte{'b'}, std::byte{'c'}};
        const std::vector<std::byte> deflate{std::byte{0x4b}, std::byte{0x4c}, std::byte{0x4a}, std::byte{0x06}, std::byte{0x00}};
        const auto& encoded = compressed ? deflate : payload;
        auto& zip = context->apk_bytes;
        zip.resize(30 + entry.size());
        Put32(zip, 0, 0x04034b50);
        Put16(zip, 8, compressed ? 8 : 0);
        Put32(zip, 14, 0x352441c2);
        Put32(zip, 18, static_cast<std::uint32_t>(encoded.size()));
        Put32(zip, 22, 3);
        Put16(zip, 26, static_cast<std::uint16_t>(entry.size()));
        std::memcpy(zip.data() + 30, entry.data(), entry.size());
        zip.insert(zip.end(), encoded.begin(), encoded.end());
        context->archive.entries.push_back({entry, 0, static_cast<std::uint16_t>(compressed ? 8 : 0),
            0x352441c2, static_cast<std::uint32_t>(encoded.size()), 3, 0});
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
        request.context = context;
        request.filesystem = &filesystem;
        request.ledger = &ledger;
        request.logger = &logger;
        request.platform.android_id = "0123456789abcdef";
        request.dexvm.interpreter.backend = backend;
        auto app = session::AndroidAppProcess::Create(std::move(request));
        auto& process = app->NativeProcess();
        auto& bridge = app->DexVm();
        auto& vm = bridge.Vm();
        auto& env = process.Environment();
        const auto memory = process.GuestMemoryAccess();
        const auto scratch = memory.allocate(256);
        std::array<std::byte, 256> bytes{};
        std::memcpy(bytes.data(), "hello", 6);
        memory.write(scratch, bytes);
        const auto symbols = runtime::detail::BuildAndroidBoundarySymbols();
        const runtime::BionicHleSymbolProvider provider(symbols);
        const auto invoke = [&](const char* name, std::array<std::uint32_t, 4> args) {
            const auto target = provider.Lookup("libandroid.so", name);
            REQUIRE(target.has_value());
            return process.Invoke({*target, args, {}}).return_value;
        };
        const auto libc = [&](const char* name, std::array<std::uint32_t, 4> args) {
            const auto target = process.FindModuleExport(0, name);
            REQUIRE_FALSE(target.IsNull());
            return process.Invoke({target, args, {}}).return_value;
        };
        const auto word = [&](std::uint32_t offset) {
            std::array<std::byte, 4> out{}; memory.read(scratch.Add(offset), out);
            std::uint32_t result{};
            for (std::size_t i = 0; i < out.size(); ++i) result |= std::to_integer<std::uint32_t>(out[i]) << (i * 8);
            return result;
        };
        const auto sensor_manager = invoke("ASensorManager_getInstance", {});
        REQUIRE(sensor_manager != 0);
        const auto native_queries = directory_facts->queries;
        CHECK(invoke("ASensorManager_getSensorList", {sensor_manager, scratch.Add(32).Value(), 0, 0}) == 0);
        CHECK(word(32) != 0);
        CHECK(invoke("ASensorManager_getDefaultSensor", {sensor_manager, 1, 0, 0}) == 0);
        CHECK(directory_facts->queries > native_queries);
        const auto java_queries = directory_facts->queries;
        const auto sensor_class = vm.Linker().ResolveDescriptor("Landroid/hardware/LocalSensorManager;");
        const auto sensor_method = vm.Linker().FindDirectMethod(sensor_class, "nativeGetSensors", "()[Landroid/hardware/Sensor;");
        REQUIRE(sensor_method.has_value());
        const auto java_sensors = vm.Call(*sensor_method, {});
        REQUIRE_FALSE(java_sensors.exception.IsValid());
        CHECK(vm.Model().ArrayLength(java_sensors.value.ref) == 0);
        CHECK(directory_facts->queries > java_queries); // Both sides read this exact object.
        const auto native_looper = invoke("ALooper_forThread", {});
        REQUIRE(native_looper != 0);
        const auto queue_target = provider.Lookup("libandroid.so", "ASensorManager_createEventQueue");
        REQUIRE(queue_target.has_value());
        const std::array<std::uint32_t, 2> queue_data{0, 0};
        const auto sensor_queue = process.Invoke({*queue_target,
            {sensor_manager, native_looper, 2, 0x12345678}, queue_data}).return_value;
        REQUIRE(sensor_queue != 0);
        CHECK(static_cast<std::int32_t>(invoke("ASensorEventQueue_hasEvents", {sensor_queue, 0, 0, 0})) == -1);
        CHECK(invoke("ASensorEventQueue_getEvents", {sensor_queue, scratch.Value(), 1, 0}) == 0);
        CHECK(invoke("ASensorManager_destroyEventQueue", {sensor_manager, sensor_queue, 0, 0}) == 0);
        const auto object = vm.NewIntrinsicInstance("Landroid/content/res/AssetManager;");
        auto reference = bridge.PublishLocal(object);
        const auto convert = [&](runtime::JniReference ref) {
            return invoke("AAssetManager_fromJava", {process.GuestEnvironment().Value(), ref.Value(), 0, 0});
        };
        const auto manager = convert(reference);
        REQUIRE(manager != 0);
        // Real ARM JNI Surface conversion uses the existing active holder facts.
        const auto surface_object = vm.NewIntrinsicInstance("Landroid/view/Surface;");
        const auto holder_object = vm.NewIntrinsicInstance("Landroid/view/SurfaceHolder$Impl;");
        context->holder_surfaces.emplace(holder_object.Value(), surface_object);
        context->active_surface_holders.insert(holder_object.Value());
        context->managed_host_surface_open = true;
        const auto surface_reference = bridge.PublishLocal(surface_object);
        const auto from_surface = [&](runtime::JniReference ref) {
            return invoke("ANativeWindow_fromSurface", {process.GuestEnvironment().Value(), ref.Value(), 0, 0});
        };
        CHECK(from_surface(runtime::JniReference{}) == 0);
        CHECK(from_surface(reference) == 0);
        const auto surface_window = from_surface(surface_reference);
        REQUIRE(surface_window != 0);
        CHECK(from_surface(surface_reference) == surface_window);
        CHECK(invoke("ANativeWindow_getWidth", {surface_window, 0, 0, 0}) == context->surface_width);
        CHECK_FALSE(runtime::DispatchSurfaceHolderCallbacks(vm, *context, runtime::SurfaceHolderPhase::destroyed).has_value());
        CHECK(from_surface(surface_reference) == 0);
        invoke("ANativeWindow_release", {surface_window, 0, 0, 0});
        invoke("ANativeWindow_release", {surface_window, 0, 0, 0});
        env.DeleteLocalRef(1, surface_reference);
        context->holder_surfaces.clear();
        CHECK(convert(reference) == manager);
        const auto weak = env.NewWeakGlobalRef(1, reference);
        CHECK(convert(weak) == manager);
        const auto second_object = vm.NewIntrinsicInstance("Landroid/content/res/AssetManager;");
        const auto second_reference = bridge.PublishLocal(second_object);
        CHECK(convert(second_reference) != manager);
        env.DeleteLocalRef(1, second_reference);
        CHECK(invoke("AAssetManager_fromJava", {scratch.Value(), reference.Value(), 0, 0}) == 0);
        CHECK_FALSE(env.ExceptionCheck(1));
        CHECK(convert(runtime::JniReference{}) == 0);
        REQUIRE(env.ExceptionCheck(1));
        env.ExceptionClear(1);
        const auto system_object = vm.NewIntrinsicInstance("Landroid/content/res/AssetManager;");
        context->singletons.emplace("system_assets", system_object);
        const auto system_reference = bridge.PublishLocal(system_object);
        CHECK(convert(system_reference) == 0);
        REQUIRE(env.ExceptionCheck(1));
        const auto system_exception = env.ExceptionOccurred(1);
        env.ExceptionClear(1);
        CHECK(vm.Linker().Class(vm.Model().ObjectClass(bridge.FromReference(system_exception))).descriptor ==
            "Ljava/lang/UnsupportedOperationException;");
        env.DeleteLocalRef(1, system_exception); env.DeleteLocalRef(1, system_reference);
        context->singletons.erase("system_assets");
        const auto global = env.NewGlobalRef(1, reference);
        CHECK(convert(global) == manager);
        const auto open = [&](const char* path) {
            std::array<std::byte, 32> text{};
            std::memcpy(text.data(), path, std::strlen(path));
            memory.write(scratch, text);
            return invoke("AAssetManager_open", {manager, scratch.Value(), 2, 0});
        };
        CHECK(open("missing") == 0);
        CHECK(open("../hello") == 0);
        CHECK(open("/hello") == 0);
        const auto asset = open("hello"), other = open("hello");
        REQUIRE(asset != 0); REQUIRE(other != 0);
        CHECK(asset != other);
        if (!compressed) {
            auto damaged = std::make_shared<runtime::DexVmAndroidContext>();
            damaged->apk_bytes = context->apk_bytes;
            damaged->archive = context->archive;
            damaged->apk_bytes.back() ^= std::byte{1};
            CHECK_THROWS(runtime::ApkAssets(damaged)->Open("hello"));
        }
        CHECK(invoke("AAsset_read", {asset, scratch.Add(32).Value(), 2, 0}) == 2);
        CHECK(invoke("AAsset_getRemainingLength", {asset, 0, 0, 0}) == 1);
        CHECK(invoke("AAsset_getRemainingLength", {other, 0, 0, 0}) == 3);
        const memory::GuestAddress buffer{invoke("AAsset_getBuffer", {asset, 0, 0, 0})};
        REQUIRE_FALSE(buffer.IsNull());
        CHECK(invoke("AAsset_getBuffer", {asset, 0, 0, 0}) == buffer.Value());
        std::vector<std::byte> buffered(3);
        memory.read(buffer, buffered);
        CHECK(buffered == payload); // STORED and Deflate use the same entire contents.
        CHECK(invoke("AAsset_getRemainingLength", {asset, 0, 0, 0}) == 1);
        const auto directory = invoke("AAssetManager_openDir", {manager, scratch.Add(16).Value(), 0, 0});
        REQUIRE(directory != 0);
        const memory::GuestAddress filename{invoke("AAssetDir_getNextFileName", {directory, 0, 0, 0})};
        REQUIRE_FALSE(filename.IsNull());
        std::array<std::byte, 6> name_bytes{}; memory.read(filename, name_bytes);
        CHECK(std::string(reinterpret_cast<const char*>(name_bytes.data())) == "hello");
        CHECK(invoke("AAssetDir_getNextFileName", {directory, 0, 0, 0}) == 0);
        invoke("AAssetDir_close", {directory, 0, 0, 0});
        const auto fd = invoke("AAsset_openFileDescriptor", {asset, scratch.Add(64).Value(), scratch.Add(68).Value(), 0});
        if (compressed) {
            CHECK(static_cast<std::int32_t>(fd) == -1);
            CHECK(word(64) == 0); CHECK(word(68) == 0);
        } else {
            REQUIRE(static_cast<std::int32_t>(fd) >= 0);
            CHECK(word(64) == 30 + entry.size()); CHECK(word(68) == 3);
            const auto second_fd = invoke("AAsset_openFileDescriptor", {asset, scratch.Add(72).Value(), scratch.Add(76).Value(), 0});
            REQUIRE(second_fd != fd);
            CHECK(libc("lseek", {fd, word(64), 0, 0}) == word(64));
            CHECK(libc("read", {fd, scratch.Add(96).Value(), 3, 0}) == 3);
            std::array<std::byte, 3> actual{}; memory.read(scratch.Add(96), actual);
            CHECK(std::vector(actual.begin(), actual.end()) == payload);
            invoke("AAsset_close", {asset, 0, 0, 0});
            CHECK(libc("lseek", {second_fd, word(72), 0, 0}) == word(72));
            CHECK(libc("read", {second_fd, scratch.Add(96).Value(), 3, 0}) == 3);
            CHECK(libc("close", {fd, 0, 0, 0}) == 0);
            CHECK(libc("close", {second_fd, 0, 0, 0}) == 0);
        }
        if (compressed) invoke("AAsset_close", {asset, 0, 0, 0});
        CHECK_THROWS(memory.read(buffer, buffered));
        // Conversion must preserve a pending throwable and never root a manager.
        const auto throwable = vm.NewIntrinsicInstance("Ljava/lang/IllegalStateException;");
        const auto throwable_reference = bridge.PublishLocal(throwable);
        env.Throw(1, throwable_reference);
        CHECK(convert(reference) == 0);
        const auto pending = env.ExceptionOccurred(1);
        env.ExceptionClear(1);
        CHECK(bridge.FromReference(pending) == throwable);
        env.DeleteLocalRef(1, pending);
        env.DeleteLocalRef(1, throwable_reference);
        const auto wrong = bridge.PublishLocal(vm.NewStringUtf8("not an asset manager"));
        CHECK(convert(wrong) == 0); REQUIRE(env.ExceptionCheck(1));
        env.ExceptionClear(1); env.DeleteLocalRef(1, wrong);
        env.DeleteLocalRef(1, reference); env.DeleteGlobalRef(1, global);
        static_cast<void>(vm.CollectGarbage("ndk-asset-manager-lifetime"));
        CHECK(convert(weak) == 0); REQUIRE(env.ExceptionCheck(1));
        env.ExceptionClear(1); env.DeleteWeakGlobalRef(1, weak);
        CHECK(invoke("AAsset_read", {other, scratch.Add(32).Value(), 3, 0}) == 3);
        invoke("AAsset_close", {other, 0, 0, 0});
        CHECK_THROWS(invoke("AAssetManager_open", {manager, scratch.Value(), 2, 0}));
        memory.release(scratch, 256);
        static_cast<void>(app->Stop());
      }
    }
}

TEST_CASE("DVM-239 ARM JNI Debug query observes virtual RAM growth and release") {
    using namespace ogplay;
    using namespace runtime::dexvm;
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        OrchestratedApp f("fixture.DelayedContentActivity", true, false, {}, {}, true, backend);
        {
        auto& bridge = f.app->DexVm(); auto& vm = bridge.Vm(); auto& linker = vm.Linker();
        auto& process = f.app->NativeProcess(); auto& env = process.Environment();
        const auto memory = process.GuestMemoryAccess();
        const auto scratch = memory.allocate(256);
        std::array<std::byte, 256> bytes{};
        constexpr char method_name[] = "getMemoryInfo";
        constexpr char signature[] = "(Landroid/os/Debug$MemoryInfo;)V";
        std::memcpy(bytes.data(), method_name, sizeof(method_name));
        std::memcpy(bytes.data() + 32, signature, sizeof(signature));
        memory.write(scratch, bytes);
        const auto debug_class = linker.ResolveDescriptor("Landroid/os/Debug;");
        const auto clazz = bridge.PublishLocal(vm.Model().ClassObject(debug_class));
        const auto info_class = linker.ResolveDescriptor("Landroid/os/Debug$MemoryInfo;");
        const auto initialized = vm.EnsureClassInitialized(info_class);
        REQUIRE_FALSE(initialized.exception.IsValid());
        const auto info = vm.NewIntrinsicInstance("Landroid/os/Debug$MemoryInfo;");
        const auto roots = vm.ProtectReferences(std::array{info});
        const auto constructor = linker.FindDirectMethod(info_class, "<init>", "()V");
        REQUIRE(constructor.has_value());
        REQUIRE_FALSE(vm.Call(*constructor, std::array{VmValue::Ref(info)}).exception.IsValid());
        const auto reference = bridge.PublishLocal(info);
        const auto invoke = [&](const char* name, std::array<std::uint32_t, 4> args) {
            return process.Invoke({memory::GuestAddress{EnvironmentThunk(name)}, args, {}}).return_value;
        };
        const auto method = invoke("GetStaticMethodID", {process.GuestEnvironment().Value(), clazz.Value(),
            scratch.Value(), scratch.Add(32).Value()});
        REQUIRE(method != 0); REQUIRE_FALSE(env.ExceptionCheck(1));
        for (std::size_t i = 0; i < 4; ++i) bytes[128 + i] = static_cast<std::byte>(reference.Value() >> (i * 8));
        memory.write(scratch, bytes);
        const auto query = [&] {
            invoke("CallStaticVoidMethodA", {process.GuestEnvironment().Value(), clazz.Value(), method, scratch.Add(128).Value()});
            REQUIRE_FALSE(env.ExceptionCheck(1));
            const auto slot = linker.FindVtableIndex(info_class, "getTotalPss", "()I");
            REQUIRE(slot.has_value());
            const auto result = vm.Call(linker.Class(info_class).vtable[*slot], std::array{VmValue::Ref(info)});
            REQUIRE_FALSE(result.exception.IsValid());
            return result.value.AsInt();
        };
        const auto before = query();
        CHECK(before > 0);
        const auto extra = memory.allocate(8192);
        const auto grown = query();
        CHECK(grown >= before + 8);
        memory.release(extra, 8192);
        CHECK(query() < grown);
        f.context->process_memory_snapshot = runtime::MakeVirtualPrivateProcessMemorySnapshot(1024, 2048);
        CHECK(query() == 3);
        env.DeleteLocalRef(1, reference); env.DeleteLocalRef(1, clazz);
        memory.release(scratch, 256);
        }
        static_cast<void>(f.app->Stop());
        f.app.reset();
        CHECK_FALSE(f.context->read_process_memory);
    }
}

TEST_CASE("DVM-240 ARM JNI installed applications expose the session APK and reject unknown flags") {
    using namespace ogplay;
    using namespace runtime::dexvm;
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        OrchestratedApp f("fixture.DelayedContentActivity", true, false, {}, {}, true, backend);
        REQUIRE(f.context->package_inventory_known);
        static_cast<void>(f.app->StartApplication());
        auto& bridge = f.app->DexVm(); auto& vm = bridge.Vm(); auto& linker = vm.Linker();
        auto& process = f.app->NativeProcess(); auto& env = process.Environment();
        const auto memory = process.GuestMemoryAccess();
        const auto scratch = memory.allocate(256);
        std::array<std::byte, 256> bytes{};
        constexpr char method_name[] = "getInstalledApplications";
        constexpr char signature[] = "(I)Ljava/util/List;";
        std::memcpy(bytes.data(), method_name, sizeof(method_name));
        std::memcpy(bytes.data() + 32, signature, sizeof(signature));
        memory.write(scratch, bytes);
        const auto base = f.context->application_base_context;
        REQUIRE(base.IsValid());
        const auto slot = linker.FindVtableIndex(vm.Model().ObjectClass(base), "getPackageManager", "()Landroid/content/pm/PackageManager;");
        REQUIRE(slot.has_value());
        const auto result = vm.Call(linker.Class(vm.Model().ObjectClass(base)).vtable[*slot], std::array{VmValue::Ref(base)});
        REQUIRE_FALSE(result.exception.IsValid());
        const auto manager = result.value.ref;
        const auto reference = bridge.PublishLocal(manager);
        const auto clazz = bridge.PublishLocal(vm.Model().ClassObject(vm.Model().ObjectClass(manager)));
        const auto invoke = [&](const char* name, std::array<std::uint32_t, 4> args) {
            return process.Invoke({memory::GuestAddress{EnvironmentThunk(name)}, args, {}}).return_value;
        };
        const auto method = invoke("GetMethodID", {process.GuestEnvironment().Value(), clazz.Value(),
            scratch.Value(), scratch.Add(32).Value()});
        REQUIRE(method != 0); REQUIRE_FALSE(env.ExceptionCheck(1));
        const auto list_reference = runtime::JniReference{invoke("CallObjectMethodA", {
            process.GuestEnvironment().Value(), reference.Value(), method, scratch.Add(128).Value()})};
        REQUIRE_FALSE(env.ExceptionCheck(1));
        const auto list = bridge.FromReference(list_reference);
        const auto roots = vm.ProtectReferences(std::array{list});
        CHECK(linker.Class(vm.Model().ObjectClass(list)).is_boot_dex);
        const auto get = linker.FindVtableIndex(vm.Model().ObjectClass(list), "get", "(I)Ljava/lang/Object;");
        REQUIRE(get.has_value());
        const auto item = vm.Call(linker.Class(vm.Model().ObjectClass(list)).vtable[*get],
                                 std::array{VmValue::Ref(list), VmValue::Int(0)});
        REQUIRE_FALSE(item.exception.IsValid());
        const auto source = linker.FindFieldRecursive(vm.Model().ObjectClass(item.value.ref), "sourceDir", "Ljava/lang/String;");
        REQUIRE(source.has_value());
        const auto path = VmObjectRef{vm.Model().InstanceSlots(item.value.ref)[linker.Field(*source).slot].bits};
        CHECK(vm.StringUtf8(path) == f.context->package_resource_path);
        CHECK(vm.StringUtf8(path) == "/data/app/fixture-1.apk");
        const auto fd = f.filesystem.Open(vm.StringUtf8(path), {.read = true});
        std::array<std::byte, 4> apk_header{};
        CHECK(f.filesystem.Read(fd, apk_header) == apk_header.size());
        CHECK(std::equal(apk_header.begin(), apk_header.end(), f.context->apk_bytes.begin()));
        f.filesystem.Close(fd);
        // GET_SHARED_LIBRARY_FILES is outside the declared bounded query.
        bytes[129] = std::byte{4};
        memory.write(scratch, bytes);
        CHECK(invoke("CallObjectMethodA", {process.GuestEnvironment().Value(), reference.Value(),
                                          method, scratch.Add(128).Value()}) == 0);
        REQUIRE(env.ExceptionCheck(1));
        const auto throwable = env.ExceptionOccurred(1);
        env.ExceptionClear(1);
        CHECK(linker.Class(vm.Model().ObjectClass(bridge.FromReference(throwable))).descriptor == "Ljava/lang/UnsupportedOperationException;");
        env.DeleteLocalRef(1, throwable); env.DeleteLocalRef(1, list_reference);
        env.DeleteLocalRef(1, reference); env.DeleteLocalRef(1, clazz);
        memory.release(scratch, 256);
        static_cast<void>(f.app->Stop());
    }
}

TEST_CASE("DVM-245 ARM JNI content append and lookup preserve the shared hierarchy") {
    using namespace ogplay;
    using namespace runtime;
    using namespace runtime::dexvm;
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ApplicationProcess f(backend);
        auto& bridge = *f.bridge; auto& vm = bridge.Vm();
        auto& process = f.session->Process(); auto& env = f.session->Environment();
        const auto memory = process.GuestMemoryAccess(); const auto scratch = memory.allocate(512);
        env.PushLocalFrame(1, 32);
        const auto activity = vm.NewIntrinsicInstance("Landroid/app/Activity;");
        const auto activity_ref = bridge.PublishLocal(activity);
        const auto klass = bridge.PublishLocal(vm.Model().ClassObject(vm.Model().ObjectClass(activity)));
        const auto first = vm.NewIntrinsicInstance("Landroid/view/View;");
        const auto first_ref = bridge.PublishLocal(first);
        const auto second = vm.NewIntrinsicInstance("Landroid/view/View;");
        const auto second_ref = bridge.PublishLocal(second);
        const auto invoke = [&](const char* name, std::array<std::uint32_t, 4> args) {
            return process.Invoke({memory::GuestAddress{EnvironmentThunk(name)}, args, {}}).return_value;
        };
        const auto method = [&](const char* name, const char* descriptor) {
            std::array<std::byte, 256> data{};
            std::memcpy(data.data(), name, std::strlen(name) + 1);
            std::memcpy(data.data() + 64, descriptor, std::strlen(descriptor) + 1);
            memory.write(scratch, data);
            const auto id = invoke("GetMethodID", {process.GuestEnvironment().Value(), klass.Value(), scratch.Value(), scratch.Add(64).Value()});
            REQUIRE(id != 0); REQUIRE_FALSE(env.ExceptionCheck(1)); return id;
        };
        const auto arguments = [&](std::uint32_t a, std::uint32_t b = 0) {
            std::array<std::byte, 16> data{};
            for (std::size_t i = 0; i < 4; ++i) {
                data[i] = static_cast<std::byte>(a >> (8 * i));
                data[8 + i] = static_cast<std::byte>(b >> (8 * i));
            }
            memory.write(scratch.Add(256), data);
        };
        const auto setter = method("setContentView", "(Landroid/view/View;)V");
        arguments(first_ref.Value());
        static_cast<void>(invoke("CallVoidMethodA", {process.GuestEnvironment().Value(), activity_ref.Value(), setter, scratch.Add(256).Value()}));
        REQUIRE_FALSE(env.ExceptionCheck(1));
        const auto append = method("addContentView", "(Landroid/view/View;Landroid/view/ViewGroup$LayoutParams;)V");
        arguments(second_ref.Value());
        static_cast<void>(invoke("CallVoidMethodA", {process.GuestEnvironment().Value(), activity_ref.Value(), append, scratch.Add(256).Value()}));
        REQUIRE_FALSE(env.ExceptionCheck(1));
        const auto find = method("findViewById", "(I)Landroid/view/View;");
        arguments(0x01020002);
        const auto result = JniReference{invoke("CallObjectMethodA", {process.GuestEnvironment().Value(), activity_ref.Value(), find, scratch.Add(256).Value()})};
        REQUIRE_FALSE(env.ExceptionCheck(1)); REQUIRE_FALSE(result.IsNull());
        const auto parent = bridge.FromReference(result);
        const auto node = *FindViewUiNode(*f.context, parent.Value());
        static_cast<void>(vm.CollectGarbage("window-content-jni"));
        CHECK(f.context->ui_tree.Get(node)->children.size() == 2);
        CHECK(f.context->ui_tree.Get(*FindViewUiNode(*f.context, first.Value()))->parent == node);
        CHECK(f.context->ui_tree.Get(*FindViewUiNode(*f.context, second.Value()))->parent == node);
        CHECK(f.context->content_view == first);
        static_cast<void>(env.PopLocalFrame(1)); memory.release(scratch, 512);
    }
}

TEST_CASE("DVM-247 ARM JNI inherited inflater lookup Context and native resource failure") {
    using namespace ogplay;
    using namespace runtime;
    using namespace runtime::dexvm;
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ApplicationProcess f(backend);
        auto& bridge = *f.bridge; auto& vm = bridge.Vm(); auto& linker = vm.Linker();
        auto& process = f.session->Process(); auto& env = f.session->Environment();
        const auto memory = process.GuestMemoryAccess(); const auto scratch = memory.allocate(512);
        env.PushLocalFrame(1, 32);
        const auto base = vm.NewIntrinsicInstance("Landroid/content/Context;");
        const auto base_ref = bridge.PublishLocal(base);
        const auto activity = vm.NewIntrinsicInstance("Landroid/app/Activity;");
        const auto activity_ref = bridge.PublishLocal(activity);
        const auto attach = *linker.FindVtableIndex(vm.Model().ObjectClass(activity), "attachBaseContext", "(Landroid/content/Context;)V");
        REQUIRE_FALSE(vm.Call(linker.Class(vm.Model().ObjectClass(activity)).vtable[attach], std::array{VmValue::Ref(activity), VmValue::Ref(base)}).exception.IsValid());
        const auto invoke = [&](const char* name, std::array<std::uint32_t, 4> args) {
            return process.Invoke({memory::GuestAddress{EnvironmentThunk(name)}, args, {}}).return_value;
        };
        const auto method = [&](VmObjectRef object, const char* name, const char* descriptor) {
            const auto klass = bridge.PublishLocal(vm.Model().ClassObject(vm.Model().ObjectClass(object)));
            std::array<std::byte, 256> data{};
            std::memcpy(data.data(), name, std::strlen(name) + 1);
            std::memcpy(data.data() + 64, descriptor, std::strlen(descriptor) + 1);
            memory.write(scratch, data);
            const auto id = invoke("GetMethodID", {process.GuestEnvironment().Value(), klass.Value(), scratch.Value(), scratch.Add(64).Value()});
            REQUIRE(id != 0); REQUIRE_FALSE(env.ExceptionCheck(1)); return id;
        };
        const auto getter = method(activity, "getLayoutInflater", "()Landroid/view/LayoutInflater;");
        const auto query = [&](JniReference object, std::uint32_t id) {
            const auto result = JniReference{invoke("CallObjectMethodA", {process.GuestEnvironment().Value(), object.Value(), id, scratch.Add(256).Value()})};
            REQUIRE_FALSE(env.ExceptionCheck(1)); REQUIRE_FALSE(result.IsNull()); return result;
        };
        const auto inflater_ref = query(activity_ref, getter);
        const auto inflater = bridge.FromReference(inflater_ref);
        CHECK(bridge.FromReference(query(activity_ref, getter)) == inflater);
        const auto get_context = method(inflater, "getContext", "()Landroid/content/Context;");
        CHECK(bridge.FromReference(query(inflater_ref, get_context)) == activity);
        const auto clone = method(inflater, "cloneInContext", "(Landroid/content/Context;)Landroid/view/LayoutInflater;");
        std::array<std::byte, 32> args{};
        for (std::size_t i = 0; i < 4; ++i) args[i] = static_cast<std::byte>(base_ref.Value() >> (8 * i));
        memory.write(scratch.Add(256), args);
        const auto clone_ref = query(inflater_ref, clone);
        CHECK(bridge.FromReference(clone_ref) != inflater);
        CHECK(bridge.FromReference(query(clone_ref, get_context)) == base);
        static_cast<void>(vm.CollectGarbage("inflater-jni-fields"));
        const auto inflate = method(inflater, "inflate", "(ILandroid/view/ViewGroup;Z)Landroid/view/View;");
        args.fill(std::byte{0}); args[0] = std::byte{0x7b};
        memory.write(scratch.Add(256), args);
        static_cast<void>(invoke("CallObjectMethodA", {process.GuestEnvironment().Value(), inflater_ref.Value(), inflate, scratch.Add(256).Value()}));
        REQUIRE(env.ExceptionCheck(1));
        const auto error_ref = env.ExceptionOccurred(1);
        env.ExceptionClear(1);
        const auto error = bridge.FromReference(error_ref);
        CHECK(linker.Class(vm.Model().ObjectClass(error)).descriptor == "Landroid/content/res/Resources$NotFoundException;");
        static_cast<void>(env.PopLocalFrame(1)); memory.release(scratch, 512);
    }
}

TEST_CASE("DVM-244 ARM JNI resource bitmap decoding publishes shared pixel storage") {
    using namespace ogplay;
    using namespace runtime;
    using namespace runtime::dexvm;
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ApplicationProcess f(backend);
        f.context->apk_bytes = ogplay::test::BitmapDecodeApk();
        f.context->archive = loader::ParseApkArchive(f.context->apk_bytes);
        f.context->arsc.entries = {{.resource_id=0x7f020001, .type_name="drawable", .entry_name="fixture",
            .string_value="res/drawable/fixture.png", .value_type=3, .density=160}};
        auto& bridge=*f.bridge; auto& vm=bridge.Vm(); auto& linker=vm.Linker();
        auto& process=f.session->Process(); auto& env=f.session->Environment();
        const auto memory=process.GuestMemoryAccess(); const auto scratch=memory.allocate(256);
        env.PushLocalFrame(1,32);
        const auto factory=bridge.PublishLocal(vm.Model().ClassObject(linker.ResolveDescriptor("Landroid/graphics/BitmapFactory;")));
        const auto resources=bridge.PublishLocal(vm.NewIntrinsicInstance("Landroid/content/res/Resources;"));
        std::array<std::byte,256> data{};
        constexpr char name[]="decodeResource";
        constexpr char descriptor[]="(Landroid/content/res/Resources;I)Landroid/graphics/Bitmap;";
        std::memcpy(data.data(),name,sizeof(name)); std::memcpy(data.data()+64,descriptor,sizeof(descriptor));
        for(std::size_t i=0;i<4;++i) {
            data[128+i]=static_cast<std::byte>(resources.Value()>>(8*i));
            data[136+i]=static_cast<std::byte>(0x7f020001U>>(8*i));
        }
        memory.write(scratch,data);
        const auto invoke=[&](const char* slot,std::array<std::uint32_t,4> args) {
            return process.Invoke({memory::GuestAddress{EnvironmentThunk(slot)},args,{}}).return_value;
        };
        const auto method=invoke("GetStaticMethodID",{process.GuestEnvironment().Value(),factory.Value(),scratch.Value(),scratch.Add(64).Value()});
        REQUIRE(method!=0); REQUIRE_FALSE(env.ExceptionCheck(1));
        const auto decode=[&] {
            const auto ref=JniReference{invoke("CallStaticObjectMethodA",{process.GuestEnvironment().Value(),factory.Value(),method,scratch.Add(128).Value()})};
            REQUIRE_FALSE(env.ExceptionCheck(1)); REQUIRE_FALSE(ref.IsNull()); return ref;
        };
        const auto first_ref=decode(); const auto first=bridge.FromReference(first_ref);
        const auto second_ref=decode(); const auto second=bridge.FromReference(second_ref);
        CHECK(first!=second);
        CHECK(f.context->bitmaps.at(first.Value()).pixels!=f.context->bitmaps.at(second.Value()).pixels);
        CHECK(f.context->bitmaps.at(first.Value()).pixels->Get(0)==0xffff0000);
        CHECK(f.context->bitmaps.at(first.Value()).pixels->Get(1)==0xff0000ff);
        static_cast<void>(vm.CollectGarbage("bitmap-resource-jni"));
        const auto width=linker.FindVtableIndex(vm.Model().ObjectClass(first),"getWidth","()I");
        REQUIRE(width.has_value());
        const auto result=vm.Call(linker.Class(vm.Model().ObjectClass(first)).vtable[*width],std::array{VmValue::Ref(first)});
        REQUIRE_FALSE(result.exception.IsValid()); CHECK(result.value.AsInt()==2);
        // The decoded object also reaches the same ImageView binding through ARM JNI.
        const auto owner = vm.NewIntrinsicInstance("Landroid/content/Context;");
        const auto owner_root = vm.ProtectReferences(std::array{owner});
        const auto image = vm.NewIntrinsicInstance("Landroid/widget/ImageView;");
        const auto image_ref = bridge.PublishLocal(image);
        const auto image_type = vm.Model().ObjectClass(image);
        const auto constructor = linker.FindDirectMethod(image_type, "<init>", "(Landroid/content/Context;)V");
        REQUIRE(constructor.has_value());
        REQUIRE_FALSE(vm.Call(*constructor, std::array{VmValue::Ref(image), VmValue::Ref(owner)}).exception.IsValid());
        const auto image_class = bridge.PublishLocal(vm.Model().ClassObject(image_type));
        constexpr char setter_name[] = "setImageBitmap";
        constexpr char setter_descriptor[] = "(Landroid/graphics/Bitmap;)V";
        std::memcpy(data.data(), setter_name, sizeof(setter_name));
        std::memcpy(data.data() + 64, setter_descriptor, sizeof(setter_descriptor));
        for (std::size_t i = 0; i < 4; ++i)
            data[128 + i] = static_cast<std::byte>(first_ref.Value() >> (8 * i));
        memory.write(scratch, data);
        const auto setter = invoke("GetMethodID", {process.GuestEnvironment().Value(), image_class.Value(), scratch.Value(), scratch.Add(64).Value()});
        REQUIRE(setter != 0); REQUIRE_FALSE(env.ExceptionCheck(1));
        static_cast<void>(invoke("CallVoidMethodA", {process.GuestEnvironment().Value(), image_ref.Value(), setter, scratch.Add(128).Value()}));
        REQUIRE_FALSE(env.ExceptionCheck(1));
        static_cast<void>(vm.CollectGarbage("image-bitmap-jni"));
        const auto drawable = f.context->ui_view_images.at(image.Value());
        CHECK(f.context->ui_drawables.at(drawable.Value()).bitmap == first);
        const auto node = *FindViewUiNode(*f.context, image.Value());
        REQUIRE(f.context->ui_tree.Get(node)->image_bitmap);
        CHECK(f.context->ui_tree.Get(node)->image_bitmap->rgba8[0] == 255);
        CHECK(f.context->ui_tree.Get(node)->image_bitmap->rgba8[6] == 255);
        static_cast<void>(env.PopLocalFrame(1)); memory.release(scratch,256);
    }
}

TEST_CASE("DVM-243 ARM JNI StatFs refresh observes the shared sandbox volume") {
    using namespace ogplay;
    using namespace runtime;
    using namespace runtime::dexvm;
    const auto root = std::filesystem::temp_directory_path() / "ogplay-jni-statfs";
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        std::filesystem::remove_all(root);
        std::filesystem::create_directories(root);
        {
            auto store = SandboxStore::Open(root, "fixture", "fixture", {.byte_quota = 16 * 4096});
            ApplicationProcess f(backend, std::string_view("/"));
            const std::array<std::string, 2> writable{"/data/data/fixture", "/sdcard"};
            f.filesystem.AttachSandbox(*store, writable);
            auto& bridge = *f.bridge; auto& vm = bridge.Vm(); auto& env = f.session->Environment();
            auto& process = f.session->Process(); const auto memory = process.GuestMemoryAccess();
            const auto scratch = memory.allocate(256);
            env.PushLocalFrame(1, 32);
            const auto klass = bridge.PublishLocal(vm.Model().ClassObject(vm.Linker().ResolveDescriptor("Landroid/os/StatFs;")));
            const auto path = bridge.PublishLocal(vm.NewStringUtf8("/sdcard"));
            const auto invoke = [&](const char* name, std::array<std::uint32_t, 4> args) {
                return process.Invoke({memory::GuestAddress{EnvironmentThunk(name)}, args, {}});
            };
            const auto method = [&](const char* name, const char* signature) {
                std::array<std::byte, 256> bytes{};
                std::memcpy(bytes.data(), name, std::strlen(name) + 1);
                std::memcpy(bytes.data() + 64, signature, std::strlen(signature) + 1);
                for (std::size_t i = 0; i < 4; ++i) bytes[128 + i] = static_cast<std::byte>(path.Value() >> (i * 8));
                memory.write(scratch, bytes);
                const auto id = invoke("GetMethodID", {process.GuestEnvironment().Value(), klass.Value(), scratch.Value(), scratch.Add(64).Value()}).return_value;
                REQUIRE(id != 0); REQUIRE_FALSE(env.ExceptionCheck(1));
                return id;
            };
            const auto constructor = method("<init>", "(Ljava/lang/String;)V");
            const auto object = JniReference{invoke("NewObjectA", {process.GuestEnvironment().Value(), klass.Value(), constructor, scratch.Add(128).Value()}).return_value};
            REQUIRE_FALSE(object.IsNull()); REQUIRE_FALSE(env.ExceptionCheck(1));
            const auto available = method("getAvailableBlocksLong", "()J");
            const auto query = [&] {
                const auto value = invoke("CallLongMethodA", {process.GuestEnvironment().Value(), object.Value(), available, 0});
                REQUIRE_FALSE(env.ExceptionCheck(1));
                return (static_cast<std::uint64_t>(value.return_value_high) << 32) | value.return_value;
            };
            CHECK(query() == 16);
            const auto fd = f.filesystem.Open("/sdcard/save", {.write = true, .create = true});
            const std::vector<std::byte> bytes(4096);
            REQUIRE(f.filesystem.Write(fd, bytes) == bytes.size());
            CHECK(query() == 16);
            const auto refresh = method("restat", "(Ljava/lang/String;)V");
            static_cast<void>(invoke("CallVoidMethodA", {process.GuestEnvironment().Value(), object.Value(), refresh, scratch.Add(128).Value()}));
            REQUIRE_FALSE(env.ExceptionCheck(1)); CHECK(query() == 15);
            f.filesystem.Close(fd);
            static_cast<void>(env.PopLocalFrame(1)); memory.release(scratch, 256);
        }
        std::filesystem::remove_all(root);
    }
}

TEST_CASE("DVM-241 ARM JNI connectivity query returns the original disconnected value") {
    using namespace ogplay;
    using namespace runtime::dexvm;
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        OrchestratedApp f("fixture.DelayedContentActivity", true, false, {}, {}, true, backend);
        f.context->granted_permissions.insert("android.permission.ACCESS_NETWORK_STATE");
        auto& bridge = f.app->DexVm(); auto& vm = bridge.Vm(); auto& linker = vm.Linker();
        auto& process = f.app->NativeProcess(); auto& env = process.Environment();
        const auto memory = process.GuestMemoryAccess();
        const auto scratch = memory.allocate(256);
        const auto manager = vm.NewIntrinsicInstance("Landroid/net/ConnectivityManager;");
        const auto roots = vm.ProtectReferences(std::array{manager});
        env.PushLocalFrame(1, 32);
        const auto reference = bridge.PublishLocal(manager);
        const auto invoke = [&](const char* name, std::array<std::uint32_t, 4> args) {
            return process.Invoke({memory::GuestAddress{EnvironmentThunk(name)}, args, {}}).return_value;
        };
        const auto method_id = [&](VmObjectRef object, const char* name, const char* signature) {
            std::array<std::byte, 256> bytes{};
            std::memcpy(bytes.data(), name, std::strlen(name) + 1);
            std::memcpy(bytes.data() + 64, signature, std::strlen(signature) + 1);
            bytes[128] = std::byte{1}; // jvalue networkType=TYPE_WIFI
            memory.write(scratch, bytes);
            const auto clazz = bridge.PublishLocal(vm.Model().ClassObject(vm.Model().ObjectClass(object)));
            const auto method = invoke("GetMethodID", {process.GuestEnvironment().Value(), clazz.Value(),
                                                       scratch.Value(), scratch.Add(64).Value()});
            REQUIRE_FALSE(env.ExceptionCheck(1)); REQUIRE(method != 0);
            return method;
        };
        const auto query = method_id(manager, "getNetworkInfo", "(I)Landroid/net/NetworkInfo;");
        const auto info_reference = runtime::JniReference{invoke("CallObjectMethodA", {
            process.GuestEnvironment().Value(), reference.Value(), query, scratch.Add(128).Value()})};
        REQUIRE_FALSE(env.ExceptionCheck(1));
        const auto info = bridge.FromReference(info_reference);
        REQUIRE(info.IsValid()); CHECK(linker.Class(vm.Model().ObjectClass(info)).is_boot_dex);
        const auto connected = method_id(info, "isConnected", "()Z");
        CHECK(invoke("CallBooleanMethodA", {process.GuestEnvironment().Value(), info_reference.Value(), connected, 0}) == 0);
        REQUIRE_FALSE(env.ExceptionCheck(1));
        f.context->granted_permissions.clear();
        CHECK(invoke("CallObjectMethodA", {process.GuestEnvironment().Value(), reference.Value(), query,
                                          scratch.Add(128).Value()}) == 0);
        REQUIRE(env.ExceptionCheck(1));
        const auto exception = env.ExceptionOccurred(1); env.ExceptionClear(1);
        CHECK(linker.Class(vm.Model().ObjectClass(bridge.FromReference(exception))).descriptor == "Ljava/lang/SecurityException;");
        static_cast<void>(env.PopLocalFrame(1, runtime::JniReference{}));
        memory.release(scratch, 256);
        static_cast<void>(f.app->Stop());
    }
}

TEST_CASE("DVM-221 pixel formats use API19 stride and native packed layout") {
    using namespace ogplay;
    OrchestratedApp f("fixture.DelayedContentActivity", true, false);
    const auto access = f.app->NativeProcess().GuestMemoryAccess();
    for (const auto spec : {std::array<std::uint32_t, 5>{1, 8, 0x80000000U, 0x80, 0},
                            std::array<std::uint32_t, 5>{3, 4, 0xffff0000U, 0, 0xf8},
                            std::array<std::uint32_t, 5>{4, 7, 0xff00ff00U, 0x0f, 0x0f}}) {
        runtime::BitmapPixels pixels(1, 2, static_cast<std::int32_t>(spec[0]), {spec[2], spec[2]});
        CHECK(pixels.Format() == static_cast<std::int32_t>(spec[1])); CHECK(pixels.Stride() == 4);
        const auto address = pixels.Lock(access);
        std::array<std::byte, 8> bytes{}; access.read(address, bytes);
        CHECK(bytes[0] == static_cast<std::byte>(spec[3])); CHECK(bytes[4] == bytes[0]);
        if (spec[0] != 1) { CHECK(bytes[1] == static_cast<std::byte>(spec[4])); CHECK(bytes[5] == bytes[1]); }
        CHECK(bytes[2] == std::byte{0}); CHECK(bytes[3] == std::byte{0});
        CHECK(pixels.Snapshot() == std::vector<std::uint32_t>{spec[2], spec[2]});
        pixels.Unlock(); CHECK_FALSE(access.validate(address, 8));
    }
    runtime::BitmapPixels failed(1, 1, 5);
    auto unavailable = access; unavailable.allocate = [](std::uint32_t) { return memory::GuestAddress{}; };
    CHECK_THROWS_AS(static_cast<void>(failed.Lock(unavailable)), std::bad_alloc);
    CHECK(failed.Locks() == 0); CHECK(failed.Snapshot() == std::vector<std::uint32_t>{0});
    static_cast<void>(f.app->Stop());
}

TEST_CASE("DVM-221 real ARM NDK Bitmap shares Java pixels and survives GC and recycle") {
    using namespace ogplay;
    using namespace runtime::dexvm;
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        OrchestratedApp f("fixture.DelayedContentActivity", true, false, {}, {}, true, backend);
        auto& bridge = f.app->DexVm(); auto& vm = bridge.Vm(); auto& linker = bridge.Linker();
        auto& process = f.app->NativeProcess(); auto& env = process.Environment();
        const auto access = process.GuestMemoryAccess();
        const auto scratch = access.allocate(256);
        std::array<std::byte, 256> bytes{};
        const std::string library = "libjnigraphics.so", symbol = "AndroidBitmap_getInfo";
        std::memcpy(bytes.data() + 64, library.c_str(), library.size() + 1);
        std::memcpy(bytes.data() + 100, symbol.c_str(), symbol.size() + 1);
        access.write(scratch, bytes);
        const auto symbols = runtime::detail::BuildAndroidBoundarySymbols();
        const runtime::BionicHleSymbolProvider provider(symbols);
        const auto invoke = [&](const char* library_name, const char* name, std::array<std::uint32_t, 4> args) {
            const auto target = provider.Lookup(library_name, name); REQUIRE(target.has_value());
            return process.Invoke({*target, args, {}}).return_value;
        };
        const auto handle = invoke("libdl.so", "dlopen", {scratch.Add(64).Value(), 2, 0, 0});
        CHECK(handle != 0);
        CHECK(invoke("libdl.so", "dlsym", {handle, scratch.Add(100).Value(), 0, 0}) ==
              provider.Lookup("libjnigraphics.so", "AndroidBitmap_getInfo")->Value());
        const auto stat = [&](const char* type, const char* name, const char* sig, std::vector<VmValue> args) {
            const auto id = linker.FindDirectMethod(linker.ResolveDescriptor(type), name, sig);
            REQUIRE(id.has_value()); const auto result = vm.Call(*id, args);
            REQUIRE_MESSAGE(!result.exception.IsValid(), result.exception_message); return result.value;
        };
        const auto config = stat("Landroid/graphics/Bitmap$Config;", "valueOf",
            "(Ljava/lang/String;)Landroid/graphics/Bitmap$Config;", {VmValue::Ref(vm.NewStringUtf8("ARGB_8888"))}).ref;
        const auto bitmap = stat("Landroid/graphics/Bitmap;", "createBitmap",
            "(IILandroid/graphics/Bitmap$Config;)Landroid/graphics/Bitmap;",
            {VmValue::Int(2), VmValue::Int(1), VmValue::Ref(config)}).ref;
        auto reference = bridge.PublishLocal(bitmap);
        const auto on = [&](const char* name, const char* sig, std::vector<VmValue> args) {
            const auto type = vm.Model().ObjectClass(bitmap); const auto index = linker.FindVtableIndex(type, name, sig);
            REQUIRE(index.has_value()); args.insert(args.begin(), VmValue::Ref(bitmap));
            const auto result = vm.Call(linker.Class(type).vtable[*index], args);
            REQUIRE_FALSE(result.exception.IsValid()); return result.value;
        };
        const auto colors = vm.Model().NewPrimitiveArray(linker.ResolveDescriptor("[I"), runtime::JniPrimitiveKind::integer, 2);
        const auto colors_root = vm.ProtectReferences(std::array{colors});
        vm.Model().SetPrimitiveElement(colors, 0, 0x80804020U); vm.Model().SetPrimitiveElement(colors, 1, 0xff112233U);
        on("setPixels", "([IIIIIII)V", {VmValue::Ref(colors), VmValue::Int(0), VmValue::Int(2),
            VmValue::Int(0), VmValue::Int(0), VmValue::Int(2), VmValue::Int(1)});
        const auto ndk = [&](const char* name, std::uint32_t out = 0) {
            return invoke("libjnigraphics.so", name, {process.GuestEnvironment().Value(), reference.Value(), out, 0});
        };
        const auto word = [&](std::uint32_t offset) {
            std::array<std::byte, 4> out{}; access.read(scratch.Add(offset), out);
            std::uint32_t value{}; for (std::size_t i = 0; i < 4; ++i) value |= std::to_integer<std::uint32_t>(out[i]) << (8U * i);
            return value;
        };
        CHECK(ndk("AndroidBitmap_getInfo", scratch.Value()) == 0);
        CHECK(word(0) == 2); CHECK(word(4) == 1); CHECK(word(8) == 8); CHECK(word(12) == 1); CHECK(word(16) == 0);
        CHECK(ndk("AndroidBitmap_getInfo") == 0);
        CHECK(ndk("AndroidBitmap_lockPixels", 1) == UINT32_MAX);
        CHECK(f.context->bitmaps.at(bitmap.Value()).pixels->Locks() == 0);
        CHECK(ndk("AndroidBitmap_lockPixels", scratch.Add(24).Value()) == 0);
        const memory::GuestAddress pixels{word(24)}; CHECK(!pixels.IsNull());
        std::array<std::byte, 8> actual{}; access.read(pixels, actual);
        CHECK(actual == std::array<std::byte, 8>{std::byte{64}, std::byte{32}, std::byte{16}, std::byte{128},
            std::byte{17}, std::byte{34}, std::byte{51}, std::byte{255}});
        CHECK(ndk("AndroidBitmap_lockPixels", scratch.Add(28).Value()) == 0); CHECK(word(28) == pixels.Value());
        env.DeleteLocalRef(1, reference);
        static_cast<void>(vm.CollectGarbage("ndk-bitmap-lock-root"));
        CHECK(f.context->bitmap_lock_roots.contains(bitmap.Value()));
        reference = bridge.PublishLocal(bitmap);
        const std::array changed{std::byte{5}, std::byte{6}, std::byte{7}, std::byte{255}};
        access.write(pixels, changed);
        on("getPixels", "([IIIIIII)V", {VmValue::Ref(colors), VmValue::Int(0), VmValue::Int(2),
            VmValue::Int(0), VmValue::Int(0), VmValue::Int(2), VmValue::Int(1)});
        CHECK(vm.Model().GetPrimitiveElement(colors, 0) == 0xff050607U);
        vm.Model().SetPrimitiveElement(colors, 0, 0xffaabbccU);
        on("setPixels", "([IIIIIII)V", {VmValue::Ref(colors), VmValue::Int(0), VmValue::Int(2),
            VmValue::Int(0), VmValue::Int(0), VmValue::Int(2), VmValue::Int(1)});
        access.read(pixels, actual);
        CHECK(actual[0] == std::byte{0xaa}); CHECK(actual[1] == std::byte{0xbb}); CHECK(actual[2] == std::byte{0xcc});
        CHECK(ndk("AndroidBitmap_unlockPixels") == 0); CHECK(f.context->bitmap_lock_roots.contains(bitmap.Value()));
        CHECK(ndk("AndroidBitmap_unlockPixels") == 0); CHECK_FALSE(f.context->bitmap_lock_roots.contains(bitmap.Value()));
        CHECK(f.context->bitmaps.at(bitmap.Value()).pixels->Snapshot()[0] == 0xffaabbccU);
        CHECK(ndk("AndroidBitmap_unlockPixels") == UINT32_MAX - 1);
        REQUIRE(env.ExceptionCheck(1)); const auto pending = env.ExceptionOccurred(1);
        CHECK(ndk("AndroidBitmap_getInfo", scratch.Value()) == UINT32_MAX - 1);
        const auto repeated = env.ExceptionOccurred(1); env.ExceptionClear(1);
        CHECK(bridge.FromReference(repeated) == bridge.FromReference(pending));
        CHECK(ndk("AndroidBitmap_lockPixels", scratch.Add(24).Value()) == 0);
        const memory::GuestAddress held{word(24)};
        on("recycle", "()V", {}); access.read(held, actual);
        CHECK(ndk("AndroidBitmap_lockPixels", scratch.Add(24).Value()) == UINT32_MAX - 1);
        CHECK(env.ExceptionCheck(1)); env.ExceptionClear(1);
        CHECK(ndk("AndroidBitmap_unlockPixels") == 0);
        CHECK_FALSE(f.context->bitmap_lock_roots.contains(bitmap.Value()));
        CHECK(invoke("libdl.so", "dlclose", {handle, 0, 0, 0}) == 0);
        access.release(scratch, 256); static_cast<void>(f.app->Stop());
    }
}

TEST_CASE("InputDevice production directory agrees with dispatched keyboard identity") {
    using namespace ogplay;
    namespace dx = runtime::dexvm;
    OrchestratedApp fixture("fixture.LauncherActivity", true, false);
    fixture.app->StartApplication();
    REQUIRE(fixture.app->StartLauncherActivity().state == session::LifecycleRunState::running);
    auto& vm = fixture.app->DexVm().Vm();
    auto& linker = vm.Linker();
    const auto type = linker.ResolveDescriptor("Landroid/view/InputDevice;");
    const auto get_device = linker.FindDirectMethod(type, "getDevice", "(I)Landroid/view/InputDevice;");
    REQUIRE(get_device.has_value());
    const auto outcome = vm.Call(*get_device, std::array{dx::VmValue::Int(runtime::kAndroidKeyboardDeviceId)});
    REQUIRE_FALSE(outcome.exception.IsValid());
    REQUIRE(outcome.value.ref.IsValid());
    const auto root = vm.ProtectReferences(std::array{outcome.value.ref});
    const auto query_id = runtime::android_intrinsics::CallAndroidMethod(vm, outcome.value.ref, "getId", "()I").AsInt();
    std::int32_t event_id = 1234;
    const auto activity_type = vm.Model().ObjectClass(fixture.context->activity);
    const auto slot = linker.FindVtableIndex(activity_type, "onKeyDown", "(ILandroid/view/KeyEvent;)Z");
    REQUIRE(slot.has_value());
    auto& handler = linker.MutableMethod(linker.Class(activity_type).vtable[*slot]);
    handler.kind = dx::MethodKind::intrinsic;
    handler.implementation = [&](dx::IntrinsicContext& call) {
        event_id = runtime::android_intrinsics::CallAndroidMethod(call.vm, call.arguments[1].ref,
            "getDeviceId", "()I").AsInt();
        return dx::VmValue::Int(1);
    };
    fixture.app->ActivityLifecycle().QueueInput({.type = runtime::AndroidBoundaryInputType::key,
        .code = 29, .pressed = true, .device_id = 1234});
    static_cast<void>(fixture.app->ActivityLifecycle().StepFrame());
    CHECK(event_id == query_id);
    CHECK(event_id == runtime::kAndroidKeyboardDeviceId);
    CHECK(fixture.context->input_devices.size() == 2);
    static_cast<void>(fixture.app->Stop());
}
