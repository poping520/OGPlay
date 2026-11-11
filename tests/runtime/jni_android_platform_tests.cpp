#include "dexvm/resource_fixture.h"
#include "../dexvm/file_vfs_fixture.h"
#include "ogplay/runtime/raster/bitmap.h"
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

TEST_CASE("DVM-239 ARM JNI Debug query observes virtual RAM growth and release") {
    using namespace ogplay;
    using namespace runtime::dexvm;
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        OrchestratedApp f("fixture.DelayedContentActivity", true, false, {}, {}, true, backend);
        f.app->StartApplication();
        f.RunOnMainThread([&] {
            auto& bridge = f.app->DexVm();
            auto& vm = bridge.Vm();
            auto& linker = vm.Linker();
            auto& process = f.app->NativeProcess();
            auto& env = process.Environment();
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
            REQUIRE(method != 0);
            REQUIRE_FALSE(env.ExceptionCheck(1));
            for (std::size_t i = 0; i < 4; ++i)
                bytes[128 + i] = static_cast<std::byte>(reference.Value() >> (i * 8));
            memory.write(scratch, bytes);
            const auto query = [&] {
                invoke("CallStaticVoidMethodA",
                       {process.GuestEnvironment().Value(), clazz.Value(), method, scratch.Add(128).Value()});
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
            env.DeleteLocalRef(1, reference);
            env.DeleteLocalRef(1, clazz);
            memory.release(scratch, 256);
        });
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
        f.RunOnMainThread([&] {
            auto& bridge = f.app->DexVm();
            auto& vm = bridge.Vm();
            auto& linker = vm.Linker();
            auto& process = f.app->NativeProcess();
            auto& env = process.Environment();
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
            const auto slot = linker.FindVtableIndex(vm.Model().ObjectClass(base), "getPackageManager",
                                                     "()Landroid/content/pm/PackageManager;");
            REQUIRE(slot.has_value());
            const auto result =
                vm.Call(linker.Class(vm.Model().ObjectClass(base)).vtable[*slot], std::array{VmValue::Ref(base)});
            REQUIRE_FALSE(result.exception.IsValid());
            const auto manager = result.value.ref;
            const auto reference = bridge.PublishLocal(manager);
            const auto clazz = bridge.PublishLocal(vm.Model().ClassObject(vm.Model().ObjectClass(manager)));
            const auto invoke = [&](const char* name, std::array<std::uint32_t, 4> args) {
                return process.Invoke({memory::GuestAddress{EnvironmentThunk(name)}, args, {}}).return_value;
            };
            const auto method = invoke("GetMethodID", {process.GuestEnvironment().Value(), clazz.Value(),
                                                       scratch.Value(), scratch.Add(32).Value()});
            REQUIRE(method != 0);
            REQUIRE_FALSE(env.ExceptionCheck(1));
            const auto list_reference = runtime::JniReference{
                invoke("CallObjectMethodA",
                       {process.GuestEnvironment().Value(), reference.Value(), method, scratch.Add(128).Value()})};
            REQUIRE_FALSE(env.ExceptionCheck(1));
            const auto list = bridge.FromReference(list_reference);
            const auto roots = vm.ProtectReferences(std::array{list});
            CHECK(linker.Class(vm.Model().ObjectClass(list)).is_boot_dex);
            const auto get = linker.FindVtableIndex(vm.Model().ObjectClass(list), "get", "(I)Ljava/lang/Object;");
            REQUIRE(get.has_value());
            const auto item = vm.Call(linker.Class(vm.Model().ObjectClass(list)).vtable[*get],
                                      std::array{VmValue::Ref(list), VmValue::Int(0)});
            REQUIRE_FALSE(item.exception.IsValid());
            const auto source =
                linker.FindFieldRecursive(vm.Model().ObjectClass(item.value.ref), "sourceDir", "Ljava/lang/String;");
            REQUIRE(source.has_value());
            const auto path = VmObjectRef{vm.Model().InstanceSlots(item.value.ref)[linker.Field(*source).slot].bits};
            CHECK(vm.StringUtf8(path) == f.context->package_resource_path);
            CHECK(vm.StringUtf8(path) == "/data/app/fixture-1.apk");
            const auto fd = f.filesystem.Open(vm.StringUtf8(path), {.read = true});
            std::array<std::byte, 4> apk_header{};
            CHECK(f.filesystem.Read(fd, apk_header) == apk_header.size());
            CHECK(std::equal(apk_header.begin(), apk_header.end(), f.context->ApkBytes().begin()));
            f.filesystem.Close(fd);
            bytes[129] = std::byte{0x20};  // GET_UNINSTALLED_PACKAGES (8192).
            memory.write(scratch, bytes);
            const auto uninstalled_reference = runtime::JniReference{
                invoke("CallObjectMethodA",
                       {process.GuestEnvironment().Value(), reference.Value(), method, scratch.Add(128).Value()})};
            REQUIRE_FALSE(env.ExceptionCheck(1));
            const auto uninstalled_list = bridge.FromReference(uninstalled_reference);
            const auto uninstalled_roots = vm.ProtectReferences(std::array{uninstalled_list});
            const auto uninstalled_item = vm.Call(linker.Class(vm.Model().ObjectClass(uninstalled_list)).vtable[*get],
                                                  std::array{VmValue::Ref(uninstalled_list), VmValue::Int(0)});
            REQUIRE_FALSE(uninstalled_item.exception.IsValid());
            const auto uninstalled_path =
                VmObjectRef{vm.Model().InstanceSlots(uninstalled_item.value.ref)[linker.Field(*source).slot].bits};
            CHECK(vm.StringUtf8(uninstalled_path) == f.context->package_resource_path);
            const auto package_field = linker.FindFieldRecursive(vm.Model().ObjectClass(uninstalled_item.value.ref),
                                                                 "packageName", "Ljava/lang/String;");
            REQUIRE(package_field.has_value());
            CHECK(vm.StringUtf8(VmObjectRef{
                      vm.Model().InstanceSlots(uninstalled_item.value.ref)[linker.Field(*package_field).slot].bits}) ==
                  f.context->package_name);
            env.DeleteLocalRef(1, uninstalled_reference);
            // GET_SHARED_LIBRARY_FILES is outside the declared bounded query.
            bytes[129] = std::byte{4};
            memory.write(scratch, bytes);
            CHECK(invoke("CallObjectMethodA", {process.GuestEnvironment().Value(), reference.Value(), method,
                                               scratch.Add(128).Value()}) == 0);
            REQUIRE(env.ExceptionCheck(1));
            const auto throwable = env.ExceptionOccurred(1);
            env.ExceptionClear(1);
            CHECK(linker.Class(vm.Model().ObjectClass(bridge.FromReference(throwable))).descriptor ==
                  "Ljava/lang/UnsupportedOperationException;");
            env.DeleteLocalRef(1, throwable);
            env.DeleteLocalRef(1, list_reference);
            env.DeleteLocalRef(1, reference);
            env.DeleteLocalRef(1, clazz);
            memory.release(scratch, 256);
        });
        static_cast<void>(f.app->Stop());
    }
}

