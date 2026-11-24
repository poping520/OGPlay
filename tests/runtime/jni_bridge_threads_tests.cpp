#include "runtime/boundary/core/boundary_symbols.h"
#include "ogplay/runtime/dexvm/big_int_runtime.h"
#include <set>
#include "ogplay/runtime/integration/native_activity_runtime.h"
#include "ogplay/runtime/dexvm/guest_memory.h"
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
#include "interpreter_backends.h"

using namespace ogplay::test::native_library;

TEST_CASE("JNI native-only attachments detach without a DexVM monitor context") {
    using namespace ogplay::runtime;
    using namespace ogplay::runtime::dexvm;
    for (const auto backend : ogplay::test::InterpreterBackends()) {
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

TEST_CASE("JNI stopped Java callbacks preserve native thread cancellation") {
    using namespace ogplay::runtime;
    using namespace ogplay::runtime::dexvm;
    for (const auto backend : ogplay::test::InterpreterBackends()) {
        ApplicationProcess f(backend);
        auto& session = *f.session;
        auto& env = session.Environment();
        JniJavaVm java_vm(env);
        const auto type = session.Classes().FindClass("java/lang/Thread");
        REQUIRE(type.has_value());
        const auto method = session.Classes().GetMethodId(*type, "currentThread", "()Ljava/lang/Thread;", true);
        REQUIRE(method.has_value());
        const auto sleep = session.Classes().GetMethodId(*type, "sleep", "(J)V", true);
        REQUIRE(sleep.has_value());
        REQUIRE(java_vm.AttachCurrentThread(5, kJniVersion1_6).status == JniStatus::ok);
        // Establish the attached native thread's Java execution context.
        CHECK_FALSE(std::get<JniReference>(
                        session.Invocations().InvokeStatic(5, *type, *method, {}, JniArgumentSource::value_array))
                        .IsNull());
        const std::array<JniValue, 1> arguments{JniLong{1}};
        f.bridge->Threads().RequestShutdown();
        bool cancelled = false;
        try {
            static_cast<void>(
                session.Invocations().InvokeStatic(5, *type, *sleep, arguments, JniArgumentSource::value_array));
        } catch (const A32GuestCallCancelled& error) {
            cancelled = true;
            CHECK(error.ThreadId() == 5U);
        }
        CHECK(cancelled);
        CHECK_FALSE(env.ExceptionCheck(5));
        CHECK(java_vm.DetachCurrentThread(5) == JniStatus::ok);
    }
}

TEST_CASE("JNI native attachment constructs pending exceptions while the default context is active") {
    using namespace ogplay;
    using namespace runtime;
    for (const auto backend : test::InterpreterBackends()) {
        ApplicationProcess fixture(backend);
        auto& process = fixture.session->Process();
        auto& env = process.Environment();
        auto& vm = fixture.bridge->Vm();
        constexpr std::uint64_t worker_thread = 5;
        const auto memory = process.GuestMemoryAccess();
        const auto scratch = memory.allocate(64);
        const auto write = [&](const std::string& value, std::uint32_t offset = 0) {
            const auto address = scratch.Add(offset);
            memory.write(address, std::as_bytes(std::span{value.c_str(), value.size() + 1}));
            return address.Value();
        };
        auto& lock = vm.ExecutionLock();
        vm.RunInExecutionContext(vm.ExecutionContext(vm.CurrentContextToken()), [&] {
            auto worker = std::async(std::launch::async, [&] {
                process.PrepareDexVmThread(worker_thread, 0);
                const auto call = [&](const char* name, std::uint32_t a = 0, std::uint32_t b = 0, std::uint32_t c = 0) {
                    return process
                        .Invoke({memory::GuestAddress{EnvironmentThunk(name)},
                                 {process.GuestEnvironment().Value(), a, b, c},
                                 {},
                                 worker_thread})
                        .return_value;
                };
                const auto expect_exception = [&](const char* name, const char* message) {
                    CHECK(call("ExceptionCheck") == 1);
                    const auto pending = env.PendingExceptionMetadata(worker_thread);
                    REQUIRE(pending.has_value());
                    CHECK(process.Classes().ClassName(pending->exception_class) == name);
                    CHECK(pending->modified_utf8_message == message);
                    const auto throwable = JniReference{call("ExceptionOccurred")};
                    CHECK(env.ResolveObjectForHle(worker_thread, throwable) == pending->throwable);
                    CHECK(fixture.bridge->Model().FindIdentity(pending->throwable).IsValid());
                    call("ExceptionClear");
                    CHECK(call("ExceptionCheck") == 0);
                };

                CHECK(call("FindClass", write("fixture/Invalid;")) == 0);
                expect_exception("java/lang/NoClassDefFoundError", "fixture/Invalid;");
                const auto object = call("FindClass", write("java/lang/Object"));
                REQUIRE(object != 0);
                CHECK(call("GetMethodID", object, write("missing", 0), write("()V", 32)) == 0);
                expect_exception("java/lang/NoSuchMethodError", "java/lang/Object.missing()V");
                CHECK(call("GetMethodID", object, write("<init>", 0), write("()V", 32)) != 0);
                const auto exception = call("FindClass", write("java/lang/RuntimeException"));
                REQUIRE(exception != 0);
                CHECK(call("ThrowNew", exception, write("worker exception")) == 0);
                expect_exception("java/lang/RuntimeException", "worker exception");
                CHECK(call("FindClass", write("java/lang/Object")) != 0);
                process.ReleaseDexVmThread(worker_thread);
            });
            // Keep the default context active while the worker can take the VM lock and enter JNI.
            const auto depth = lock.ReleaseForBlocking();
            worker.wait();
            lock.ReacquireAfterBlocking(depth);
            worker.get();
        });
        CHECK_FALSE(env.IsThreadAttached(worker_thread));
        CHECK_FALSE(env.ExceptionCheck(AndroidGuestProcess::kRootThreadId));
    }
}

TEST_CASE("JNI native attachments isolate Java execution identity and detach cleanup") {
    using namespace ogplay::runtime;
    using namespace ogplay::runtime::dexvm;
    for (const auto backend : ogplay::test::InterpreterBackends()) {
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
        const auto current =
            session.Classes().GetMethodId(*thread_class, "currentThread", "()Ljava/lang/Thread;", true);
        const auto parse = session.Classes().GetMethodId(*long_class, "parseLong", "(Ljava/lang/String;)J", true);
        const auto is_daemon = session.Classes().GetMethodId(*thread_class, "isDaemon", "()Z", false);
        REQUIRE(current.has_value());
        REQUIRE(parse.has_value());
        REQUIRE(is_daemon.has_value());
        const auto root = std::get<JniReference>(
            session.Invocations().InvokeStatic(1, *thread_class, *current, {}, JniArgumentSource::value_array));
        const auto root_identity = env.ResolveObjectForHle(1, root);
        const auto baseline = vm.StackSnapshot().size();
        std::uint64_t previous_token = 1;
        for (const bool daemon : {false, true}) {
            const auto attached = daemon ? java_vm.AttachCurrentThreadAsDaemon(5, kJniVersion1_6)
                                         : java_vm.AttachCurrentThread(5, kJniVersion1_6);
            REQUIRE(attached.status == JniStatus::ok);
            const auto result =
                std::async(std::launch::async, [&] {
                    const auto ref = std::get<JniReference>(session.Invocations().InvokeStatic(
                        5, *thread_class, *current, {}, JniArgumentSource::value_array));
                    const auto identity = env.ResolveObjectForHle(5, ref);
                    REQUIRE(identity.has_value());
                    CHECK(identity != root_identity);
                    const auto object = model.FromIdentity(*identity);
                    CHECK(f.bridge->Threads().IsAlive(object));
                    CHECK(std::get<JniBoolean>(session.Invocations().InvokeVirtual(
                              5, ref, *thread_class, *is_daemon, {}, JniArgumentSource::value_array)) ==
                          (daemon ? 1 : 0));
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
            const auto ref = std::get<JniReference>(
                session.Invocations().InvokeStatic(id, *thread_class, *current, {}, JniArgumentSource::value_array));
            CHECK_FALSE(ref.IsNull());
        }
        REQUIRE(java_vm.AttachCurrentThread(264, kJniVersion1_6).status == JniStatus::ok);
        CHECK_THROWS_WITH(static_cast<void>(session.Invocations().InvokeStatic(264, *thread_class, *current, {},
                                                                               JniArgumentSource::value_array)),
                          doctest::Contains("native JNI execution context limit exceeded"));
        for (std::uint64_t id = 200; id <= 264; ++id)
            CHECK(java_vm.DetachCurrentThread(id) == JniStatus::ok);
        CHECK(vm.StackSnapshot().size() == baseline);
    }
}

TEST_CASE("DVM-111 scheduled tasks use VM interface assignability in shared JNI arrays") {
    using namespace ogplay;
    using namespace runtime::dexvm;
    for (const auto backend : ogplay::test::InterpreterBackends()) {
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
            auto direct = [&](const char* owner, const char* name, const char* signature, std::vector<VmValue> args) {
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
                                          {VmValue::Ref(vm.NewStringUtf8("DAYS"))})
                                       .ref);
            // The real BootDex queue stores ScheduledFutureTask into RunnableScheduledFuture[].
            const auto future = keep(
                call(pool, "schedule",
                     "(Ljava/lang/Runnable;JLjava/util/concurrent/TimeUnit;)Ljava/util/concurrent/ScheduledFuture;",
                     {VmValue::Ref(runnable), VmValue::Long(1), VmValue::Ref(unit)})
                    .ref);
            const auto task_type = model.ObjectClass(future);
            for (const auto* descriptor :
                 {"Ljava/util/concurrent/RunnableScheduledFuture;", "Ljava/util/concurrent/RunnableFuture;",
                  "Ljava/util/concurrent/Future;", "Ljava/util/concurrent/Delayed;", "Ljava/lang/Comparable;",
                  "Ljava/lang/Runnable;"}) {
                const auto element = linker.ResolveDescriptor(descriptor);
                CHECK(linker.IsAssignable(element, task_type));
                const auto array =
                    keep(model.NewObjectArray(linker.ResolveDescriptor(std::string("[") + descriptor), element, 1));
                model.SetObjectElement(array, 0, future);
                CHECK(model.GetObjectElement(array, 0) == future);
            }
            const auto element = linker.ResolveDescriptor("Ljava/util/concurrent/RunnableScheduledFuture;");
            const auto typed = keep(model.NewObjectArray(
                linker.ResolveDescriptor("[Ljava/util/concurrent/RunnableScheduledFuture;"), element, 1));
            auto& arrays = fixture.session->Objects().ObjectArrays();
            const auto host_only =
                fixture.session->Classes().RegisterClass({"fixture/HostOnly", "java/lang/Object", {}, {}});
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
                                       runtime::JniObjectValue{model.ToIdentity(runnable), *thread_identity}),
                            runtime::JniObjectArrayError);
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
