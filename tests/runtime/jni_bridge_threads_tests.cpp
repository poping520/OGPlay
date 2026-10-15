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

TEST_CASE("JNI native-only attachments detach without a DexVM monitor context") {
    using namespace ogplay::runtime;
    using namespace ogplay::runtime::dexvm;
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ApplicationProcess f(backend);
        auto& env = f.session->Environment();
        JniJavaVm java_vm(env);
        for (const bool daemon : {false, true}) {
            const auto attached = daemon ? java_vm.AttachCurrentThreadAsDaemon(5, kJniVersion1_6)
                                         : java_vm.AttachCurrentThread(5, kJniVersion1_6);
            REQUIRE(attached.status == JniStatus::ok);
            CHECK(java_vm.AttachCurrentThread(5, kJniVersion1_6).environment == attached.environment);
            CHECK(env.IsThreadAttached(5));
            CHECK(java_vm.DetachCurrentThread(5) == JniStatus::ok);
            CHECK_FALSE(env.IsThreadAttached(5));
            CHECK(java_vm.GetEnv(5, kJniVersion1_6).status == JniStatus::detached);
            CHECK(java_vm.DetachCurrentThread(5) == JniStatus::detached);
        }
    }
}

TEST_CASE("JNI native attachments isolate Java execution identity and detach cleanup") {
    using namespace ogplay::runtime;
    using namespace ogplay::runtime::dexvm;
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ApplicationProcess f(backend);
        auto& vm = f.bridge->Vm();
        auto& model = f.bridge->Model();
        auto& session = *f.session;
        auto& env = session.Environment();
        JniJavaVm java_vm(env);
        const auto thread_class = session.Classes().FindClass("java/lang/Thread");
        const auto long_class = session.Classes().FindClass("java/lang/Long");
        REQUIRE(thread_class.has_value());
        REQUIRE(long_class.has_value());
        const auto current = session.Classes().GetMethodId(*thread_class, "currentThread", "()Ljava/lang/Thread;", true);
        const auto parse = session.Classes().GetMethodId(*long_class, "parseLong", "(Ljava/lang/String;)J", true);
        const auto is_daemon = session.Classes().GetMethodId(*thread_class, "isDaemon", "()Z", false);
        REQUIRE(current.has_value());
        REQUIRE(parse.has_value());
        REQUIRE(is_daemon.has_value());
        const auto root = std::get<JniReference>(session.Invocations().InvokeStatic(
            1, *thread_class, *current, {}, JniArgumentSource::value_array));
        const auto root_identity = env.ResolveObjectForHle(1, root);
        const auto baseline = vm.StackSnapshot().size();
        std::uint64_t previous_token = 1;
        for (const bool daemon : {false, true}) {
            const auto attached = daemon ? java_vm.AttachCurrentThreadAsDaemon(5, kJniVersion1_6)
                                         : java_vm.AttachCurrentThread(5, kJniVersion1_6);
            REQUIRE(attached.status == JniStatus::ok);
            const auto result = std::async(std::launch::async, [&] {
                const auto ref = std::get<JniReference>(session.Invocations().InvokeStatic(
                    5, *thread_class, *current, {}, JniArgumentSource::value_array));
                const auto identity = env.ResolveObjectForHle(5, ref);
                REQUIRE(identity.has_value());
                CHECK(identity != root_identity);
                const auto object = model.FromIdentity(*identity);
                CHECK(f.bridge->Threads().IsAlive(object));
                CHECK(std::get<JniBoolean>(session.Invocations().InvokeVirtual(
                    5, ref, *thread_class, *is_daemon, {}, JniArgumentSource::value_array)) == (daemon ? 1 : 0));
                const auto text = vm.NewStringUtf8("4886718345");
                const auto text_ref = env.PublishLocalObject(5, model.ToIdentity(text));
                const std::array<JniValue, 1> args{text_ref};
                CHECK(std::get<JniLong>(session.Invocations().InvokeStatic(
                    5, *long_class, *parse, args, JniArgumentSource::value_array)) == INT64_C(4886718345));
                const auto repeated = std::get<JniReference>(session.Invocations().InvokeStatic(
                    5, *thread_class, *current, {}, JniArgumentSource::value_array));
                CHECK(env.IsSameObject(5, ref, repeated));
                env.MonitorEnter(5, ref);
                env.MonitorEnter(5, ref);
                return object;
            }).get();
            const auto stacks = vm.StackSnapshot();
            CHECK(stacks.size() == baseline + 1);
            const auto token = stacks.back().context_token;
            CHECK(token > previous_token);
            previous_token = token;
            CHECK(vm.Monitors().HeldCount(token) == 1);
            CHECK(java_vm.DetachCurrentThread(5) == JniStatus::ok);
            CHECK_FALSE(env.IsThreadAttached(5));
            CHECK_FALSE(f.bridge->Threads().IsAlive(result));
            CHECK(vm.Monitors().HeldCount(token) == 0);
            CHECK(vm.StackSnapshot().size() == baseline);
            CHECK_FALSE(env.ExceptionCheck(1));
        }
        // The limit applies to live attachments; released slots are reusable.
        for (std::uint64_t id = 200; id < 264; ++id) {
            REQUIRE(java_vm.AttachCurrentThread(id, kJniVersion1_6).status == JniStatus::ok);
            const auto ref = std::get<JniReference>(session.Invocations().InvokeStatic(
                id, *thread_class, *current, {}, JniArgumentSource::value_array));
            CHECK_FALSE(ref.IsNull());
        }
        REQUIRE(java_vm.AttachCurrentThread(264, kJniVersion1_6).status == JniStatus::ok);
        CHECK_THROWS_WITH(static_cast<void>(session.Invocations().InvokeStatic(
            264, *thread_class, *current, {}, JniArgumentSource::value_array)),
            doctest::Contains("native JNI execution context limit exceeded"));
        for (std::uint64_t id = 200; id <= 264; ++id)
            CHECK(java_vm.DetachCurrentThread(id) == JniStatus::ok);
        CHECK(vm.StackSnapshot().size() == baseline);
    }
}