TEST_CASE("DVM-247 ARM JNI inherited inflater lookup Context and native resource failure") {
    using namespace ogplay;
    using namespace runtime;
    using namespace runtime::dexvm;
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        RomValueProcess f(backend);
        auto& bridge = *f.bridge;
        auto& vm = bridge.Vm();
        auto& linker = vm.Linker();
        auto& process = f.session->Process();
        auto& env = f.session->Environment();
        const auto memory = process.GuestMemoryAccess();
        const auto scratch = memory.allocate(512);
        env.PushLocalFrame(1, 32);
        const auto base = test::MakeRomAppContext(vm, f.context);
        const auto base_ref = bridge.PublishLocal(base);
        const auto activity = vm.NewIntrinsicInstance("Landroid/app/Activity;");
        const auto activity_ref = bridge.PublishLocal(activity);
        const auto attach = *linker.FindVtableIndex(vm.Model().ObjectClass(activity), "attachBaseContext",
                                                    "(Landroid/content/Context;)V");
        REQUIRE_FALSE(vm.Call(linker.Class(vm.Model().ObjectClass(activity)).vtable[attach],
                              std::array{VmValue::Ref(activity), VmValue::Ref(base)})
                          .exception.IsValid());
        test::AttachViewTestContext(vm, activity, f.context);
        const auto invoke = [&](const char* name, std::array<std::uint32_t, 4> args) {
            return process.Invoke({memory::GuestAddress{EnvironmentThunk(name)}, args, {}}).return_value;
        };
        const auto method = [&](VmObjectRef object, const char* name, const char* descriptor) {
            const auto klass = bridge.PublishLocal(vm.Model().ClassObject(vm.Model().ObjectClass(object)));
            std::array<std::byte, 256> data{};
            std::memcpy(data.data(), name, std::strlen(name) + 1);
            std::memcpy(data.data() + 64, descriptor, std::strlen(descriptor) + 1);
            memory.write(scratch, data);
            const auto id = invoke("GetMethodID", {process.GuestEnvironment().Value(), klass.Value(), scratch.Value(),
                                                   scratch.Add(64).Value()});
            REQUIRE(id != 0);
            REQUIRE_FALSE(env.ExceptionCheck(1));
            return id;
        };
        const auto getter = method(activity, "getLayoutInflater", "()Landroid/view/LayoutInflater;");
        const auto query = [&](JniReference object, std::uint32_t id) {
            const auto result =
                JniReference{invoke("CallObjectMethodA", {process.GuestEnvironment().Value(), object.Value(), id,
                                                          scratch.Add(256).Value()})};
            REQUIRE_FALSE(env.ExceptionCheck(1));
            REQUIRE_FALSE(result.IsNull());
            return result;
        };
        const auto inflater_ref = query(activity_ref, getter);
        const auto inflater = bridge.FromReference(inflater_ref);
        CHECK(bridge.FromReference(query(activity_ref, getter)) == inflater);
        const auto get_context = method(inflater, "getContext", "()Landroid/content/Context;");
        CHECK(bridge.FromReference(query(inflater_ref, get_context)) == activity);
        const auto clone =
            method(inflater, "cloneInContext", "(Landroid/content/Context;)Landroid/view/LayoutInflater;");
        std::array<std::byte, 32> args{};
        for (std::size_t i = 0; i < 4; ++i)
            args[i] = static_cast<std::byte>(base_ref.Value() >> (8 * i));
        memory.write(scratch.Add(256), args);
        const auto clone_ref = query(inflater_ref, clone);
        CHECK(bridge.FromReference(clone_ref) != inflater);
        CHECK(bridge.FromReference(query(clone_ref, get_context)) == base);
        static_cast<void>(vm.CollectGarbage("inflater-jni-fields"));
        const auto inflate = method(inflater, "inflate", "(ILandroid/view/ViewGroup;Z)Landroid/view/View;");
        args.fill(std::byte{0});
        args[0] = std::byte{0x7b};
        memory.write(scratch.Add(256), args);
        static_cast<void>(invoke("CallObjectMethodA", {process.GuestEnvironment().Value(), inflater_ref.Value(),
                                                       inflate, scratch.Add(256).Value()}));
        REQUIRE(env.ExceptionCheck(1));
        const auto error_ref = env.ExceptionOccurred(1);
        env.ExceptionClear(1);
        const auto error = bridge.FromReference(error_ref);
        CHECK(linker.Class(vm.Model().ObjectClass(error)).descriptor ==
              "Landroid/content/res/Resources$NotFoundException;");
        static_cast<void>(env.PopLocalFrame(1));
        memory.release(scratch, 512);
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
            auto& bridge = *f.bridge;
            auto& vm = bridge.Vm();
            auto& env = f.session->Environment();
            auto& process = f.session->Process();
            const auto memory = process.GuestMemoryAccess();
            const auto scratch = memory.allocate(256);
            env.PushLocalFrame(1, 32);
            const auto klass =
                bridge.PublishLocal(vm.Model().ClassObject(vm.Linker().ResolveDescriptor("Landroid/os/StatFs;")));
            const auto path = bridge.PublishLocal(vm.NewStringUtf8("/sdcard"));
            const auto invoke = [&](const char* name, std::array<std::uint32_t, 4> args) {
                return process.Invoke({memory::GuestAddress{EnvironmentThunk(name)}, args, {}});
            };
            const auto method = [&](const char* name, const char* signature) {
                std::array<std::byte, 256> bytes{};
                std::memcpy(bytes.data(), name, std::strlen(name) + 1);
                std::memcpy(bytes.data() + 64, signature, std::strlen(signature) + 1);
                for (std::size_t i = 0; i < 4; ++i)
                    bytes[128 + i] = static_cast<std::byte>(path.Value() >> (i * 8));
                memory.write(scratch, bytes);
                const auto id = invoke("GetMethodID", {process.GuestEnvironment().Value(), klass.Value(),
                                                       scratch.Value(), scratch.Add(64).Value()})
                                    .return_value;
                REQUIRE(id != 0);
                REQUIRE_FALSE(env.ExceptionCheck(1));
                return id;
            };
            const auto constructor = method("<init>", "(Ljava/lang/String;)V");
            const auto object = JniReference{invoke("NewObjectA", {process.GuestEnvironment().Value(), klass.Value(),
                                                                   constructor, scratch.Add(128).Value()})
                                                 .return_value};
            REQUIRE_FALSE(object.IsNull());
            REQUIRE_FALSE(env.ExceptionCheck(1));
            const auto available = method("getAvailableBlocksLong", "()J");
            const auto query = [&] {
                const auto value =
                    invoke("CallLongMethodA", {process.GuestEnvironment().Value(), object.Value(), available, 0});
                REQUIRE_FALSE(env.ExceptionCheck(1));
                return (static_cast<std::uint64_t>(value.return_value_high) << 32) | value.return_value;
            };
            CHECK(query() == 16);
            const auto fd = f.filesystem.Open("/sdcard/save", {.write = true, .create = true});
            const std::vector<std::byte> bytes(4096);
            REQUIRE(f.filesystem.Write(fd, bytes) == bytes.size());
            CHECK(query() == 16);
            const auto refresh = method("restat", "(Ljava/lang/String;)V");
            static_cast<void>(invoke("CallVoidMethodA", {process.GuestEnvironment().Value(), object.Value(), refresh,
                                                         scratch.Add(128).Value()}));
            REQUIRE_FALSE(env.ExceptionCheck(1));
            CHECK(query() == 15);
            f.filesystem.Close(fd);
            static_cast<void>(env.PopLocalFrame(1));
            memory.release(scratch, 256);
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
        f.app->StartApplication();
        f.RunOnMainThread([&] {
            auto& bridge = f.app->DexVm();
            auto& vm = bridge.Vm();
            auto& linker = vm.Linker();
            auto& process = f.app->NativeProcess();
            auto& env = process.Environment();
            const auto memory = process.GuestMemoryAccess();
            const auto scratch = memory.allocate(256);
            const auto base = test::MakeRomAppContext(vm, f.context);
            const auto context_root = vm.ProtectReferences(std::array{base});
            const auto service = linker.FindVtableIndex(vm.Model().ObjectClass(base), "getSystemService",
                                                        "(Ljava/lang/String;)Ljava/lang/Object;");
            REQUIRE(service);
            const auto found = vm.Call(linker.Class(vm.Model().ObjectClass(base)).vtable[*service],
                                       std::array{VmValue::Ref(base), VmValue::Ref(vm.NewStringUtf8("connectivity"))});
            REQUIRE_FALSE(found.exception.IsValid());
            const auto manager = found.value.ref;
            REQUIRE(manager.IsValid());
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
                bytes[128] = std::byte{1};  // jvalue networkType=TYPE_WIFI
                memory.write(scratch, bytes);
                const auto clazz = bridge.PublishLocal(vm.Model().ClassObject(vm.Model().ObjectClass(object)));
                const auto method = invoke("GetMethodID", {process.GuestEnvironment().Value(), clazz.Value(),
                                                           scratch.Value(), scratch.Add(64).Value()});
                REQUIRE_FALSE(env.ExceptionCheck(1));
                REQUIRE(method != 0);
                return method;
            };
            const auto query = method_id(manager, "getNetworkInfo", "(I)Landroid/net/NetworkInfo;");
            const auto info_reference = runtime::JniReference{
                invoke("CallObjectMethodA",
                       {process.GuestEnvironment().Value(), reference.Value(), query, scratch.Add(128).Value()})};
            REQUIRE_FALSE(env.ExceptionCheck(1));
            const auto info = bridge.FromReference(info_reference);
            REQUIRE(info.IsValid());
            CHECK(linker.Class(vm.Model().ObjectClass(info)).is_boot_dex);
            const auto connected = method_id(info, "isConnected", "()Z");
            CHECK(invoke("CallBooleanMethodA",
                         {process.GuestEnvironment().Value(), info_reference.Value(), connected, 0}) == 0);
            REQUIRE_FALSE(env.ExceptionCheck(1));
            f.context->granted_permissions.clear();
            CHECK(invoke("CallObjectMethodA", {process.GuestEnvironment().Value(), reference.Value(), query,
                                               scratch.Add(128).Value()}) == 0);
            REQUIRE(env.ExceptionCheck(1));
            const auto exception = env.ExceptionOccurred(1);
            env.ExceptionClear(1);
            CHECK(linker.Class(vm.Model().ObjectClass(bridge.FromReference(exception))).descriptor ==
                  "Ljava/lang/SecurityException;");
            static_cast<void>(env.PopLocalFrame(1, runtime::JniReference{}));
            memory.release(scratch, 256);
        });
        static_cast<void>(f.app->Stop());
    }
}

