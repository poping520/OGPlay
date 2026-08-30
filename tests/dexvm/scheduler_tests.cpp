#include <doctest/doctest.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
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

std::vector<IntrinsicClassDecl> TestCatalog(
    std::vector<std::int32_t>* messages, std::vector<std::int64_t>* ticks,
    std::vector<std::pair<std::int32_t, VmObjectRef>>* receiver_results,
    std::atomic<std::int32_t>* runnable_calls,
    std::atomic<std::int32_t>* timer_calls,
    std::atomic<std::int32_t>* finishes,
    std::atomic<std::int32_t>* async_background,
    std::atomic<std::int32_t>* async_post) {
    std::vector<IntrinsicClassDecl> result;
    auto handler = IntrinsicClassBuilder::Class(
        "Ltest/RecordingHandler;", "Landroid/os/Handler;");
    handler.OverrideMethod("handleMessage", "(Landroid/os/Message;)V",
        [messages](IntrinsicContext& call) {
            messages->push_back(static_cast<std::int32_t>(
                call.vm.Model().InstanceSlots(call.arguments[0].ref)[0].bits));
            return VmValue::Void();
        });
    result.push_back(std::move(handler).Build());

    auto runnable = IntrinsicClassBuilder::Class(
        "Ltest/RecordingRunnable;", "Ljava/lang/Object;",
        {"Ljava/lang/Runnable;"});
    runnable.VirtualMethod("run", "()V",
        [runnable_calls](IntrinsicContext&) {
            ++*runnable_calls;
            return VmValue::Void();
        });
    result.push_back(std::move(runnable).Build());

    auto timer = IntrinsicClassBuilder::Class(
        "Ltest/RecordingTimerTask;", "Ljava/util/TimerTask;");
    timer.OverrideMethod("run", "()V", [timer_calls](IntrinsicContext&) {
        ++*timer_calls;
        return VmValue::Void();
    });
    result.push_back(std::move(timer).Build());

    auto countdown = IntrinsicClassBuilder::Class(
        "Ltest/RecordingCountDown;", "Landroid/os/CountDownTimer;");
    countdown.OverrideMethod("onTick", "(J)V",
        [ticks](IntrinsicContext& call) {
            ticks->push_back(call.arguments[0].AsLong());
            return VmValue::Void();
        });
    countdown.OverrideMethod("onFinish", "()V",
        [finishes](IntrinsicContext&) {
            ++*finishes;
            return VmValue::Void();
        });
    result.push_back(std::move(countdown).Build());

    auto receiver = IntrinsicClassBuilder::Class(
        "Ltest/RecordingResultReceiver;", "Landroid/os/ResultReceiver;");
    receiver.OverrideMethod("onReceiveResult", "(ILandroid/os/Bundle;)V",
        [receiver_results](IntrinsicContext& call) {
            receiver_results->emplace_back(
                call.arguments[0].AsInt(), call.arguments[1].ref);
            return VmValue::Void();
        }, 0x0004U);
    result.push_back(std::move(receiver).Build());

    auto async = IntrinsicClassBuilder::Class(
        "Ltest/RecordingAsyncTask;", "Landroid/os/AsyncTask;");
    async.OverrideMethod(
        "doInBackground", "([Ljava/lang/Object;)Ljava/lang/Object;",
        [async_background](IntrinsicContext&) {
            ++*async_background;
            return VmValue::Ref(VmObjectRef{});
        });
    async.OverrideMethod("onPostExecute", "(Ljava/lang/Object;)V",
        [async_post](IntrinsicContext&) {
            ++*async_post;
            return VmValue::Void();
        });
    result.push_back(std::move(async).Build());
    return result;
}

struct SchedulerVm final {
    JniStringStore strings;
    JniPrimitiveArrayStore arrays;
    JavaObjectModel model{strings, arrays};
    DexClassLinker linker;
    ogplay::core::CapabilityLedger ledger;
    std::shared_ptr<DexVmAndroidContext> context{
        std::make_shared<DexVmAndroidContext>()};
    std::vector<std::int32_t> messages;
    std::vector<std::int64_t> ticks;
    std::vector<std::pair<std::int32_t, VmObjectRef>> receiver_results;
    std::atomic<std::int32_t> runnable_calls{};
    std::atomic<std::int32_t> timer_calls{};
    std::atomic<std::int32_t> finishes{};
    std::atomic<std::int32_t> async_background{};
    std::atomic<std::int32_t> async_post{};
    Interpreter vm;
    VmThreadRuntime threads;

