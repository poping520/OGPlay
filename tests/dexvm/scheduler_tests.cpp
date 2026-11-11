#include <fstream>
#include <iterator>
#include "ogplay/runtime/jni/host_copy_payload_provider.h"
#include "boot_dex.h"
#include "android_value_fixture.h"
#include <doctest/doctest.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "ogplay/core/capability_ledger.h"
#include "ogplay/runtime/dexvm/class_linker.h"
#include "ogplay/runtime/dexvm/interpreter.h"
#include "ogplay/runtime/dexvm/intrinsic_builder.h"
#include "ogplay/runtime/dexvm/object_model.h"
#include "ogplay/runtime/dexvm/vm_monitors.h"
#include "ogplay/runtime/dexvm/vm_threads.h"
#include "ogplay/runtime/integration/dexvm_android.h"

namespace {

using namespace ogplay::runtime;
using namespace ogplay::runtime::dexvm;

std::vector<IntrinsicClassDecl> TestCatalog(std::vector<std::int32_t>* messages, std::vector<std::int64_t>* ticks,
                                            std::vector<std::pair<std::int32_t, VmObjectRef>>* receiver_results,
                                            std::atomic<std::int32_t>* runnable_calls,
                                            std::atomic<std::int32_t>* timer_calls, std::atomic<std::int32_t>* finishes,
                                            std::atomic<std::int32_t>* async_background,
                                            std::atomic<std::int32_t>* async_post,
                                            std::atomic<std::int32_t>* idle_calls) {
    std::vector<IntrinsicClassDecl> result;
    auto handler = IntrinsicClassBuilder::Class("Ltest/RecordingHandler;", "Landroid/os/Handler;");
    handler.OverrideMethod("handleMessage", "(Landroid/os/Message;)V", [messages](IntrinsicContext& call) {
        const auto field =
            call.vm.Linker().FindFieldRecursive(call.vm.Model().ObjectClass(call.arguments[0].ref), "what", "I");
        messages->push_back(static_cast<std::int32_t>(
            call.vm.Model().InstanceSlots(call.arguments[0].ref)[call.vm.Linker().Field(*field).slot].bits));
        return VmValue::Void();
    });
    result.push_back(std::move(handler).Build());

    auto runnable =
        IntrinsicClassBuilder::Class("Ltest/RecordingRunnable;", "Ljava/lang/Object;", {"Ljava/lang/Runnable;"});
    runnable.VirtualMethod("run", "()V", [runnable_calls](IntrinsicContext&) {
        ++*runnable_calls;
        return VmValue::Void();
    });
    result.push_back(std::move(runnable).Build());

    auto idle = IntrinsicClassBuilder::Class("Ltest/IdleCallback;", "Ljava/lang/Object;",
                                             {"Landroid/os/MessageQueue$IdleHandler;"});
    idle.VirtualMethod("queueIdle", "()Z", [idle_calls](IntrinsicContext&) {
        ++*idle_calls;
        return VmValue::Int(0);
    });
    result.push_back(std::move(idle).Build());

    auto failing_runnable =
        IntrinsicClassBuilder::Class("Ltest/FailingRunnable;", "Ljava/lang/Object;", {"Ljava/lang/Runnable;"});
    failing_runnable.VirtualMethod("run", "()V", [](IntrinsicContext& call) -> VmValue {
        const auto cause = call.vm.MakeThrowable("Ljava/lang/IllegalStateException;", "native formatter failed");
        const auto roots = call.vm.ProtectReferences(std::array{cause});
        const auto wrapper = call.vm.NewIntrinsicInstance("Ljava/lang/ExceptionInInitializerError;");
        const auto wrapper_root = call.vm.ProtectReferences(std::array{wrapper});
        const auto constructor = call.vm.Linker().FindDirectMethod(call.vm.Model().ObjectClass(wrapper), "<init>",
                                                                   "(Ljava/lang/Throwable;)V");
        REQUIRE(constructor.has_value());
        const auto initialized = call.vm.Call(*constructor, std::array{VmValue::Ref(wrapper), VmValue::Ref(cause)});
        REQUIRE_FALSE(initialized.exception.IsValid());
        throw VmJavaThrow{"Ljava/lang/ExceptionInInitializerError;", "", wrapper};
    });
    result.push_back(std::move(failing_runnable).Build());

    auto timer = IntrinsicClassBuilder::Class("Ltest/RecordingTimerTask;", "Ljava/util/TimerTask;");
    timer.OverrideMethod("run", "()V", [timer_calls](IntrinsicContext&) {
        ++*timer_calls;
        return VmValue::Void();
    });
    result.push_back(std::move(timer).Build());

    auto countdown = IntrinsicClassBuilder::Class("Ltest/RecordingCountDown;", "Landroid/os/CountDownTimer;");
    countdown.OverrideMethod("onTick", "(J)V", [ticks](IntrinsicContext& call) {
        ticks->push_back(call.arguments[0].AsLong());
        return VmValue::Void();
    });
    countdown.OverrideMethod("onFinish", "()V", [finishes](IntrinsicContext&) {
        ++*finishes;
        return VmValue::Void();
    });
    result.push_back(std::move(countdown).Build());

    auto receiver = IntrinsicClassBuilder::Class("Ltest/RecordingResultReceiver;", "Landroid/os/ResultReceiver;");
    receiver.OverrideMethod(
        "onReceiveResult", "(ILandroid/os/Bundle;)V",
        [receiver_results](IntrinsicContext& call) {
            receiver_results->emplace_back(call.arguments[0].AsInt(), call.arguments[1].ref);
            return VmValue::Void();
        },
        0x0004U);
    result.push_back(std::move(receiver).Build());

    auto async = IntrinsicClassBuilder::Class("Ltest/RecordingAsyncTask;", "Landroid/os/AsyncTask;");
    async.OverrideMethod("doInBackground", "([Ljava/lang/Object;)Ljava/lang/Object;",
                         [async_background](IntrinsicContext&) {
                             ++*async_background;
                             return VmValue::Ref(VmObjectRef{});
                         });
    async.OverrideMethod("onPostExecute", "(Ljava/lang/Object;)V", [async_post](IntrinsicContext&) {
        ++*async_post;
        return VmValue::Void();
    });
    result.push_back(std::move(async).Build());
    auto failing_async = IntrinsicClassBuilder::Class("Ltest/FailingAsyncTask;", "Landroid/os/AsyncTask;");
    failing_async.OverrideMethod(
        "doInBackground", "([Ljava/lang/Object;)Ljava/lang/Object;", [](IntrinsicContext&) -> VmValue {
            throw DexVmError(DexVmErrorReason::unresolved_reference, "missing HTTP capability");
        });
    failing_async.OverrideMethod("onPostExecute", "(Ljava/lang/Object;)V", [async_post](IntrinsicContext&) {
        ++*async_post;
        return VmValue::Void();
    });
    result.push_back(std::move(failing_async).Build());
    return result;
}

struct SchedulerVm final {
    ogplay::runtime::HostCopyJavaPayloadProvider strings_payload;
    JniStringStore strings{strings_payload};
    ogplay::runtime::HostCopyJavaPayloadProvider arrays_payload;
    JniPrimitiveArrayStore arrays{arrays_payload};
    JavaObjectModel model{strings, arrays};
    DexClassLinker linker;
    ogplay::core::CapabilityLedger ledger;
    std::shared_ptr<DexVmAndroidContext> context{std::make_shared<DexVmAndroidContext>()};
    std::vector<std::int32_t> messages;
    std::vector<std::int64_t> ticks;
    std::vector<std::pair<std::int32_t, VmObjectRef>> receiver_results;
    std::atomic<std::int32_t> runnable_calls{};
    std::atomic<std::int32_t> timer_calls{};
    std::atomic<std::int32_t> finishes{};
    std::atomic<std::int32_t> async_background{};
    std::atomic<std::int32_t> async_post{};
    std::atomic<std::int32_t> idle_calls{};
    Interpreter vm;
    VmThreadRuntime threads;