TEST_CASE("DVM-221 pixel formats use API19 stride and native packed layout") {
    using namespace ogplay;
    OrchestratedApp f("fixture.DelayedContentActivity", true, false);
    for (const auto spec : {std::array<std::uint32_t, 6>{1, 8, 1, 0x80000000U, 0x80, 0},
                            std::array<std::uint32_t, 6>{3, 4, 2, 0xffff0000U, 0, 0xf8},
                            std::array<std::uint32_t, 6>{4, 7, 2, 0xff00ff00U, 0x0f, 0x0f}}) {
        auto backing = std::make_shared<std::array<std::byte, 4>>();
        runtime::raster::Bitmap pixels(1, 2, static_cast<int>(spec[0]), true, true, *backing, backing,
                                       memory::GuestAddress{0x1000});
        pixels.Set(0, spec[3]);
        pixels.Set(1, spec[3]);
        CHECK(pixels.Format() == static_cast<int>(spec[1]));
        CHECK(pixels.Stride() == spec[2]);
        CHECK(pixels.Lock() == memory::GuestAddress{0x1000});
        CHECK((*backing)[0] == static_cast<std::byte>(spec[4]));
        CHECK((*backing)[spec[2]] == (*backing)[0]);
        if (spec[2] > 1) {
            CHECK((*backing)[1] == static_cast<std::byte>(spec[5]));
            CHECK((*backing)[spec[2] + 1] == (*backing)[1]);
        }
        CHECK(pixels.Snapshot() == std::vector<std::uint32_t>{spec[3], spec[3]});
        pixels.Unlock();
    }
    static_cast<void>(f.app->Stop());
}