    SchedulerVm()
        : vm([this]() -> DexClassLinker& {
                 linker.RegisterIntrinsics(
                     CoreIntrinsicCatalog(AndroidCoreIntrinsicServices(context)));
                 linker.RegisterIntrinsics(AndroidIntrinsicCatalog(context));
                 linker.RegisterIntrinsics(TestCatalog(
                     &messages, &ticks, &receiver_results, &runnable_calls,
                     &timer_calls, &finishes, &async_background,
                     &async_post));
                 linker.Link();
                 return linker;
             }(), model, nullptr, ledger, {}),
          threads(vm) {
        context->threads = &threads;
        vm.Monitors().SetTimeSource(
            [state = context] { return state->uptime_millis.load(); });
        RegisterAndroidSchedulerStateTable(vm, context);
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

    VmCallOutcome Direct(const char* owner, const char* name,
                         const char* descriptor,
                         std::vector<VmValue> arguments = {}) {
        const auto method = linker.FindDirectMethod(
            Class(owner), name, descriptor);
        REQUIRE_MESSAGE(method.has_value(), owner, ".", name, descriptor);
        return vm.Call(*method, arguments);
    }

    VmCallOutcome Virtual(const VmObjectRef receiver, const char* name,
                          const char* descriptor,
                          std::vector<VmValue> arguments = {}) {
        const auto owner = model.ObjectClass(receiver);
        const auto index = linker.FindVtableIndex(owner, name, descriptor);
        REQUIRE_MESSAGE(index.has_value(), name, descriptor);
        arguments.insert(arguments.begin(), VmValue::Ref(receiver));
        return vm.Call(linker.Class(owner).vtable[*index], arguments);
    }

    VmObjectRef New(const char* descriptor) {
        return vm.NewIntrinsicInstance(descriptor);
    }

    void ConstructAs(const VmObjectRef object, const char* owner,
                     const char* descriptor,
                     std::vector<VmValue> arguments = {}) {
        arguments.insert(arguments.begin(), VmValue::Ref(object));
        RequireOk(Direct(owner, "<init>", descriptor, arguments));
    }

    static void RequireOk(const VmCallOutcome& outcome) {
        REQUIRE_MESSAGE(!outcome.exception.IsValid(),
                        outcome.exception_message);
    }
};

template <typename Predicate>
bool WaitFor(Predicate predicate) {
    for (int attempt = 0; attempt < 2000; ++attempt) {
        if (predicate()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
}

}  // namespace

TEST_CASE("DVM-89 ResultReceiver dispatches locally and through its Handler") {
    SchedulerVm fixture;
    const auto bundle = fixture.New("Landroid/os/Bundle;");
    fixture.ConstructAs(bundle, "Landroid/os/Bundle;", "()V");

    const auto direct = fixture.New("Ltest/RecordingResultReceiver;");
    fixture.ConstructAs(
        direct, "Landroid/os/ResultReceiver;",
        "(Landroid/os/Handler;)V", {VmValue::Ref(VmObjectRef{})});
    SchedulerVm::RequireOk(fixture.Virtual(
        direct, "send", "(ILandroid/os/Bundle;)V",
        {VmValue::Int(7), VmValue::Ref(bundle)}));
    REQUIRE(fixture.receiver_results.size() == 1);
    CHECK(fixture.receiver_results[0] == std::pair{7, bundle});

    const auto handler = fixture.New("Landroid/os/Handler;");
    fixture.ConstructAs(handler, "Landroid/os/Handler;", "()V");
    const auto asynchronous =
        fixture.New("Ltest/RecordingResultReceiver;");
    fixture.ConstructAs(
        asynchronous, "Landroid/os/ResultReceiver;",
        "(Landroid/os/Handler;)V", {VmValue::Ref(handler)});
    SchedulerVm::RequireOk(fixture.Virtual(
        asynchronous, "send", "(ILandroid/os/Bundle;)V",
        {VmValue::Int(9), VmValue::Ref(bundle)}));
    CHECK(fixture.receiver_results.size() == 1);
    CHECK_FALSE(PumpJavaThreads(fixture.vm, *fixture.context).has_value());
    REQUIRE(fixture.receiver_results.size() == 2);
    CHECK(fixture.receiver_results[1] == std::pair{9, bundle});

    const auto parcel = fixture.New("Landroid/os/Parcel;");
    fixture.ConstructAs(parcel, "Landroid/os/Parcel;", "()V");
    const auto unsupported = fixture.Virtual(
        direct, "writeToParcel", "(Landroid/os/Parcel;I)V",
        {VmValue::Ref(parcel), VmValue::Int(0)});
    REQUIRE(unsupported.exception.IsValid());
    CHECK(fixture.linker.Class(unsupported.exception_class).descriptor ==
          "Ljava/lang/UnsupportedOperationException;");
}

TEST_CASE("DVM-85 Handler queue is delayed ordered and removable") {
    SchedulerVm fixture;
    const auto handler = fixture.New("Ltest/RecordingHandler;");
    fixture.ConstructAs(handler, "Landroid/os/Handler;", "()V");
    const auto runnable = fixture.New("Ltest/RecordingRunnable;");

    const auto first = fixture.Virtual(
        handler, "obtainMessage", "(I)Landroid/os/Message;",
        {VmValue::Int(2)}).value.ref;
    const auto second = fixture.Virtual(
        handler, "obtainMessage", "(I)Landroid/os/Message;",
        {VmValue::Int(1)}).value.ref;
    SchedulerVm::RequireOk(fixture.Virtual(
        handler, "sendMessageDelayed", "(Landroid/os/Message;J)Z",
        {VmValue::Ref(first), VmValue::Long(10)}));
    SchedulerVm::RequireOk(fixture.Virtual(
        handler, "sendMessageDelayed", "(Landroid/os/Message;J)Z",
        {VmValue::Ref(second), VmValue::Long(10)}));
    SchedulerVm::RequireOk(fixture.Virtual(
        handler, "postDelayed", "(Ljava/lang/Runnable;J)Z",
        {VmValue::Ref(runnable), VmValue::Long(5)}));
    SchedulerVm::RequireOk(fixture.Virtual(
        handler, "removeCallbacks", "(Ljava/lang/Runnable;)V",
        {VmValue::Ref(runnable)}));
    SchedulerVm::RequireOk(fixture.Virtual(
        handler, "sendEmptyMessageDelayed", "(IJ)Z",
        {VmValue::Int(3), VmValue::Long(20)}));
    SchedulerVm::RequireOk(fixture.Virtual(
        handler, "removeMessages", "(I)V", {VmValue::Int(3)}));

    AdvanceAndroidClock(*fixture.context, 9);
    CHECK_FALSE(PumpJavaThreads(fixture.vm, *fixture.context).has_value());
    CHECK(fixture.messages.empty());
    CHECK(fixture.runnable_calls.load() == 0);

    AdvanceAndroidClock(*fixture.context, 1);
    CHECK_FALSE(PumpJavaThreads(fixture.vm, *fixture.context).has_value());
    CHECK(fixture.messages == std::vector<std::int32_t>{2, 1});
    CHECK(fixture.Direct("Landroid/os/SystemClock;", "uptimeMillis", "()J")
              .value.AsLong() == 10);
}

TEST_CASE("DVM-85 Timer and CountDownTimer share the Android clock") {
    SchedulerVm fixture;
    const auto timer = fixture.New("Ljava/util/Timer;");
    fixture.ConstructAs(timer, "Ljava/util/Timer;", "()V");
    const auto task = fixture.New("Ltest/RecordingTimerTask;");
    SchedulerVm::RequireOk(fixture.Virtual(
        timer, "schedule", "(Ljava/util/TimerTask;J)V",
        {VmValue::Ref(task), VmValue::Long(10)}));
    const auto cancelled_task = fixture.New("Ltest/RecordingTimerTask;");
    SchedulerVm::RequireOk(fixture.Virtual(
        timer, "schedule", "(Ljava/util/TimerTask;J)V",
        {VmValue::Ref(cancelled_task), VmValue::Long(20)}));
    CHECK(fixture.Virtual(cancelled_task, "cancel", "()Z").value.AsInt() == 1);
    const auto invalid_task = fixture.New("Ltest/RecordingTimerTask;");
    const auto invalid = fixture.Virtual(
        timer, "schedule", "(Ljava/util/TimerTask;JJ)V",
        {VmValue::Ref(invalid_task), VmValue::Long(0), VmValue::Long(0)});
    REQUIRE(invalid.exception.IsValid());
    CHECK(fixture.linker.Class(invalid.exception_class).descriptor ==
          "Ljava/lang/IllegalArgumentException;");

    const auto countdown = fixture.New("Ltest/RecordingCountDown;");
    fixture.ConstructAs(countdown, "Landroid/os/CountDownTimer;", "(JJ)V",
                        {VmValue::Long(30), VmValue::Long(10)});
    SchedulerVm::RequireOk(fixture.Virtual(
        countdown, "start", "()Landroid/os/CountDownTimer;"));
    CHECK_FALSE(PumpJavaThreads(fixture.vm, *fixture.context).has_value());
    CHECK(fixture.ticks == std::vector<std::int64_t>{30});

    for (int step = 0; step < 3; ++step) {
        AdvanceAndroidClock(*fixture.context, 10);
        CHECK_FALSE(PumpJavaThreads(fixture.vm, *fixture.context).has_value());
    }
    CHECK(fixture.timer_calls.load() == 1);
    CHECK(fixture.ticks == std::vector<std::int64_t>{30, 20, 10});
    CHECK(fixture.finishes.load() == 1);
}

TEST_CASE("DVM-85 HandlerThread owns a real child Looper") {
    SchedulerVm fixture;
    const auto thread = fixture.New("Landroid/os/HandlerThread;");
    fixture.ConstructAs(thread, "Landroid/os/HandlerThread;",
                        "(Ljava/lang/String;)V",
                        {VmValue::Ref(fixture.vm.NewStringUtf8("worker"))});
    SchedulerVm::RequireOk(fixture.Virtual(thread, "start", "()V"));
    const auto looper = fixture.Virtual(
        thread, "getLooper", "()Landroid/os/Looper;").value.ref;
    REQUIRE(looper.IsValid());

    const auto handler = fixture.New("Landroid/os/Handler;");
    fixture.ConstructAs(handler, "Landroid/os/Handler;",
                        "(Landroid/os/Looper;)V",
                        {VmValue::Ref(looper)});
    const auto runnable = fixture.New("Ltest/RecordingRunnable;");
    SchedulerVm::RequireOk(fixture.Virtual(
        handler, "post", "(Ljava/lang/Runnable;)Z",
        {VmValue::Ref(runnable)}));
    REQUIRE(WaitFor([&] { return fixture.runnable_calls.load() == 1; }));
    CHECK(fixture.messages.empty());
    SchedulerVm::RequireOk(fixture.Virtual(thread, "quit", "()Z"));
    SchedulerVm::RequireOk(fixture.Virtual(thread, "join", "()V"));
    CHECK_FALSE(fixture.threads.IsAlive(thread));
}

TEST_CASE("DVM-85 AsyncTask runs background work then posts to main") {
    SchedulerVm fixture;
    const auto task = fixture.New("Ltest/RecordingAsyncTask;");
    fixture.ConstructAs(task, "Landroid/os/AsyncTask;", "()V");
    const auto params = fixture.model.NewObjectArray(
        fixture.linker.ResolveDescriptor("[Ljava/lang/Object;"),
        fixture.Class("Ljava/lang/Object;"), 0);
    SchedulerVm::RequireOk(fixture.Virtual(
        task, "execute", "([Ljava/lang/Object;)Landroid/os/AsyncTask;",
        {VmValue::Ref(params)}));
    REQUIRE(WaitFor([&] { return fixture.async_background.load() == 1; }));
    CHECK(fixture.async_post.load() == 0);
    CHECK_FALSE(PumpJavaThreads(fixture.vm, *fixture.context).has_value());
    CHECK(fixture.async_post.load() == 1);
    const auto status = fixture.Virtual(
        task, "getStatus", "()Landroid/os/AsyncTask$Status;").value.ref;
    REQUIRE(status.IsValid());
    const auto name = fixture.Virtual(
        status, "name", "()Ljava/lang/String;").value.ref;
    CHECK(fixture.vm.StringUtf8(name) == "FINISHED");
}