    explicit SchedulerVm(const InterpreterBackend backend = InterpreterBackend::switch_dispatch)
        : vm(
              [this]() -> DexClassLinker& {
                  linker.RegisterIntrinsics(CoreIntrinsicCatalog(AndroidCoreIntrinsicServices(context)));
                  linker.RegisterIntrinsics(AndroidIntrinsicCatalog(context));
                  linker.RegisterIntrinsics(TestCatalog(&messages, &ticks, &receiver_results, &runnable_calls,
                                                        &timer_calls, &finishes, &async_background, &async_post,
                                                        &idle_calls));
                  ogplay::test::RegisterBootDex(linker);
                  std::ifstream stream(std::string(OGPLAY_DEXVM_FIXTURE_DIR) + "/rom_java_replacement.dex",
                                       std::ios::binary);
                  linker.RegisterDex(std::vector<std::uint8_t>(std::istreambuf_iterator<char>(stream), {}));
                  linker.Link();
                  return linker;
              }(),
              model, nullptr, ledger, {.backend = backend}),
          threads(vm) {
        context->threads = &threads;
        vm.Monitors().SetTimeSource([state = context] { return state->uptime_millis.load(); });
        RegisterAndroidSchedulerStateTable(vm, context);
        PrepareAndroidMainLooper(vm, context);
    }

    ~SchedulerVm() {
        ShutdownAndroidScheduler(*context);
        threads.Shutdown();
    }

    DexClassId Class(const char* descriptor) {
        const auto java_class = linker.FindClass(descriptor);
        REQUIRE_MESSAGE(java_class.has_value(), descriptor);
        return *java_class;
    }

    VmCallOutcome Direct(const char* owner, const char* name, const char* descriptor,
                         std::vector<VmValue> arguments = {}) {
        const auto method = linker.FindDirectMethod(Class(owner), name, descriptor);
        REQUIRE_MESSAGE(method.has_value(), owner, ".", name, descriptor);
        return vm.Call(*method, arguments);
    }

    VmCallOutcome Virtual(const VmObjectRef receiver, const char* name, const char* descriptor,
                          std::vector<VmValue> arguments = {}) {
        const auto owner = model.ObjectClass(receiver);
        const auto index = linker.FindVtableIndex(owner, name, descriptor);
        REQUIRE_MESSAGE(index.has_value(), name, descriptor);
        arguments.insert(arguments.begin(), VmValue::Ref(receiver));
        return vm.Call(linker.Class(owner).vtable[*index], arguments);
    }

    VmObjectRef New(const char* descriptor) { return vm.NewIntrinsicInstance(descriptor); }

    void ConstructAs(const VmObjectRef object, const char* owner, const char* descriptor,
                     std::vector<VmValue> arguments = {}) {
        arguments.insert(arguments.begin(), VmValue::Ref(object));
        RequireOk(Direct(owner, "<init>", descriptor, arguments));
    }