TEST_CASE("DVM-221 real ARM NDK Bitmap shares Java pixels and survives GC and recycle") {
    using namespace ogplay;
    using namespace runtime::dexvm;
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        OrchestratedApp f("fixture.DelayedContentActivity", true, false, {}, {}, true, backend);
        f.app->StartApplication();
        f.RunOnMainThread([&] {
            auto& bridge = f.app->DexVm();
            auto& vm = bridge.Vm();
            auto& linker = bridge.Linker();
            auto& process = f.app->NativeProcess();
            auto& env = process.Environment();
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
                const auto target = provider.Lookup(library_name, name);
                REQUIRE(target.has_value());
                return process.Invoke({*target, args, {}}).return_value;
            };
            const auto handle = invoke("libdl.so", "dlopen", {scratch.Add(64).Value(), 2, 0, 0});
            CHECK(handle != 0);
            CHECK(invoke("libdl.so", "dlsym", {handle, scratch.Add(100).Value(), 0, 0}) ==
                  provider.Lookup("libjnigraphics.so", "AndroidBitmap_getInfo")->Value());
            const auto stat = [&](const char* type, const char* name, const char* sig, std::vector<VmValue> args) {
                const auto id = linker.FindDirectMethod(linker.ResolveDescriptor(type), name, sig);
                REQUIRE(id.has_value());
                const auto result = vm.Call(*id, args);
                REQUIRE_MESSAGE(!result.exception.IsValid(), result.exception_message);
                return result.value;
            };
            const auto config = stat("Landroid/graphics/Bitmap$Config;", "valueOf",
                                     "(Ljava/lang/String;)Landroid/graphics/Bitmap$Config;",
                                     {VmValue::Ref(vm.NewStringUtf8("ARGB_8888"))})
                                    .ref;
            const auto bitmap = stat("Landroid/graphics/Bitmap;", "createBitmap",
                                     "(IILandroid/graphics/Bitmap$Config;)Landroid/graphics/Bitmap;",
                                     {VmValue::Int(2), VmValue::Int(1), VmValue::Ref(config)})
                                    .ref;
            auto reference = bridge.PublishLocal(bitmap);
            const auto canvas = vm.NewIntrinsicInstance("Landroid/graphics/Canvas;");
            const auto canvas_root = vm.ProtectReferences(std::array{canvas});
            const auto canvas_type = vm.Model().ObjectClass(canvas);
            const auto canvas_ctor = linker.FindDirectMethod(canvas_type, "<init>", "(Landroid/graphics/Bitmap;)V");
            REQUIRE(canvas_ctor);
            REQUIRE_FALSE(
                vm.Call(*canvas_ctor, std::array{VmValue::Ref(canvas), VmValue::Ref(bitmap)}).exception.IsValid());
            const auto draw = linker.FindVtableIndex(canvas_type, "drawColor", "(I)V");
            REQUIRE(draw);
            const auto draw_red = [&] {
                return vm.Call(linker.Class(canvas_type).vtable[*draw],
                               std::array{VmValue::Ref(canvas), VmValue::Int(std::bit_cast<int>(0xffff0000U))});
            };

            const auto on = [&](const char* name, const char* sig, std::vector<VmValue> args) {
                const auto type = vm.Model().ObjectClass(bitmap);
                const auto index = linker.FindVtableIndex(type, name, sig);
                REQUIRE(index.has_value());
                args.insert(args.begin(), VmValue::Ref(bitmap));
                const auto result = vm.Call(linker.Class(type).vtable[*index], args);
                REQUIRE_FALSE(result.exception.IsValid());
                return result.value;
            };
            const auto colors =
                vm.Model().NewPrimitiveArray(linker.ResolveDescriptor("[I"), runtime::JniPrimitiveKind::integer, 2);
            const auto colors_root = vm.ProtectReferences(std::array{colors});
            vm.Model().SetPrimitiveElement(colors, 0, 0x80804020U);
            vm.Model().SetPrimitiveElement(colors, 1, 0xff112233U);
            on("setPixels", "([IIIIIII)V",
               {VmValue::Ref(colors), VmValue::Int(0), VmValue::Int(2), VmValue::Int(0), VmValue::Int(0),
                VmValue::Int(2), VmValue::Int(1)});
            const auto ndk = [&](const char* name, std::uint32_t out = 0) {
                return invoke("libjnigraphics.so", name,
                              {process.GuestEnvironment().Value(), reference.Value(), out, 0});
            };
            const auto word = [&](std::uint32_t offset) {
                std::array<std::byte, 4> out{};
                access.read(scratch.Add(offset), out);
                std::uint32_t value{};
                for (std::size_t i = 0; i < 4; ++i)
                    value |= std::to_integer<std::uint32_t>(out[i]) << (8U * i);
                return value;
            };
            CHECK(ndk("AndroidBitmap_getInfo", scratch.Value()) == 0);
            CHECK(word(0) == 2);
            CHECK(word(4) == 1);
            CHECK(word(8) == 8);
            CHECK(word(12) == 1);
            CHECK(word(16) == 0);
            CHECK(ndk("AndroidBitmap_getInfo") == 0);
            CHECK(ndk("AndroidBitmap_lockPixels", 1) == UINT32_MAX);
            CHECK(f.context->bitmaps.at(bitmap.Value()).pixels->Locks() == 0);
            CHECK(ndk("AndroidBitmap_lockPixels", scratch.Add(24).Value()) == 0);
            const memory::GuestAddress pixels{word(24)};
            const auto buffer_field = linker.FindFieldRecursive(vm.Model().ObjectClass(bitmap), "mBuffer", "[B");
            REQUIRE(buffer_field);
            const auto buffer = VmObjectRef{vm.Model().InstanceSlots(bitmap)[linker.Field(*buffer_field).slot].bits};
            CHECK(pixels == vm.Model().PrimitiveArrayAddress(buffer));
            CHECK(!pixels.IsNull());
            std::array<std::byte, 8> actual{};
            access.read(pixels, actual);
            CHECK(actual == std::array<std::byte, 8>{std::byte{64}, std::byte{32}, std::byte{16}, std::byte{128},
                                                     std::byte{17}, std::byte{34}, std::byte{51}, std::byte{255}});
            CHECK(ndk("AndroidBitmap_lockPixels", scratch.Add(28).Value()) == 0);
            CHECK(word(28) == pixels.Value());
            const auto locked_draw = draw_red();
            REQUIRE(locked_draw.exception.IsValid());
            CHECK(linker.Class(locked_draw.exception_class).descriptor == "Ljava/lang/IllegalStateException;");
            const auto set = linker.FindVtableIndex(canvas_type, "setBitmap", "(Landroid/graphics/Bitmap;)V");
            REQUIRE(set);
            const auto set_bitmap = [&](VmObjectRef target) {
                return vm.Call(linker.Class(canvas_type).vtable[*set],
                               std::array{VmValue::Ref(canvas), VmValue::Ref(target)});
            };
            REQUIRE_FALSE(set_bitmap(VmObjectRef{}).exception.IsValid());
            env.DeleteLocalRef(1, reference);
            static_cast<void>(vm.CollectGarbage("ndk-bitmap-lock-root"));
            CHECK(f.context->bitmap_lock_roots.contains(bitmap.Value()));
            reference = bridge.PublishLocal(bitmap);
            REQUIRE_FALSE(set_bitmap(bitmap).exception.IsValid());
            const std::array changed{std::byte{5}, std::byte{6}, std::byte{7}, std::byte{255}};
            access.write(pixels, changed);
            on("getPixels", "([IIIIIII)V",
               {VmValue::Ref(colors), VmValue::Int(0), VmValue::Int(2), VmValue::Int(0), VmValue::Int(0),
                VmValue::Int(2), VmValue::Int(1)});
            CHECK(vm.Model().GetPrimitiveElement(colors, 0) == 0xff050607U);
            vm.Model().SetPrimitiveElement(colors, 0, 0xffaabbccU);
            on("setPixels", "([IIIIIII)V",
               {VmValue::Ref(colors), VmValue::Int(0), VmValue::Int(2), VmValue::Int(0), VmValue::Int(0),
                VmValue::Int(2), VmValue::Int(1)});
            access.read(pixels, actual);
            CHECK(actual[0] == std::byte{0xaa});
            CHECK(actual[1] == std::byte{0xbb});
            CHECK(actual[2] == std::byte{0xcc});
            CHECK(ndk("AndroidBitmap_unlockPixels") == 0);
            CHECK(f.context->bitmap_lock_roots.contains(bitmap.Value()));
            CHECK(ndk("AndroidBitmap_unlockPixels") == 0);
            CHECK_FALSE(f.context->bitmap_lock_roots.contains(bitmap.Value()));
            CHECK(f.context->bitmaps.at(bitmap.Value()).pixels->Snapshot()[0] == 0xffaabbccU);
            REQUIRE_FALSE(draw_red().exception.IsValid());
            CHECK(f.context->bitmaps.at(bitmap.Value()).pixels->Snapshot()[0] == 0xffff0000U);
            CHECK(ndk("AndroidBitmap_unlockPixels") == UINT32_MAX - 1);
            REQUIRE(env.ExceptionCheck(1));
            const auto pending = env.ExceptionOccurred(1);
            CHECK(ndk("AndroidBitmap_getInfo", scratch.Value()) == UINT32_MAX - 1);
            const auto repeated = env.ExceptionOccurred(1);
            env.ExceptionClear(1);
            CHECK(bridge.FromReference(repeated) == bridge.FromReference(pending));
            CHECK(ndk("AndroidBitmap_lockPixels", scratch.Add(24).Value()) == 0);
            const memory::GuestAddress held{word(24)};
            on("recycle", "()V", {});
            access.read(held, actual);
            CHECK(ndk("AndroidBitmap_lockPixels", scratch.Add(24).Value()) == UINT32_MAX - 1);
            CHECK(env.ExceptionCheck(1));
            env.ExceptionClear(1);
            CHECK(ndk("AndroidBitmap_unlockPixels") == 0);
            CHECK_FALSE(f.context->bitmap_lock_roots.contains(bitmap.Value()));
            CHECK(invoke("libdl.so", "dlclose", {handle, 0, 0, 0}) == 0);
            access.release(scratch, 256);
        });
        static_cast<void>(f.app->Stop());
    }
}