TEST_CASE("DVM-111 scheduled tasks use VM interface assignability in shared JNI arrays") {
    using namespace ogplay;
    using namespace runtime::dexvm;
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ApplicationProcess fixture(backend);
        auto& vm = fixture.bridge->Vm();
        auto& linker = fixture.bridge->Linker();
        auto& model = vm.Model();
        std::vector<Interpreter::RootScope> roots;
        struct StopWorkers final {
            VmThreadRuntime& threads;
            ~StopWorkers() { threads.Shutdown(); }
        } stop_workers{fixture.bridge->Threads()};
        {
            VmExecutionLockScope lock(vm.ExecutionLock());
            auto keep = [&](VmObjectRef ref) {
                roots.push_back(vm.ProtectReferences(std::array{ref}));
                return ref;
            };
            auto direct = [&](const char* owner, const char* name, const char* signature,
                              std::vector<VmValue> args) {
                const auto method = linker.FindDirectMethod(linker.ResolveDescriptor(owner), name, signature);
                REQUIRE(method);
                const auto result = vm.Call(*method, args);
                REQUIRE_MESSAGE(!result.exception.IsValid(), result.exception_message);
                return result.value;
            };
            auto call = [&](VmObjectRef receiver, const char* name, const char* signature,
                            std::vector<VmValue> args = {}) {
                const auto type = model.ObjectClass(receiver);
                const auto slot = linker.FindVtableIndex(type, name, signature);
                REQUIRE(slot);
                args.insert(args.begin(), VmValue::Ref(receiver));
                const auto result = vm.Call(linker.Class(type).vtable[*slot], args);
                REQUIRE_MESSAGE(!result.exception.IsValid(), result.exception_message);
                return result.value;
            };
            const auto pool = keep(vm.NewIntrinsicInstance("Ljava/util/concurrent/ScheduledThreadPoolExecutor;"));
            direct("Ljava/util/concurrent/ScheduledThreadPoolExecutor;", "<init>", "(I)V",
                   {VmValue::Ref(pool), VmValue::Int(1)});
            const auto runnable = keep(vm.NewIntrinsicInstance("Ljava/lang/Thread;"));
            direct("Ljava/lang/Thread;", "<init>", "()V", {VmValue::Ref(runnable)});
            const auto unit = keep(direct("Ljava/util/concurrent/TimeUnit;", "valueOf",
                "(Ljava/lang/String;)Ljava/util/concurrent/TimeUnit;",
                {VmValue::Ref(vm.NewStringUtf8("DAYS"))}).ref);
            // The real BootDex queue stores ScheduledFutureTask into RunnableScheduledFuture[].
            const auto future = keep(call(pool, "schedule",
                "(Ljava/lang/Runnable;JLjava/util/concurrent/TimeUnit;)Ljava/util/concurrent/ScheduledFuture;",
                {VmValue::Ref(runnable), VmValue::Long(1), VmValue::Ref(unit)}).ref);
            const auto task_type = model.ObjectClass(future);
            for (const auto* descriptor : {"Ljava/util/concurrent/RunnableScheduledFuture;",
                    "Ljava/util/concurrent/RunnableFuture;", "Ljava/util/concurrent/Future;",
                    "Ljava/util/concurrent/Delayed;", "Ljava/lang/Comparable;", "Ljava/lang/Runnable;"}) {
                const auto element = linker.ResolveDescriptor(descriptor);
                CHECK(linker.IsAssignable(element, task_type));
                const auto array = keep(model.NewObjectArray(
                    linker.ResolveDescriptor(std::string("[") + descriptor), element, 1));
                model.SetObjectElement(array, 0, future);
                CHECK(model.GetObjectElement(array, 0) == future);
            }
            const auto element = linker.ResolveDescriptor("Ljava/util/concurrent/RunnableScheduledFuture;");
            const auto typed = keep(model.NewObjectArray(
                linker.ResolveDescriptor("[Ljava/util/concurrent/RunnableScheduledFuture;"), element, 1));
            auto& arrays = fixture.session->Objects().ObjectArrays();
            const auto host_only = fixture.session->Classes().RegisterClass(
                {"fixture/HostOnly", "java/lang/Object", {}, {}});
            const runtime::JniObjectValue host_value{runtime::AllocateJniHostObjectIdentity(), host_only};
            const auto host_array = arrays.New(host_only, 1, host_value);
            CHECK(arrays.Get(host_array, 0) == host_value);
            CHECK_THROWS_AS(arrays.Set(model.ToIdentity(typed), 0, host_value), runtime::JniObjectArrayError);
            arrays.Delete(host_array);
            const auto task_identity = fixture.bridge->RegisteredClassIdentity(task_type);
            const auto thread_identity = fixture.bridge->RegisteredClassIdentity(model.ObjectClass(runnable));
            REQUIRE(task_identity);
            REQUIRE(thread_identity);
            const runtime::JniObjectValue value{model.ToIdentity(future), *task_identity};
            // Native-side initial values and updates follow the same authoritative VM relation.
            const auto native_array = arrays.New(arrays.ElementClass(model.ToIdentity(typed)), 1, value);
            arrays.Set(model.ToIdentity(typed), 0, value);
            CHECK_THROWS_AS(arrays.Set(model.ToIdentity(typed), 0,
                runtime::JniObjectValue{model.ToIdentity(runnable), *thread_identity}), runtime::JniObjectArrayError);
            CHECK(arrays.Get(model.ToIdentity(typed), 0) == value);
            arrays.Delete(native_array);
            static_cast<void>(vm.CollectGarbage());
            CHECK(model.GetObjectElement(typed, 0) == future);
            CHECK(call(future, "isDone", "()Z").AsInt() == 0);
            const auto pending = keep(call(pool, "shutdownNow", "()Ljava/util/List;").ref);
            CHECK(call(pending, "size", "()I").AsInt() == 1);
            CHECK(call(pending, "get", "(I)Ljava/lang/Object;", {VmValue::Int(0)}).ref == future);
            CHECK(call(future, "cancel", "(Z)Z", {VmValue::Int(0)}).AsInt() == 1);
        }
        fixture.bridge->Threads().Shutdown();
        CHECK_FALSE(fixture.bridge->Threads().TakeFailure().has_value());
    }
}