    static void RequireOk(const VmCallOutcome& outcome) {
        REQUIRE_MESSAGE(!outcome.exception.IsValid(), outcome.exception_message);
    }
};

VmObjectRef QueueHead(Interpreter& vm, VmObjectRef looper) {
    const auto read = [&](VmObjectRef object, const char* name, const char* type) {
        const auto field = vm.Linker().FindFieldRecursive(vm.Model().ObjectClass(object), name, type);
        REQUIRE(field.has_value());
        return VmObjectRef{vm.Model().InstanceSlots(object)[vm.Linker().Field(*field).slot].bits};
    };
    return read(read(looper, "mQueue", "Landroid/os/MessageQueue;"), "mMessages", "Landroid/os/Message;");
}

std::size_t QueuedMessages(Interpreter& vm, VmObjectRef looper) {
    std::size_t count{};
    for (auto message = QueueHead(vm, looper); message.IsValid();) {
        REQUIRE(++count <= 10'000U);
        const auto field =
            vm.Linker().FindFieldRecursive(vm.Model().ObjectClass(message), "next", "Landroid/os/Message;");
        message = VmObjectRef{vm.Model().InstanceSlots(message)[vm.Linker().Field(*field).slot].bits};
    }
    return count;
}

template <typename Predicate>
bool WaitFor(Predicate predicate) {
    for (int attempt = 0; attempt < 2000; ++attempt) {
        if (predicate()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
}

}  // namespace

TEST_CASE("Context exposes the stable scheduler main Looper through wrappers") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ogplay::test::android_value::AndroidValueVm fixture(backend);
        const auto base = fixture.New("Landroid/content/Context;");
        const auto root = fixture.vm.ProtectReferences(std::array{base});
        const auto main = fixture.Static("Landroid/os/Looper;", "getMainLooper", "()Landroid/os/Looper;").ref;
        CHECK(main.IsValid());
        CHECK(fixture.On(base, "getMainLooper", "()Landroid/os/Looper;").ref == main);
        const auto wrapper =
            fixture.New("Landroid/content/ContextWrapper;", "(Landroid/content/Context;)V", {VmValue::Ref(base)});
        const auto wrapper_root = fixture.vm.ProtectReferences(std::array{wrapper});
        CHECK(fixture.On(wrapper, "getMainLooper", "()Landroid/os/Looper;").ref == main);
        const auto handler = fixture.New("Landroid/os/Handler;", "(Landroid/os/Looper;)V", {VmValue::Ref(main)});
        CHECK(fixture.On(handler, "getLooper", "()Landroid/os/Looper;").ref == main);
    }
}

TEST_CASE("empty ROM main queue keeps its Looper and Thread identities") {
    SchedulerVm fixture;
    const auto main = fixture.Direct("Landroid/os/Looper;", "getMainLooper", "()Landroid/os/Looper;");
    SchedulerVm::RequireOk(main);
    const auto thread = fixture.Virtual(main.value.ref, "getThread", "()Ljava/lang/Thread;").value.ref;
    REQUIRE(thread.IsValid());
    CHECK_FALSE(ogplay::test::android_value::DispatchFixtureMessages(fixture.vm, *fixture.context).has_value());
    CHECK(fixture.Virtual(main.value.ref, "getThread", "()Ljava/lang/Thread;").value.ref == thread);
    CHECK(fixture.context->main_looper == main.value.ref);
}

TEST_CASE("main Looper dispatch keeps dequeued Runnables alive while another thread collects") {
    SchedulerVm fixture;
    // Production session roots: queued work is a root only while it stays queued.
    fixture.vm.SetGcIntegration(
        {{}, {}, [&fixture](const VmRootVisitor& visit) { VisitAndroidSessionRoots(*fixture.context, visit); }});
    const auto main = fixture.Direct("Landroid/os/Looper;", "getMainLooper", "()Landroid/os/Looper;");
    SchedulerVm::RequireOk(main);
    const auto handler = fixture.New("Ltest/RecordingHandler;");
    const auto handler_root = fixture.vm.ProtectReferences(std::array{handler});
    fixture.ConstructAs(handler, "Landroid/os/Handler;", "(Landroid/os/Looper;)V", {VmValue::Ref(main.value.ref)});
    constexpr int kRunnables = 2000;
    for (int index = 0; index < kRunnables; ++index) {
        // After post returns, only the queued work references the Runnable.
        const auto runnable = fixture.New("Ltest/RecordingRunnable;");
        SchedulerVm::RequireOk(fixture.Virtual(handler, "post", "(Ljava/lang/Runnable;)Z", {VmValue::Ref(runnable)}));
    }
    static_cast<void>(fixture.vm.CollectGarbage("scheduler-queued"));
    REQUIRE(QueuedMessages(fixture.vm, fixture.context->main_looper) == static_cast<std::size_t>(kRunnables));

    // The lifecycle thread pumps the main Looper without holding the VM lock,
    // while a Java thread collects.
    std::atomic<bool> stop{};
    std::atomic<std::uint64_t> collections{};
    std::thread collector([&] {
        while (!stop.load()) {
            static_cast<void>(fixture.vm.CollectGarbage("scheduler-dispatch-race"));
            ++collections;
        }
    });
    std::optional<std::string> failure;
    try {
        failure = ogplay::test::android_value::DispatchFixtureMessages(fixture.vm, *fixture.context);
    } catch (const std::exception& error) {
        failure = error.what();
    }
    stop = true;
    collector.join();
    INFO(failure.value_or(""));
    CHECK_FALSE(failure.has_value());
    CHECK(fixture.runnable_calls.load() == kRunnables);
    CHECK(collections.load() > 0U);
}

TEST_CASE("DVM-89 ResultReceiver dispatches locally and through its Handler") {
    SchedulerVm fixture;
    const auto bundle = fixture.New("Landroid/os/Bundle;");
    fixture.ConstructAs(bundle, "Landroid/os/Bundle;", "()V");

    const auto direct = fixture.New("Ltest/RecordingResultReceiver;");
    fixture.ConstructAs(direct, "Landroid/os/ResultReceiver;", "(Landroid/os/Handler;)V",
                        {VmValue::Ref(VmObjectRef{})});
    SchedulerVm::RequireOk(
        fixture.Virtual(direct, "send", "(ILandroid/os/Bundle;)V", {VmValue::Int(7), VmValue::Ref(bundle)}));
    REQUIRE(fixture.receiver_results.size() == 1);
    CHECK(fixture.receiver_results[0] == std::pair{7, bundle});

    const auto handler = fixture.New("Landroid/os/Handler;");
    fixture.ConstructAs(handler, "Landroid/os/Handler;", "()V");
    const auto asynchronous = fixture.New("Ltest/RecordingResultReceiver;");
    fixture.ConstructAs(asynchronous, "Landroid/os/ResultReceiver;", "(Landroid/os/Handler;)V",
                        {VmValue::Ref(handler)});
    SchedulerVm::RequireOk(
        fixture.Virtual(asynchronous, "send", "(ILandroid/os/Bundle;)V", {VmValue::Int(9), VmValue::Ref(bundle)}));
    CHECK(fixture.receiver_results.size() == 1);
    CHECK_FALSE(ogplay::test::android_value::DispatchFixtureMessages(fixture.vm, *fixture.context).has_value());
    REQUIRE(fixture.receiver_results.size() == 2);
    CHECK(fixture.receiver_results[1] == std::pair{9, bundle});

    const auto parcel_outcome = fixture.Direct("Landroid/os/Parcel;", "obtain", "()Landroid/os/Parcel;");
    SchedulerVm::RequireOk(parcel_outcome);
    const auto parcel = parcel_outcome.value.ref;
    REQUIRE(parcel.IsValid());
    const auto transport_roots = fixture.vm.ProtectReferences(std::array{parcel, direct, bundle});
    SchedulerVm::RequireOk(
        fixture.Virtual(direct, "writeToParcel", "(Landroid/os/Parcel;I)V", {VmValue::Ref(parcel), VmValue::Int(0)}));
    SchedulerVm::RequireOk(fixture.Virtual(parcel, "setDataPosition", "(I)V", {VmValue::Int(0)}));
    const auto result_type = fixture.linker.ResolveDescriptor("Landroid/os/ResultReceiver;");
    SchedulerVm::RequireOk(fixture.vm.EnsureClassInitialized(result_type));
    const auto creator_field =
        fixture.linker.FindFieldRecursive(result_type, "CREATOR", "Landroid/os/Parcelable$Creator;");
    REQUIRE(creator_field.has_value());
    const auto creator =
        VmObjectRef(fixture.linker.Class(result_type).static_storage[fixture.linker.Field(*creator_field).slot]);
    REQUIRE(creator.IsValid());
    const auto transported =
        fixture.Virtual(creator, "createFromParcel", "(Landroid/os/Parcel;)Ljava/lang/Object;", {VmValue::Ref(parcel)})
            .value.ref;
    SchedulerVm::RequireOk(
        fixture.Virtual(transported, "send", "(ILandroid/os/Bundle;)V", {VmValue::Int(11), VmValue::Ref(bundle)}));
    REQUIRE(fixture.receiver_results.size() == 3);
    CHECK(fixture.receiver_results[2] == std::pair{11, bundle});
}

TEST_CASE("Message five-argument obtain preserves fields and queued ownership on both backends") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        SchedulerVm fixture(backend);
        fixture.vm.SetGcIntegration(
            {{}, {}, [&fixture](const VmRootVisitor& visit) { VisitAndroidSessionRoots(*fixture.context, visit); }});
        const auto handler = fixture.New("Ltest/RecordingHandler;");
        VmObjectRef first, second, payload;
        const auto slot = [&](VmObjectRef message, const char* name, const char* descriptor) {
            const auto field = fixture.linker.FindFieldRecursive(fixture.model.ObjectClass(message), name, descriptor);
            REQUIRE(field.has_value());
            return fixture.linker.Field(*field).slot;
        };
        const auto number = [&](VmObjectRef message, const char* name) {
            return static_cast<std::int32_t>(fixture.model.InstanceSlots(message)[slot(message, name, "I")].bits);
        };
        const auto reference = [&](VmObjectRef message, const char* name, const char* type) {
            return VmObjectRef(fixture.model.InstanceSlots(message)[slot(message, name, type)].bits);
        };
        {
            const auto handler_root = fixture.vm.ProtectReferences(std::array{handler});
            fixture.ConstructAs(handler, "Landroid/os/Handler;", "()V");
            payload = fixture.vm.NewStringUtf8("NULL");
            const auto payload_root = fixture.vm.ProtectReferences(std::array{payload});
            const auto obtained = fixture.Direct("Landroid/os/Message;", "obtain",
                                                 "(Landroid/os/Handler;IIILjava/lang/Object;)Landroid/os/Message;",
                                                 {VmValue::Ref(handler), VmValue::Int(12), VmValue::Int(10223772),
                                                  VmValue::Int(65537), VmValue::Ref(payload)});
            SchedulerVm::RequireOk(obtained);
            first = obtained.value.ref;
            const auto first_root = fixture.vm.ProtectReferences(std::array{first});
            const auto next = fixture.Direct("Landroid/os/Message;", "obtain",
                                             "(Landroid/os/Handler;IIILjava/lang/Object;)Landroid/os/Message;",
                                             {VmValue::Ref(handler), VmValue::Int(13), VmValue::Int(-9),
                                              VmValue::Int(4), VmValue::Ref(VmObjectRef{})});
            SchedulerVm::RequireOk(next);
            second = next.value.ref;
            const auto second_root = fixture.vm.ProtectReferences(std::array{second});
            CHECK(first != second);
            CHECK(number(first, "what") == 12);
            CHECK(number(first, "arg1") == 10223772);
            CHECK(number(first, "arg2") == 65537);
            CHECK(reference(first, "obj", "Ljava/lang/Object;") == payload);
            CHECK(reference(first, "target", "Landroid/os/Handler;") == handler);
            const auto legacy = fixture.Direct("Landroid/os/Message;", "obtain",
                                               "(Landroid/os/Handler;ILjava/lang/Object;)Landroid/os/Message;",
                                               {VmValue::Ref(handler), VmValue::Int(99), VmValue::Ref(payload)});
            SchedulerVm::RequireOk(legacy);
            CHECK(number(legacy.value.ref, "arg1") == 0);
            CHECK(number(legacy.value.ref, "arg2") == 0);
            SchedulerVm::RequireOk(fixture.Virtual(first, "sendToTarget", "()V"));
            SchedulerVm::RequireOk(fixture.Virtual(second, "sendToTarget", "()V"));
            CHECK(fixture.messages.empty());  // Delivery remains asynchronous.
        }
        static_cast<void>(fixture.vm.CollectGarbage("queued-five-argument-message"));
        CHECK(number(first, "arg1") == 10223772);
        CHECK(number(first, "arg2") == 65537);
        CHECK(number(second, "arg1") == -9);
        CHECK(number(second, "arg2") == 4);
        CHECK(fixture.vm.StringUtf8(reference(first, "obj", "Ljava/lang/Object;")) == "NULL");
        CHECK_FALSE(reference(second, "obj", "Ljava/lang/Object;").IsValid());
        CHECK(reference(first, "target", "Landroid/os/Handler;") == handler);
        CHECK_FALSE(ogplay::test::android_value::DispatchFixtureMessages(fixture.vm, *fixture.context).has_value());
        CHECK(fixture.messages == std::vector<std::int32_t>{12, 13});
        const auto untargeted = fixture.Direct("Landroid/os/Message;", "obtain",
                                               "(Landroid/os/Handler;IIILjava/lang/Object;)Landroid/os/Message;",
                                               {VmValue::Ref(VmObjectRef{}), VmValue::Int(7), VmValue::Int(-1),
                                                VmValue::Int(2), VmValue::Ref(VmObjectRef{})});
        SchedulerVm::RequireOk(untargeted);
        const auto invalid = fixture.Virtual(untargeted.value.ref, "sendToTarget", "()V");
        REQUIRE(invalid.exception.IsValid());
        CHECK(fixture.linker.Class(invalid.exception_class).descriptor == "Ljava/lang/NullPointerException;");
    }
}