TEST_CASE("InputDevice production directory agrees with dispatched keyboard identity") {
    using namespace ogplay;
    namespace dx = runtime::dexvm;
    OrchestratedApp fixture("fixture.LauncherActivity", true, false);
    fixture.app->ActivityLifecycle().SetRealtimeVideoClock(false);
    fixture.app->StartApplication();
    REQUIRE(fixture.app->StartLauncherActivity().state == session::LifecycleRunState::running);
    static_cast<void>(fixture.app->ActivityLifecycle().StepFrame());
    auto& vm = fixture.app->DexVm().Vm();
    auto& linker = vm.Linker();
    const auto type = linker.ResolveDescriptor("Landroid/view/InputDevice;");
    const auto get_device = linker.FindDirectMethod(type, "getDevice", "(I)Landroid/view/InputDevice;");
    REQUIRE(get_device.has_value());
    const auto outcome =
        fixture.CallGuest(*get_device, std::array{dx::VmValue::Int(runtime::kAndroidKeyboardDeviceId)});
    REQUIRE_FALSE(outcome.exception.IsValid());
    REQUIRE(outcome.value.ref.IsValid());
    const auto root = vm.ProtectReferences(std::array{outcome.value.ref});
    const auto query_id = fixture.CallGuest(outcome.value.ref, "getId", "()I").AsInt();
    std::int32_t event_id = 1234;
    const auto activity_type = vm.Model().ObjectClass(fixture.context->foreground_activity);
    const auto slot = linker.FindVtableIndex(activity_type, "onKeyDown", "(ILandroid/view/KeyEvent;)Z");
    REQUIRE(slot.has_value());
    auto& handler = linker.MutableMethod(linker.Class(activity_type).vtable[*slot]);
    handler.kind = dx::MethodKind::intrinsic;
    handler.implementation = [&](dx::IntrinsicContext& call) {
        event_id = runtime::android_intrinsics::CallAndroidMethod(call.vm, call.arguments[1].ref, "getDeviceId", "()I")
                       .AsInt();
        return dx::VmValue::Int(1);
    };
    fixture.app->ActivityLifecycle().QueueInput(
        {.type = runtime::AndroidBoundaryInputType::key, .code = 29, .pressed = true, .device_id = 1234});
    static_cast<void>(fixture.app->ActivityLifecycle().StepFrame());
    CHECK(event_id == query_id);
    CHECK(event_id == runtime::kAndroidKeyboardDeviceId);
    CHECK(fixture.context->input_devices.size() == 2);
    static_cast<void>(fixture.app->Stop());
}