TEST_CASE("DVM-85 Handler queue is delayed ordered and removable") {
    SchedulerVm fixture;
    CHECK(fixture.Direct("Landroid/os/SystemClock;", "uptimeMillis", "()J").value.AsLong() ==
          kDefaultAndroidDeviceUptimeMillis);
    CHECK(fixture.Direct("Landroid/os/SystemClock;", "elapsedRealtime", "()J").value.AsLong() ==
          kDefaultAndroidDeviceUptimeMillis);
    const auto handler = fixture.New("Ltest/RecordingHandler;");
    fixture.ConstructAs(handler, "Landroid/os/Handler;", "()V");
    const auto runnable = fixture.New("Ltest/RecordingRunnable;");

    const auto first =
        fixture.Virtual(handler, "obtainMessage", "(I)Landroid/os/Message;", {VmValue::Int(2)}).value.ref;
    const auto second =
        fixture.Virtual(handler, "obtainMessage", "(I)Landroid/os/Message;", {VmValue::Int(1)}).value.ref;
    SchedulerVm::RequireOk(fixture.Virtual(handler, "sendMessageDelayed", "(Landroid/os/Message;J)Z",
                                           {VmValue::Ref(first), VmValue::Long(10)}));
    SchedulerVm::RequireOk(fixture.Virtual(handler, "sendMessageDelayed", "(Landroid/os/Message;J)Z",
                                           {VmValue::Ref(second), VmValue::Long(10)}));
    SchedulerVm::RequireOk(fixture.Virtual(handler, "postDelayed", "(Ljava/lang/Runnable;J)Z",
                                           {VmValue::Ref(runnable), VmValue::Long(5)}));
    SchedulerVm::RequireOk(
        fixture.Virtual(handler, "removeCallbacks", "(Ljava/lang/Runnable;)V", {VmValue::Ref(runnable)}));
    SchedulerVm::RequireOk(
        fixture.Virtual(handler, "sendEmptyMessageDelayed", "(IJ)Z", {VmValue::Int(3), VmValue::Long(20)}));
    SchedulerVm::RequireOk(fixture.Virtual(handler, "removeMessages", "(I)V", {VmValue::Int(3)}));

    AdvanceAndroidClock(*fixture.context, 9);
    CHECK_FALSE(ogplay::test::android_value::DispatchFixtureMessages(fixture.vm, *fixture.context).has_value());
    CHECK(fixture.messages.empty());
    CHECK(fixture.runnable_calls.load() == 0);

    AdvanceAndroidClock(*fixture.context, 1);
    CHECK_FALSE(ogplay::test::android_value::DispatchFixtureMessages(fixture.vm, *fixture.context).has_value());
    CHECK(fixture.messages == std::vector<std::int32_t>{2, 1});
    CHECK(fixture.Direct("Landroid/os/SystemClock;", "uptimeMillis", "()J").value.AsLong() ==
          kDefaultAndroidDeviceUptimeMillis + 10);
}

TEST_CASE("DVM-170 Activity runOnUiThread posts workers to the main Looper") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        SchedulerVm fixture(backend);
        const auto activity = fixture.New("Landroid/app/Activity;");
        fixture.ConstructAs(activity, "Landroid/app/Activity;", "()V");
        const auto field = fixture.linker.FindFieldRecursive(fixture.vm.Model().ObjectClass(activity), "mUiThread",
                                                             "Ljava/lang/Thread;");
        REQUIRE(field);
        const auto root_thread = fixture.Direct("Ljava/lang/Thread;", "currentThread", "()Ljava/lang/Thread;");
        SchedulerVm::RequireOk(root_thread);
        fixture.vm.Model().InstanceSlots(activity)[fixture.linker.Field(*field).slot] = {root_thread.value.ref.Value(),
                                                                                         SlotTag::ref};
        const auto runnable = fixture.New("Ltest/RecordingRunnable;");
        const auto activity_type = fixture.Class("Landroid/app/Activity;");
        const auto slot = fixture.linker.FindVtableIndex(activity_type, "runOnUiThread", "(Ljava/lang/Runnable;)V");
        REQUIRE(slot.has_value());
        const auto method = fixture.linker.Class(activity_type).vtable[*slot];

        const auto worker = fixture.vm.CreateExecutionContext();
        fixture.threads.RegisterNativeContext(worker, false, "fixture input worker");
        const std::array arguments{VmValue::Ref(activity), VmValue::Ref(runnable)};
        SchedulerVm::RequireOk(fixture.vm.Call(worker, method, arguments));
        CHECK(fixture.runnable_calls.load() == 0);
        CHECK_FALSE(ogplay::test::android_value::DispatchFixtureMessages(fixture.vm, *fixture.context).has_value());
        CHECK(fixture.runnable_calls.load() == 1);
        fixture.threads.DetachNativeContext(worker.Token());
        fixture.vm.DiscardExecutionContext(worker);

        SchedulerVm::RequireOk(fixture.vm.Call(method, arguments));
        CHECK(fixture.runnable_calls.load() == 2);
    }
}

TEST_CASE("DVM-85 Timer and CountDownTimer share the Android clock") {
    SchedulerVm fixture;
    const auto timer = fixture.New("Ljava/util/Timer;");
    fixture.ConstructAs(timer, "Ljava/util/Timer;", "()V");
    const auto task = fixture.New("Ltest/RecordingTimerTask;");
    fixture.ConstructAs(task, "Ljava/util/TimerTask;", "()V");
    SchedulerVm::RequireOk(
        fixture.Virtual(timer, "schedule", "(Ljava/util/TimerTask;J)V", {VmValue::Ref(task), VmValue::Long(10)}));
    const auto cancelled_task = fixture.New("Ltest/RecordingTimerTask;");
    fixture.ConstructAs(cancelled_task, "Ljava/util/TimerTask;", "()V");
    SchedulerVm::RequireOk(fixture.Virtual(timer, "schedule", "(Ljava/util/TimerTask;J)V",
                                           {VmValue::Ref(cancelled_task), VmValue::Long(20)}));
    CHECK(fixture.Virtual(cancelled_task, "cancel", "()Z").value.AsInt() == 1);
    const auto invalid_task = fixture.New("Ltest/RecordingTimerTask;");
    fixture.ConstructAs(invalid_task, "Ljava/util/TimerTask;", "()V");
    const auto invalid = fixture.Virtual(timer, "schedule", "(Ljava/util/TimerTask;JJ)V",
                                         {VmValue::Ref(invalid_task), VmValue::Long(0), VmValue::Long(0)});
    REQUIRE(invalid.exception.IsValid());
    CHECK(fixture.linker.Class(invalid.exception_class).descriptor == "Ljava/lang/IllegalArgumentException;");

    CHECK_FALSE(ogplay::test::android_value::DispatchFixtureMessages(fixture.vm, *fixture.context).has_value());
    const auto countdown = fixture.New("Ltest/RecordingCountDown;");
    fixture.ConstructAs(countdown, "Landroid/os/CountDownTimer;", "(JJ)V", {VmValue::Long(30), VmValue::Long(10)});
    SchedulerVm::RequireOk(fixture.Virtual(countdown, "start", "()Landroid/os/CountDownTimer;"));
    CHECK_FALSE(ogplay::test::android_value::DispatchFixtureMessages(fixture.vm, *fixture.context).has_value());
    CHECK(fixture.ticks == std::vector<std::int64_t>{30});

    for (int step = 0; step < 3; ++step) {
        AdvanceAndroidClock(*fixture.context, 10);
        CHECK_FALSE(ogplay::test::android_value::DispatchFixtureMessages(fixture.vm, *fixture.context).has_value());
    }
    REQUIRE(WaitFor([&] { return fixture.timer_calls.load() == 1; }));
    SchedulerVm::RequireOk(fixture.Virtual(timer, "cancel", "()V"));
    CHECK(fixture.ticks == std::vector<std::int64_t>{30, 20, 10});
    CHECK(fixture.finishes.load() == 1);
}

TEST_CASE("scheduled callback diagnostics include the throwable cause chain") {
    SchedulerVm fixture;
    const auto handler = fixture.New("Landroid/os/Handler;");
    fixture.ConstructAs(handler, "Landroid/os/Handler;", "()V");
    const auto runnable = fixture.New("Ltest/FailingRunnable;");
    SchedulerVm::RequireOk(fixture.Virtual(handler, "post", "(Ljava/lang/Runnable;)Z", {VmValue::Ref(runnable)}));

    const auto failure = ogplay::test::android_value::DispatchFixtureMessages(fixture.vm, *fixture.context);
    REQUIRE(failure.has_value());
    INFO(*failure);
    CHECK(failure->find("scheduled callback raised: "
                        "Ljava/lang/ExceptionInInitializerError;") != std::string::npos);
    CHECK(failure->find("Caused by: Ljava/lang/IllegalStateException;: "
                        "native formatter failed") != std::string::npos);
}

TEST_CASE("DVM-85 HandlerThread owns a real child Looper") {
    SchedulerVm fixture;
    std::atomic_uint64_t prepared_token{};
    fixture.context->prepare_native_looper = [&](std::uint64_t token) { prepared_token = token; };
    const auto thread = fixture.New("Landroid/os/HandlerThread;");
    fixture.ConstructAs(thread, "Landroid/os/HandlerThread;", "(Ljava/lang/String;)V",
                        {VmValue::Ref(fixture.vm.NewStringUtf8("worker"))});
    SchedulerVm::RequireOk(fixture.Virtual(thread, "start", "()V"));
    const auto looper = fixture.Virtual(thread, "getLooper", "()Landroid/os/Looper;").value.ref;
    REQUIRE(looper.IsValid());

    const auto handler = fixture.New("Landroid/os/Handler;");
    fixture.ConstructAs(handler, "Landroid/os/Handler;", "(Landroid/os/Looper;)V", {VmValue::Ref(looper)});
    const auto runnable = fixture.New("Ltest/RecordingRunnable;");
    SchedulerVm::RequireOk(fixture.Virtual(handler, "post", "(Ljava/lang/Runnable;)Z", {VmValue::Ref(runnable)}));
    REQUIRE(WaitFor([&] { return fixture.runnable_calls.load() == 1; }));
    CHECK(fixture.messages.empty());
    SchedulerVm::RequireOk(fixture.Virtual(thread, "quit", "()Z"));
    SchedulerVm::RequireOk(fixture.Virtual(thread, "join", "()V"));
    CHECK_FALSE(fixture.threads.IsAlive(thread));
    CHECK(prepared_token.load() > 1);
}

TEST_CASE("DVM-85 AsyncTask runs background work then posts to main") {
    SchedulerVm fixture;
    const auto task = fixture.New("Ltest/RecordingAsyncTask;");
    fixture.ConstructAs(task, "Landroid/os/AsyncTask;", "()V");
    const auto params = fixture.model.NewObjectArray(fixture.linker.ResolveDescriptor("[Ljava/lang/Object;"),
                                                     fixture.Class("Ljava/lang/Object;"), 0);
    SchedulerVm::RequireOk(
        fixture.Virtual(task, "execute", "([Ljava/lang/Object;)Landroid/os/AsyncTask;", {VmValue::Ref(params)}));
    REQUIRE(WaitFor([&] { return fixture.async_background.load() == 1; }));
    CHECK(fixture.async_post.load() == 0);
    CHECK_FALSE(ogplay::test::android_value::DispatchFixtureMessages(fixture.vm, *fixture.context).has_value());
    CHECK(fixture.async_post.load() == 1);
    const auto status = fixture.Virtual(task, "getStatus", "()Landroid/os/AsyncTask$Status;").value.ref;
    REQUIRE(status.IsValid());
    const auto name = fixture.Virtual(status, "name", "()Ljava/lang/String;").value.ref;
    CHECK(fixture.vm.StringUtf8(name) == "FINISHED");
}

TEST_CASE("AsyncTask preserves background DexVmError and never posts null") {
    SchedulerVm fixture;
    const auto task = fixture.New("Ltest/FailingAsyncTask;");
    fixture.ConstructAs(task, "Landroid/os/AsyncTask;", "()V");
    const auto params = fixture.model.NewObjectArray(fixture.linker.ResolveDescriptor("[Ljava/lang/Object;"),
                                                     fixture.Class("Ljava/lang/Object;"), 0);
    SchedulerVm::RequireOk(
        fixture.Virtual(task, "execute", "([Ljava/lang/Object;)Landroid/os/AsyncTask;", {VmValue::Ref(params)}));
    std::optional<std::string> failure;
    REQUIRE(WaitFor([&] {
        failure = ogplay::test::android_value::DispatchFixtureMessages(fixture.vm, *fixture.context);
        return failure.has_value();
    }));
    CHECK(failure->find("missing HTTP capability") != std::string::npos);
    CHECK(fixture.async_post.load() == 0);
    CHECK(fixture.ledger.Unimplemented().empty());
}

TEST_CASE("DVM-262 Message obtain overload calls the ROM no-argument pool factory") {
    using namespace ogplay::test::android_value;
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        AndroidValueVm f(backend, {}, false, nullptr);
        const auto type = f.linker.ResolveDescriptor("Landroid/os/Message;");
        const auto initialized = f.vm.EnsureClassInitialized(type);
        REQUIRE_FALSE(initialized.exception.IsValid());
        const auto pooled = f.New("Landroid/os/Message;");
        const auto handler = f.vm.NewIntrinsicInstance("Landroid/os/Handler;");
        const auto roots = f.vm.ProtectReferences(std::array{pooled, handler});
        const auto set_pool = [&](const char* name, const char* descriptor, VmValue value) {
            const auto field = f.linker.FindFieldRecursive(type, name, descriptor);
            REQUIRE(field.has_value());
            f.linker.MutableClass(type).static_storage[f.linker.Field(*field).slot] =
                value.kind == VmValue::Kind::ref ? value.ref.Value() : value.cat1;
        };
        set_pool("sPool", "Landroid/os/Message;", VmValue::Ref(pooled));
        set_pool("sPoolSize", "I", VmValue::Int(1));
        const auto target = f.linker.FindDirectMethod(type, "obtain", "()Landroid/os/Message;");
        REQUIRE(target.has_value());
        CHECK(f.linker.Method(*target).kind == MethodKind::interpreted);
        const auto message = f.Static("Landroid/os/Message;", "obtain", "(Landroid/os/Handler;III)Landroid/os/Message;",
                                      {VmValue::Ref(handler), VmValue::Int(19), VmValue::Int(23), VmValue::Int(29)})
                                 .ref;
        CHECK(message == pooled);
        const auto bits = [&](const char* name, const char* descriptor) {
            const auto field = f.linker.FindFieldRecursive(type, name, descriptor);
            REQUIRE(field.has_value());
            return f.model.InstanceSlots(message)[f.linker.Field(*field).slot].bits;
        };
        CHECK(bits("what", "I") == 19);
        CHECK(bits("arg1", "I") == 23);
        CHECK(bits("arg2", "I") == 29);
        CHECK(bits("target", "Landroid/os/Handler;") == handler.Value());
        CHECK(bits("next", "Landroid/os/Message;") == 0);
        const auto fresh = f.Static("Landroid/os/Message;", "obtain", "(Landroid/os/Handler;)Landroid/os/Message;",
                                    {VmValue::Ref(handler)})
                               .ref;
        CHECK(fresh != pooled);
    }
}

TEST_CASE("DVM-268 DEX Timer schedules once and cancel rejects more work") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        SchedulerVm f(backend);
        const auto timer = f.New("Ljava/util/Timer;");
        f.ConstructAs(timer, "Ljava/util/Timer;", "()V");
        const auto task = f.New("Ltest/RecordingTimerTask;");
        const auto roots = f.vm.ProtectReferences(std::array{timer, task});
        f.ConstructAs(task, "Ljava/util/TimerTask;", "()V");
        SchedulerVm::RequireOk(f.Direct("Ltest/RomJavaReplacement;", "timer",
                                        "(Ljava/util/Timer;Ljava/util/TimerTask;)V",
                                        {VmValue::Ref(timer), VmValue::Ref(task)}));
        AdvanceAndroidClock(*f.context, 10);
        CHECK_FALSE(ogplay::test::android_value::DispatchFixtureMessages(f.vm, *f.context).has_value());
        REQUIRE(WaitFor([&] { return f.timer_calls.load() == 1; }));
        SchedulerVm::RequireOk(f.Virtual(timer, "cancel", "()V"));
        const auto other = f.New("Ltest/RecordingTimerTask;");
        const auto other_root = f.vm.ProtectReferences(std::array{other});
        f.ConstructAs(other, "Ljava/util/TimerTask;", "()V");
        const auto cancelled =
            f.Direct("Ltest/RomJavaReplacement;", "timer", "(Ljava/util/Timer;Ljava/util/TimerTask;)V",
                     {VmValue::Ref(timer), VmValue::Ref(other)});
        REQUIRE(cancelled.exception.IsValid());
        CHECK(f.linker.Class(cancelled.exception_class).descriptor == "Ljava/lang/IllegalStateException;");
        CHECK(f.linker.Class(f.Class("Ljava/util/Timer;")).is_boot_dex);
    }
}

TEST_CASE("DVM-268 DEX CountDownTimer ticks and finishes") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        SchedulerVm f(backend);
        CHECK_FALSE(ogplay::test::android_value::DispatchFixtureMessages(f.vm, *f.context).has_value());
        const auto timer = f.New("Ltest/RecordingCountDown;");
        const auto roots = f.vm.ProtectReferences(std::array{timer});
        f.ConstructAs(timer, "Landroid/os/CountDownTimer;", "(JJ)V", {VmValue::Long(30), VmValue::Long(10)});
        SchedulerVm::RequireOk(f.Direct("Ltest/RomJavaReplacement;", "countdown", "(Landroid/os/CountDownTimer;)V",
                                        {VmValue::Ref(timer)}));
        CHECK_FALSE(ogplay::test::android_value::DispatchFixtureMessages(f.vm, *f.context).has_value());
        CHECK(f.ticks == std::vector<std::int64_t>{30});
        for (int i = 0; i < 3; ++i) {
            AdvanceAndroidClock(*f.context, 10);
            CHECK_FALSE(ogplay::test::android_value::DispatchFixtureMessages(f.vm, *f.context).has_value());
        }
        CHECK(f.ticks == std::vector<std::int64_t>{30, 20, 10});
        CHECK(f.finishes.load() == 1);
        CHECK(f.linker.Class(f.Class("Landroid/os/CountDownTimer;")).is_boot_dex);
    }
}

TEST_CASE("DVM-268 DEX TimerTask cancellation before scheduling returns false") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        SchedulerVm f(backend);
        const auto task = f.New("Ltest/RecordingTimerTask;");
        const auto roots = f.vm.ProtectReferences(std::array{task});
        f.ConstructAs(task, "Ljava/util/TimerTask;", "()V");
        const auto outcome =
            f.Direct("Ltest/RomJavaReplacement;", "task", "(Ljava/util/TimerTask;)Z", {VmValue::Ref(task)});
        SchedulerVm::RequireOk(outcome);
        CHECK(outcome.value.AsInt() == 0);
        CHECK(f.linker.Class(f.Class("Ljava/util/TimerTask;")).is_boot_dex);
    }
}

TEST_CASE("ROM MessageQueue barriers pass asynchronous messages and protect the main Looper") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        SchedulerVm f(backend);
        const auto main = f.context->main_looper;
        const auto handler = f.New("Ltest/RecordingHandler;");
        f.ConstructAs(handler, "Landroid/os/Handler;", "()V");
        const auto roots = f.vm.ProtectReferences(std::array{handler});
        const auto barrier = f.Virtual(main, "postSyncBarrier", "()I");
        SchedulerVm::RequireOk(barrier);
        const auto message = [&](int what, bool asynchronous) {
            const auto obtained = f.Virtual(handler, "obtainMessage", "(I)Landroid/os/Message;", {VmValue::Int(what)});
            SchedulerVm::RequireOk(obtained);
            const auto ref = obtained.value.ref;
            const auto root = f.vm.ProtectReferences(std::array{ref});
            SchedulerVm::RequireOk(f.Virtual(ref, "setAsynchronous", "(Z)V", {VmValue::Int(asynchronous ? 1 : 0)}));
            SchedulerVm::RequireOk(f.Virtual(ref, "sendToTarget", "()V"));
        };
        message(1, false);
        message(2, true);
        CHECK_FALSE(ogplay::test::android_value::DispatchFixtureMessages(f.vm, *f.context).has_value());
        CHECK(f.messages == std::vector<std::int32_t>{2});
        SchedulerVm::RequireOk(f.Virtual(main, "removeSyncBarrier", "(I)V", {barrier.value}));
        CHECK_FALSE(ogplay::test::android_value::DispatchFixtureMessages(f.vm, *f.context).has_value());
        CHECK(f.messages == std::vector<std::int32_t>{2, 1});
        const auto duplicate = f.Virtual(main, "removeSyncBarrier", "(I)V", {barrier.value});
        REQUIRE(duplicate.exception.IsValid());
        CHECK(f.linker.Class(duplicate.exception_class).descriptor == "Ljava/lang/IllegalStateException;");
        for (const auto* method : {"quit", "quitSafely"}) {
            const auto rejected = f.Virtual(main, method, "()V");
            REQUIRE(rejected.exception.IsValid());
            CHECK(f.linker.Class(rejected.exception_class).descriptor == "Ljava/lang/RuntimeException;");
        }
    }
}

TEST_CASE("ROM HandlerThread delays wake only after the manual Clock reaches their deadline") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        SchedulerVm f(backend);
        const auto thread = f.New("Landroid/os/HandlerThread;");
        const auto thread_root = f.vm.ProtectReferences(std::array{thread});
        f.ConstructAs(thread, "Landroid/os/HandlerThread;", "(Ljava/lang/String;)V",
                      {VmValue::Ref(f.vm.NewStringUtf8("clock-worker"))});
        SchedulerVm::RequireOk(f.Virtual(thread, "start", "()V"));
        const auto looper = f.Virtual(thread, "getLooper", "()Landroid/os/Looper;").value.ref;
        REQUIRE(looper.IsValid());
        const auto handler = f.New("Landroid/os/Handler;");
        const auto runnable = f.New("Ltest/RecordingRunnable;");
        const auto roots = f.vm.ProtectReferences(std::array{handler, runnable});
        f.ConstructAs(handler, "Landroid/os/Handler;", "(Landroid/os/Looper;)V", {VmValue::Ref(looper)});
        const auto now = f.context->uptime_millis.load();
        SchedulerVm::RequireOk(
            f.Virtual(handler, "postDelayed", "(Ljava/lang/Runnable;J)Z", {VmValue::Ref(runnable), VmValue::Long(10)}));
        REQUIRE(WaitFor([&] { return f.Virtual(looper, "isIdling", "()Z").value.AsInt() != 0; }));
        CHECK(f.runnable_calls.load() == 0);
        AdvanceAndroidClock(*f.context, 9);
        CHECK(f.context->uptime_millis.load() == now + 9);
        CHECK(f.runnable_calls.load() == 0);
        AdvanceAndroidClock(*f.context, 1);
        REQUIRE(WaitFor([&] { return f.runnable_calls.load() == 1; }));
        CHECK(f.context->uptime_millis.load() == now + 10);
        SchedulerVm::RequireOk(f.Virtual(thread, "quit", "()Z"));
        SchedulerVm::RequireOk(f.Virtual(thread, "join", "()V"));
    }
}

TEST_CASE("ROM MessageQueue invokes IdleHandler once and quitSafely discards future work") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        SchedulerVm f(backend);
        const auto thread = f.New("Landroid/os/HandlerThread;");
        const auto thread_root = f.vm.ProtectReferences(std::array{thread});
        f.ConstructAs(thread, "Landroid/os/HandlerThread;", "(Ljava/lang/String;)V",
                      {VmValue::Ref(f.vm.NewStringUtf8("idle-worker"))});
        SchedulerVm::RequireOk(f.Virtual(thread, "start", "()V"));
        const auto looper = f.Virtual(thread, "getLooper", "()Landroid/os/Looper;").value.ref;
        REQUIRE(looper.IsValid());
        const auto queue = f.Virtual(looper, "getQueue", "()Landroid/os/MessageQueue;").value.ref;
        const auto idle = f.New("Ltest/IdleCallback;");
        const auto handler = f.New("Landroid/os/Handler;");
        const auto runnable = f.New("Ltest/RecordingRunnable;");
        const auto roots = f.vm.ProtectReferences(std::array{idle, handler, runnable});
        f.ConstructAs(handler, "Landroid/os/Handler;", "(Landroid/os/Looper;)V", {VmValue::Ref(looper)});
        SchedulerVm::RequireOk(
            f.Virtual(queue, "addIdleHandler", "(Landroid/os/MessageQueue$IdleHandler;)V", {VmValue::Ref(idle)}));
        SchedulerVm::RequireOk(f.Virtual(handler, "post", "(Ljava/lang/Runnable;)Z", {VmValue::Ref(runnable)}));
        REQUIRE(WaitFor([&] { return f.idle_calls.load() == 1; }));
        SchedulerVm::RequireOk(f.Virtual(handler, "postDelayed", "(Ljava/lang/Runnable;J)Z",
                                         {VmValue::Ref(runnable), VmValue::Long(100)}));
        SchedulerVm::RequireOk(f.Virtual(handler, "post", "(Ljava/lang/Runnable;)Z", {VmValue::Ref(runnable)}));
        SchedulerVm::RequireOk(f.Virtual(thread, "quitSafely", "()Z"));
        SchedulerVm::RequireOk(f.Virtual(thread, "join", "()V"));
        CHECK(f.runnable_calls.load() == 2);
        CHECK(f.idle_calls.load() == 1);
        AdvanceAndroidClock(*f.context, 100);
        CHECK_FALSE(f.threads.IsAlive(thread));
        CHECK(f.runnable_calls.load() == 2);
    }
}

TEST_CASE("ROM idle HandlerThread wakes when the VM threads shut down") {
    ogplay::test::android_value::AndroidValueVm f;
    const auto thread = f.New("Landroid/os/HandlerThread;", "(Ljava/lang/String;)V",
                              {VmValue::Ref(f.vm.NewStringUtf8("idle-shutdown"))});
    const auto root = f.vm.ProtectReferences(std::array{thread});
    f.On(thread, "start", "()V");
    REQUIRE(f.On(thread, "getLooper", "()Landroid/os/Looper;").ref.IsValid());
    f.threads.Shutdown();
    CHECK_FALSE(f.threads.IsAlive(thread));
    CHECK(f.context->scheduler_shutdown);
}

TEST_CASE("ROM MessageQueue and NDK Looper consume the same native wake") {
    std::atomic_uint64_t token{};
    std::atomic_uint32_t returned{};
    ogplay::test::android_value::AndroidValueVm fixture(InterpreterBackend::switch_dispatch);
    const auto poll = fixture.context->poll_native_looper;
    fixture.context->poll_native_looper = [&](std::uint64_t current, const std::function<bool()>& ready) {
        token = current;
        poll(current, ready);
        ++returned;
    };
    const auto thread = fixture.New("Landroid/os/HandlerThread;", "(Ljava/lang/String;)V",
                                    {VmValue::Ref(fixture.vm.NewStringUtf8("native-wake"))});
    const auto root = fixture.vm.ProtectReferences(std::array{thread});
    fixture.On(thread, "start", "()V");
    REQUIRE(fixture.On(thread, "getLooper", "()Landroid/os/Looper;").ref.IsValid());
    REQUIRE(WaitFor([&] { return token.load() > 1; }));
    fixture.runtime->session->WakeThreadLooper(fixture.context->native_thread_for_context(token.load()));
    REQUIRE(WaitFor([&] { return returned.load() > 0; }));
    CHECK(fixture.On(thread, "quitSafely", "()Z").AsInt() == 1);
    fixture.On(thread, "join", "()V");
    fixture.context->poll_native_looper = poll;
}
