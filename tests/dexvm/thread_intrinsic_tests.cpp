// DVM-48 java.lang.Thread integration tests. Observable behavior is mapped
// to pinned libcore Thread.java/VMThread.java and Dalvik Thread.cpp/Sync.cpp.

#include <doctest/doctest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <limits>
#include <string>
#include <string_view>
#include <thread>
#include <tuple>
#include <utility>
#include <vector>

#include "ogplay/core/capability_ledger.h"
#include "ogplay/runtime/dexvm/class_loader_facade.h"
#include "ogplay/runtime/dexvm/class_linker.h"
#include "ogplay/runtime/dexvm/intrinsic_builder.h"
#include "ogplay/runtime/dexvm/interpreter.h"
#include "ogplay/runtime/dexvm/object_model.h"
#include "ogplay/runtime/dexvm/vm_monitors.h"
#include "ogplay/runtime/dexvm/vm_threads.h"

namespace {

using namespace ogplay::runtime;
using namespace ogplay::runtime::dexvm;

std::vector<std::uint8_t> ReadFixture(const std::string& name) {
    const std::string path =
        std::string(OGPLAY_DEXVM_FIXTURE_DIR) + "/" + name;
    std::ifstream stream(path, std::ios::binary);
    REQUIRE_MESSAGE(stream.good(), "missing fixture: ", path);
    return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(stream),
                                     std::istreambuf_iterator<char>());
}

struct ThreadVm final {
    JniStringStore strings;
    JniPrimitiveArrayStore arrays;
    JavaObjectModel model;
    DexClassLinker linker;
    ogplay::core::CapabilityLedger ledger;
    Interpreter interpreter;
    VmThreadRuntime threads;
    std::atomic<std::int64_t> clock_millis{};
    std::atomic<std::int32_t> uncaught_calls{};
    std::atomic<std::int32_t> default_uncaught_calls{};
    std::atomic<std::int32_t> throwing_uncaught_calls{};

    explicit ThreadVm(
        const InterpreterBackend backend = InterpreterBackend::switch_dispatch)
        : model(strings, arrays),
          linker(),
          interpreter(
              [this]() -> DexClassLinker& {
                  auto catalog = CoreIntrinsicCatalog();
                  auto handler = IntrinsicClassBuilder::Class(
                      "LThreadUncaughtHandler;", "Ljava/lang/Object;",
                      {"Ljava/lang/Thread$UncaughtExceptionHandler;"});
                  handler.VirtualMethod(
                      "uncaughtException",
                      "(Ljava/lang/Thread;Ljava/lang/Throwable;)V",
                      [this](IntrinsicContext&) {
                          ++uncaught_calls;
                          return VmValue::Void();
                      });
                  catalog.push_back(std::move(handler).Build());
                  auto default_handler = IntrinsicClassBuilder::Class(
                      "LThreadDefaultUncaughtHandler;", "Ljava/lang/Object;",
                      {"Ljava/lang/Thread$UncaughtExceptionHandler;"});
                  default_handler.VirtualMethod(
                      "uncaughtException",
                      "(Ljava/lang/Thread;Ljava/lang/Throwable;)V",
                      [this](IntrinsicContext&) {
                          ++default_uncaught_calls;
                          return VmValue::Void();
                      });
                  catalog.push_back(std::move(default_handler).Build());
                  auto throwing_handler = IntrinsicClassBuilder::Class(
                      "LThreadThrowingUncaughtHandler;", "Ljava/lang/Object;",
                      {"Ljava/lang/Thread$UncaughtExceptionHandler;"});
                  throwing_handler.VirtualMethod(
                      "uncaughtException",
                      "(Ljava/lang/Thread;Ljava/lang/Throwable;)V",
                      [this](IntrinsicContext&) -> VmValue {
                          ++throwing_uncaught_calls;
                          throw VmJavaThrow{"Ljava/lang/RuntimeException;",
                                            "handler failure"};
                      });
                  catalog.push_back(std::move(throwing_handler).Build());
                  linker.RegisterIntrinsics(std::move(catalog));
                  linker.RegisterDex(ReadFixture("interp.dex"));
                  linker.Link();
                  return linker;
              }(),
              model, nullptr, ledger, InterpreterConfig{.backend = backend}),
          threads(interpreter) {
        interpreter.Monitors().SetTimeSource(
            [this] { return clock_millis.load(); });
    }

    [[nodiscard]] DexClassId Class(const std::string& descriptor) {
        const auto java_class = linker.FindClass(descriptor);
        REQUIRE_MESSAGE(java_class.has_value(), descriptor);
        return *java_class;
    }

    [[nodiscard]] VmCallOutcome Static(
        const std::string& owner, const std::string& name,
        const std::string& descriptor, std::vector<VmValue> arguments = {}) {
        const auto method =
            linker.FindDirectMethod(Class(owner), name, descriptor);
        const auto qualified = owner + "." + name;
        REQUIRE_MESSAGE(method.has_value(), qualified);
        return interpreter.Call(*method, arguments);
    }

    [[nodiscard]] VmCallOutcome Virtual(
        const VmObjectRef receiver, const std::string& name,
        const std::string& descriptor, std::vector<VmValue> arguments = {}) {
        const auto receiver_class = model.ObjectClass(receiver);
        const auto index =
            linker.FindVtableIndex(receiver_class, name, descriptor);
        const auto signature = name + descriptor;
        REQUIRE_MESSAGE(index.has_value(), signature);
        arguments.insert(arguments.begin(), VmValue::Ref(receiver));
        return interpreter.Call(linker.Class(receiver_class).vtable[*index],
                                arguments);
    }

    [[nodiscard]] VmObjectRef New(const std::string& descriptor) {
        return interpreter.NewIntrinsicInstance(descriptor);
    }

    [[nodiscard]] VmCallOutcome Construct(
        const VmObjectRef object, const std::string& owner,
        const std::string& descriptor, std::vector<VmValue> arguments = {}) {
        const auto constructor =
            linker.FindDirectMethod(Class(owner), "<init>", descriptor);
        const auto signature = owner + descriptor;
        REQUIRE_MESSAGE(constructor.has_value(), signature);
        arguments.insert(arguments.begin(), VmValue::Ref(object));
        return interpreter.Call(*constructor, arguments);
    }

    [[nodiscard]] VmObjectRef Runnable(const std::string& owner) {
        const auto outcome = Static(owner, "make", "()Ljava/lang/Runnable;");
        REQUIRE_MESSAGE(!outcome.exception.IsValid(),
                        outcome.exception_message);
        return outcome.value.ref;
    }

    [[nodiscard]] std::int32_t Observed(const std::string& owner) {
        const auto outcome = Static(owner, "observed", "()I");
        REQUIRE_MESSAGE(!outcome.exception.IsValid(),
                        outcome.exception_message);
        return outcome.value.AsInt();
    }
};

void RequireOk(const VmCallOutcome& outcome) {
    REQUIRE_MESSAGE(!outcome.exception.IsValid(), outcome.exception_message);
}

void RequireThrow(const ThreadVm& vm, const VmCallOutcome& outcome,
                  const std::string& descriptor) {
    REQUIRE(outcome.exception.IsValid());
    CHECK(vm.linker.Class(outcome.exception_class).descriptor == descriptor);
}

template <typename Predicate>
bool WaitFor(Predicate predicate) {
    for (int attempt = 0; attempt < 5000; ++attempt) {
        if (predicate()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
}

}  // namespace

TEST_CASE("dexvm Thread context ClassLoader declarations match API19 shape") {
    const auto catalog = CoreIntrinsicCatalog();
    const auto thread = std::ranges::find_if(catalog, [](const auto& candidate) {
        return candidate.descriptor == "Ljava/lang/Thread;";
    });
    REQUIRE(thread != catalog.end());

    const auto field = std::ranges::find_if(
        thread->fields, [](const auto& candidate) {
            return candidate.name == "contextClassLoader" &&
                   candidate.descriptor == "Ljava/lang/ClassLoader;";
        });
    REQUIRE(field != thread->fields.end());
    CHECK(field->access_flags == 0x0002U);

    for (const auto& [name, descriptor] :
         std::vector<std::pair<std::string_view, std::string_view>>{
             {"getContextClassLoader", "()Ljava/lang/ClassLoader;"},
             {"setContextClassLoader", "(Ljava/lang/ClassLoader;)V"}}) {
        const auto method = std::ranges::find_if(
            thread->methods, [&](const auto& candidate) {
                return candidate.name == name &&
                       candidate.descriptor == descriptor;
            });
        REQUIRE(method != thread->methods.end());
        CHECK(method->access_flags == 0x0001U);
    }
}

TEST_CASE("dexvm Thread uncaught handler declarations match API19 shape") {
    const auto catalog = CoreIntrinsicCatalog();
    const auto thread = std::ranges::find_if(catalog, [](const auto& candidate) {
        return candidate.descriptor == "Ljava/lang/Thread;";
    });
    REQUIRE(thread != catalog.end());

    for (const auto& [name, access] :
         std::vector<std::pair<std::string_view, std::uint32_t>>{
             {"uncaughtHandler", 0x0002U},
             {"defaultUncaughtHandler", 0x000aU}}) {
        const auto field = std::ranges::find_if(
            thread->fields, [&](const auto& candidate) {
                return candidate.name == name &&
                       candidate.descriptor ==
                           "Ljava/lang/Thread$UncaughtExceptionHandler;";
            });
        REQUIRE(field != thread->fields.end());
        CHECK(field->access_flags == access);
    }

    for (const auto& [name, descriptor, access] :
         std::vector<std::tuple<std::string_view, std::string_view,
                                std::uint32_t>>{
             {"getUncaughtExceptionHandler",
              "()Ljava/lang/Thread$UncaughtExceptionHandler;", 0x0001U},
             {"setUncaughtExceptionHandler",
              "(Ljava/lang/Thread$UncaughtExceptionHandler;)V", 0x0001U},
             {"getDefaultUncaughtExceptionHandler",
              "()Ljava/lang/Thread$UncaughtExceptionHandler;", 0x0009U},
             {"setDefaultUncaughtExceptionHandler",
              "(Ljava/lang/Thread$UncaughtExceptionHandler;)V", 0x0009U}}) {
        const auto method = std::ranges::find_if(
            thread->methods, [&](const auto& candidate) {
                return candidate.name == name &&
                       candidate.descriptor == descriptor;
            });
        REQUIRE(method != thread->methods.end());
        CHECK(method->access_flags == access);
    }

    const auto interface =
        std::ranges::find_if(catalog, [](const auto& candidate) {
            return candidate.descriptor ==
                   "Ljava/lang/Thread$UncaughtExceptionHandler;";
        });
    REQUIRE(interface != catalog.end());
    CHECK(interface->is_interface);
    const auto callback =
        std::ranges::find_if(interface->methods, [](const auto& candidate) {
            return candidate.name == "uncaughtException" &&
                   candidate.descriptor ==
                       "(Ljava/lang/Thread;Ljava/lang/Throwable;)V";
        });
    REQUIRE(callback != interface->methods.end());
    CHECK(callback->access_flags == 0x0401U);
}

TEST_CASE("dexvm Thread second priority declarations match API19 shape") {
    const auto catalog = CoreIntrinsicCatalog();
    const auto find_class = [&](const std::string_view descriptor) {
        return std::ranges::find_if(catalog, [&](const auto& candidate) {
            return candidate.descriptor == descriptor;
        });
    };
    const auto thread = find_class("Ljava/lang/Thread;");
    REQUIRE(thread != catalog.end());
    for (const auto& [name, descriptor] :
         std::vector<std::pair<std::string_view, std::string_view>>{
             {"getStackTrace", "()[Ljava/lang/StackTraceElement;"},
             {"getAllStackTraces", "()Ljava/util/Map;"},
             {"getThreadGroup", "()Ljava/lang/ThreadGroup;"},
             {"toString", "()Ljava/lang/String;"}}) {
        CHECK(std::ranges::find_if(thread->methods, [&](const auto& method) {
                  return method.name == name && method.descriptor == descriptor;
              }) != thread->methods.end());
    }
    const auto start = std::ranges::find_if(thread->methods, [](const auto& method) {
        return method.name == "start" && method.descriptor == "()V";
    });
    REQUIRE(start != thread->methods.end());
    CHECK(start->access_flags == 0x0021U);
    const auto destroy =
        std::ranges::find_if(thread->methods, [](const auto& method) {
            return method.name == "destroy" && method.descriptor == "()V";
        });
    REQUIRE(destroy != thread->methods.end());
    CHECK((destroy->access_flags & 0x0010U) == 0U);

    const auto stack = find_class("Ljava/lang/StackTraceElement;");
    REQUIRE(stack != catalog.end());
    for (const auto name : {"getClassName", "getMethodName", "getFileName",
                            "getLineNumber", "isNativeMethod", "equals",
                            "hashCode", "toString"}) {
        CHECK(std::ranges::find_if(stack->methods, [&](const auto& method) {
                  return method.name == name;
              }) != stack->methods.end());
    }
    CHECK(find_class("Ljava/lang/ThreadGroup;") != catalog.end());
}

TEST_CASE("dexvm Thread third priority declarations match API19 shape") {
    const auto catalog = CoreIntrinsicCatalog();
    const auto find_class = [&](const std::string_view descriptor) {
        return std::ranges::find_if(catalog, [&](const auto& candidate) {
            return candidate.descriptor == descriptor;
        });
    };
    const auto thread = find_class("Ljava/lang/Thread;");
    REQUIRE(thread != catalog.end());
    for (const auto& [name, descriptor] :
         std::vector<std::pair<std::string_view, std::string_view>>{
             {"<init>", "(Ljava/lang/ThreadGroup;Ljava/lang/Runnable;)V"},
             {"<init>", "(Ljava/lang/ThreadGroup;Ljava/lang/Runnable;Ljava/lang/String;)V"},
             {"<init>", "(Ljava/lang/ThreadGroup;Ljava/lang/Runnable;Ljava/lang/String;J)V"},
             {"<init>", "(Ljava/lang/ThreadGroup;Ljava/lang/String;)V"},
             {"activeCount", "()I"},
             {"enumerate", "([Ljava/lang/Thread;)I"},
             {"dumpStack", "()V"},
             {"countStackFrames", "()I"},
             {"getState", "()Ljava/lang/Thread$State;"},
             {"checkAccess", "()V"},
             {"parkFor", "(J)V"},
             {"parkUntil", "(J)V"},
             {"unpark", "()V"},
             {"pushInterruptAction$", "(Ljava/lang/Runnable;)V"},
             {"popInterruptAction$", "(Ljava/lang/Runnable;)V"},
             {"stop", "(Ljava/lang/Throwable;)V"}}) {
        CHECK(std::ranges::find_if(thread->methods, [&](const auto& method) {
                  return method.name == name && method.descriptor == descriptor;
              }) != thread->methods.end());
    }

    const auto state = find_class("Ljava/lang/Thread$State;");
    REQUIRE(state != catalog.end());
    REQUIRE(state->superclass.has_value());
    CHECK(*state->superclass == "Ljava/lang/Enum;");
    for (const auto name : {"NEW", "RUNNABLE", "BLOCKED", "WAITING",
                            "TIMED_WAITING", "TERMINATED"}) {
        CHECK(std::ranges::find_if(state->fields, [&](const auto& field) {
                  return field.name == name &&
                         field.descriptor == "Ljava/lang/Thread$State;" &&
                         field.is_static;
              }) != state->fields.end());
    }
}

TEST_CASE("DVM-98 Thread State uses generated enum methods") {
    ThreadVm vm;
    constexpr auto descriptor = "Ljava/lang/Thread$State;";
    const auto first = vm.Static(
        descriptor, "values", "()[Ljava/lang/Thread$State;");
    const auto second = vm.Static(
        descriptor, "values", "()[Ljava/lang/Thread$State;");
    REQUIRE_FALSE(first.exception.IsValid());
    REQUIRE_FALSE(second.exception.IsValid());
    CHECK(first.value.ref != second.value.ref);
    REQUIRE(vm.model.ArrayLength(first.value.ref) == 6);

    const auto runnable = vm.model.GetObjectElement(first.value.ref, 1);
    CHECK(vm.interpreter.StringUtf8(
              vm.Virtual(runnable, "name", "()Ljava/lang/String;").value.ref) ==
          "RUNNABLE");
    const auto by_name = vm.Static(
        descriptor, "valueOf",
        "(Ljava/lang/String;)Ljava/lang/Thread$State;",
        {VmValue::Ref(vm.interpreter.NewStringUtf8("RUNNABLE"))});
    REQUIRE_FALSE(by_name.exception.IsValid());
    CHECK(by_name.value.ref == runnable);

    const auto state_class = vm.Class(descriptor);
    CHECK(vm.linker.FindFieldRecursive(
              state_class, "$VALUES", "[Ljava/lang/Thread$State;")
              .has_value());
}

TEST_CASE("dexvm Thread uncaught handlers are scoped and accept null") {
    for (const auto backend : {InterpreterBackend::switch_dispatch,
                               InterpreterBackend::threaded}) {
        ThreadVm first_vm(backend);
        const auto first = first_vm.New("Ljava/lang/Thread;");
        const auto second = first_vm.New("Ljava/lang/Thread;");
        RequireOk(first_vm.Construct(first, "Ljava/lang/Thread;", "()V"));
        RequireOk(first_vm.Construct(second, "Ljava/lang/Thread;", "()V"));
        const auto first_handler =
            first_vm.New("LThreadUncaughtHandler;");
        const auto default_handler =
            first_vm.New("LThreadUncaughtHandler;");

        CHECK_FALSE(first_vm
                        .Virtual(first, "getUncaughtExceptionHandler",
                                 "()Ljava/lang/Thread$UncaughtExceptionHandler;")
                        .value.ref.IsValid());
        RequireOk(first_vm.Virtual(
            first, "setUncaughtExceptionHandler",
            "(Ljava/lang/Thread$UncaughtExceptionHandler;)V",
            {VmValue::Ref(first_handler)}));
        CHECK(first_vm
                  .Virtual(first, "getUncaughtExceptionHandler",
                           "()Ljava/lang/Thread$UncaughtExceptionHandler;")
                  .value.ref == first_handler);
        CHECK_FALSE(first_vm
                        .Virtual(second, "getUncaughtExceptionHandler",
                                 "()Ljava/lang/Thread$UncaughtExceptionHandler;")
                        .value.ref.IsValid());

        RequireOk(first_vm.Static(
            "Ljava/lang/Thread;", "setDefaultUncaughtExceptionHandler",
            "(Ljava/lang/Thread$UncaughtExceptionHandler;)V",
            {VmValue::Ref(default_handler)}));
        CHECK(first_vm
                  .Static("Ljava/lang/Thread;",
                          "getDefaultUncaughtExceptionHandler",
                          "()Ljava/lang/Thread$UncaughtExceptionHandler;")
                  .value.ref == default_handler);

        RequireOk(first_vm.Virtual(
            first, "setUncaughtExceptionHandler",
            "(Ljava/lang/Thread$UncaughtExceptionHandler;)V",
            {VmValue::Ref(VmObjectRef{})}));
        RequireOk(first_vm.Static(
            "Ljava/lang/Thread;", "setDefaultUncaughtExceptionHandler",
            "(Ljava/lang/Thread$UncaughtExceptionHandler;)V",
            {VmValue::Ref(VmObjectRef{})}));
        CHECK_FALSE(first_vm
                        .Virtual(first, "getUncaughtExceptionHandler",
                                 "()Ljava/lang/Thread$UncaughtExceptionHandler;")
                        .value.ref.IsValid());
        CHECK_FALSE(first_vm
                        .Static("Ljava/lang/Thread;",
                                "getDefaultUncaughtExceptionHandler",
                                "()Ljava/lang/Thread$UncaughtExceptionHandler;")
                        .value.ref.IsValid());

        ThreadVm second_vm(backend);
        CHECK_FALSE(second_vm
                        .Static("Ljava/lang/Thread;",
                                "getDefaultUncaughtExceptionHandler",
                                "()Ljava/lang/Thread$UncaughtExceptionHandler;")
                        .value.ref.IsValid());
    }
}

TEST_CASE("dexvm Thread dispatches explicit and default uncaught handlers") {
    for (const auto backend : {InterpreterBackend::switch_dispatch,
                               InterpreterBackend::threaded}) {
        ThreadVm vm(backend);
        const auto handler = vm.New("LThreadUncaughtHandler;");
        const auto default_handler =
            vm.New("LThreadDefaultUncaughtHandler;");
        const auto thrower = vm.Runnable("LThreadThrower;");

        RequireOk(vm.Static(
            "Ljava/lang/Thread;", "setDefaultUncaughtExceptionHandler",
            "(Ljava/lang/Thread$UncaughtExceptionHandler;)V",
            {VmValue::Ref(default_handler)}));

        const auto explicit_thread = vm.New("Ljava/lang/Thread;");
        RequireOk(vm.Construct(explicit_thread, "Ljava/lang/Thread;",
                               "(Ljava/lang/Runnable;)V",
                               {VmValue::Ref(thrower)}));
        RequireOk(vm.Virtual(
            explicit_thread, "setUncaughtExceptionHandler",
            "(Ljava/lang/Thread$UncaughtExceptionHandler;)V",
            {VmValue::Ref(handler)}));
        RequireOk(vm.Virtual(explicit_thread, "start", "()V"));
        RequireOk(vm.Virtual(explicit_thread, "join", "()V"));
        CHECK(vm.uncaught_calls.load() == 1);
        CHECK(vm.default_uncaught_calls.load() == 0);
        CHECK_FALSE(vm.threads.TakeFailure().has_value());

        const auto default_thread = vm.New("Ljava/lang/Thread;");
        RequireOk(vm.Construct(default_thread, "Ljava/lang/Thread;",
                               "(Ljava/lang/Runnable;)V",
                               {VmValue::Ref(thrower)}));
        RequireOk(vm.Virtual(default_thread, "start", "()V"));
        RequireOk(vm.Virtual(default_thread, "join", "()V"));
        CHECK(vm.uncaught_calls.load() == 1);
        CHECK(vm.default_uncaught_calls.load() == 1);
        CHECK_FALSE(vm.threads.TakeFailure().has_value());

        const auto throwing_handler =
            vm.New("LThreadThrowingUncaughtHandler;");
        const auto handler_failure_thread = vm.New("Ljava/lang/Thread;");
        RequireOk(vm.Construct(handler_failure_thread, "Ljava/lang/Thread;",
                               "(Ljava/lang/Runnable;)V",
                               {VmValue::Ref(thrower)}));
        RequireOk(vm.Virtual(
            handler_failure_thread, "setUncaughtExceptionHandler",
            "(Ljava/lang/Thread$UncaughtExceptionHandler;)V",
            {VmValue::Ref(throwing_handler)}));
        RequireOk(vm.Virtual(handler_failure_thread, "start", "()V"));
        RequireOk(vm.Virtual(handler_failure_thread, "join", "()V"));
        CHECK(vm.throwing_uncaught_calls.load() == 1);
        CHECK_FALSE(vm.threads.TakeFailure().has_value());
    }
}

TEST_CASE("dexvm Thread diagnostics and group APIs follow bounded API19 semantics") {
    for (const auto backend : {InterpreterBackend::switch_dispatch,
                               InterpreterBackend::threaded}) {
        ThreadVm vm(backend);
        const auto root =
            vm.Static("Ljava/lang/Thread;", "currentThread",
                      "()Ljava/lang/Thread;")
                .value.ref;
        const auto group =
            vm.Virtual(root, "getThreadGroup", "()Ljava/lang/ThreadGroup;")
                .value.ref;
        REQUIRE(group.IsValid());
        CHECK(vm.interpreter.StringUtf8(
                  vm.Virtual(group, "getName", "()Ljava/lang/String;")
                      .value.ref) == "main");
        CHECK(vm.interpreter.StringUtf8(
                  vm.Virtual(root, "toString", "()Ljava/lang/String;")
                      .value.ref) == "Thread[main,5,main]");
        CHECK(vm.Virtual(group, "activeCount", "()I").value.AsInt() == 1);

        const auto stack =
            vm.Virtual(root, "getStackTrace",
                       "()[Ljava/lang/StackTraceElement;")
                .value.ref;
        CHECK(vm.model.ObjectClass(stack) ==
              vm.Class("[Ljava/lang/StackTraceElement;"));
        const auto element = vm.New("Ljava/lang/StackTraceElement;");
        RequireOk(vm.Construct(
            element, "Ljava/lang/StackTraceElement;",
            "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;I)V",
            {VmValue::Ref(vm.interpreter.NewStringUtf8("sample.Type")),
             VmValue::Ref(vm.interpreter.NewStringUtf8("run")),
             VmValue::Ref(VmObjectRef{}), VmValue::Int(-1)}));
        CHECK(vm.interpreter.StringUtf8(
                  vm.Virtual(element, "toString", "()Ljava/lang/String;")
                      .value.ref) == "sample.Type.run(Unknown Source)");
        const auto all =
            vm.Static("Ljava/lang/Thread;", "getAllStackTraces",
                      "()Ljava/util/Map;")
                .value.ref;
        CHECK(vm.Virtual(all, "size", "()I").value.AsInt() == 1);

        const auto child = vm.New("Ljava/lang/Thread;");
        RequireOk(vm.Construct(child, "Ljava/lang/Thread;", "()V"));
        CHECK(vm.Virtual(child, "getThreadGroup",
                         "()Ljava/lang/ThreadGroup;")
                  .value.ref == group);
        RequireOk(vm.Virtual(child, "start", "()V"));
        RequireOk(vm.Virtual(child, "join", "()V"));
        CHECK_FALSE(vm.Virtual(child, "getThreadGroup",
                               "()Ljava/lang/ThreadGroup;")
                        .value.ref.IsValid());
    }
}

TEST_CASE("dexvm Thread third priority APIs expose bounded API19 behavior") {
    for (const auto backend : {InterpreterBackend::switch_dispatch,
                               InterpreterBackend::threaded}) {
        ThreadVm vm(backend);
        const auto root =
            vm.Static("Ljava/lang/Thread;", "currentThread",
                      "()Ljava/lang/Thread;")
                .value.ref;
        const auto group =
            vm.Virtual(root, "getThreadGroup", "()Ljava/lang/ThreadGroup;")
                .value.ref;

        CHECK(vm.Static("Ljava/lang/Thread;", "activeCount", "()I")
                  .value.AsInt() == 1);
        const auto threads = vm.model.NewObjectArray(
            vm.linker.ResolveDescriptor("[Ljava/lang/Thread;"),
            vm.Class("Ljava/lang/Thread;"), 4);
        CHECK(vm.Static("Ljava/lang/Thread;", "enumerate",
                        "([Ljava/lang/Thread;)I",
                        {VmValue::Ref(threads)})
                  .value.AsInt() == 1);
        CHECK(vm.model.GetObjectElement(threads, 0) == root);
        RequireOk(vm.Virtual(root, "checkAccess", "()V"));
        CHECK(vm.Virtual(root, "countStackFrames", "()I").value.AsInt() >=
              0);
        RequireOk(vm.Static("Ljava/lang/Thread;", "dumpStack", "()V"));

        const auto root_state =
            vm.Virtual(root, "getState", "()Ljava/lang/Thread$State;")
                .value.ref;
        CHECK(vm.interpreter.StringUtf8(
                  vm.Virtual(root_state, "name", "()Ljava/lang/String;")
                      .value.ref) == "RUNNABLE");

        const auto child = vm.New("Ljava/lang/Thread;");
        const auto child_name = vm.interpreter.NewStringUtf8("group-child");
        RequireOk(vm.Construct(
            child, "Ljava/lang/Thread;",
            "(Ljava/lang/ThreadGroup;Ljava/lang/Runnable;Ljava/lang/String;J)V",
            {VmValue::Ref(group), VmValue::Ref(VmObjectRef{}),
             VmValue::Ref(child_name), VmValue::Long(4096)}));
        CHECK(vm.Virtual(child, "getThreadGroup",
                         "()Ljava/lang/ThreadGroup;")
                  .value.ref == group);
        const auto new_state =
            vm.Virtual(child, "getState", "()Ljava/lang/Thread$State;")
                .value.ref;
        CHECK(vm.interpreter.StringUtf8(
                  vm.Virtual(new_state, "name", "()Ljava/lang/String;")
                      .value.ref) == "NEW");
        RequireOk(vm.Virtual(child, "start", "()V"));
        RequireOk(vm.Virtual(child, "join", "()V"));
        const auto terminated_state =
            vm.Virtual(child, "getState", "()Ljava/lang/Thread$State;")
                .value.ref;
        CHECK(vm.interpreter.StringUtf8(
                  vm.Virtual(terminated_state, "name",
                             "()Ljava/lang/String;")
                      .value.ref) == "TERMINATED");
    }
}

TEST_CASE("dexvm Thread park permit and interrupt actions follow API19 rules") {
    for (const auto backend : {InterpreterBackend::switch_dispatch,
                               InterpreterBackend::threaded}) {
        ThreadVm vm(backend);
        const auto root =
            vm.Static("Ljava/lang/Thread;", "currentThread",
                      "()Ljava/lang/Thread;")
                .value.ref;

        RequireOk(vm.Virtual(root, "unpark", "()V"));
        RequireOk(vm.Virtual(root, "parkFor", "(J)V",
                             {VmValue::Long(
                                 std::numeric_limits<std::int64_t>::max())}));
        RequireThrow(vm,
                     vm.Virtual(root, "parkFor", "(J)V",
                                {VmValue::Long(-1)}),
                     "Ljava/lang/IllegalArgumentException;");
        RequireOk(vm.Virtual(root, "parkUntil", "(J)V",
                             {VmValue::Long(0)}));

        std::vector<std::int64_t> advances;
        vm.interpreter.Monitors().SetClockAdvance(
            [&](const std::int64_t delta_millis) {
                vm.clock_millis += delta_millis;
                advances.push_back(delta_millis);
            });
        RequireOk(vm.Virtual(root, "parkFor", "(J)V",
                             {VmValue::Long(1'500'001)}));
        REQUIRE(advances.size() == 1U);
        CHECK(advances.front() == 2);

        const auto action = vm.Runnable("LThreadProbe;");
        RequireOk(vm.Virtual(root, "pushInterruptAction$",
                             "(Ljava/lang/Runnable;)V",
                             {VmValue::Ref(action)}));
        RequireOk(vm.Virtual(root, "interrupt", "()V"));
        CHECK(vm.Static("LThreadProbe;", "counter", "()I")
                  .value.AsInt() == 4950);
        RequireOk(vm.Virtual(root, "popInterruptAction$",
                             "(Ljava/lang/Runnable;)V",
                             {VmValue::Ref(action)}));
        CHECK(vm.Static("Ljava/lang/Thread;", "interrupted", "()Z")
                  .value.AsInt() == 1);
        RequireOk(vm.Virtual(root, "interrupt", "()V"));
        CHECK(vm.Static("LThreadProbe;", "counter", "()I")
                  .value.AsInt() == 4950);
    }
}

TEST_CASE("dexvm Thread constructors allocate stable per-VM identity") {
    ThreadVm vm;
    const auto root_first =
        vm.Static("Ljava/lang/Thread;", "currentThread",
                  "()Ljava/lang/Thread;");
    const auto root_second =
        vm.Static("Ljava/lang/Thread;", "currentThread",
                  "()Ljava/lang/Thread;");
    RequireOk(root_first);
    RequireOk(root_second);
    CHECK(root_first.value.ref == root_second.value.ref);
    CHECK(vm.Virtual(root_first.value.ref, "getId", "()J").value.AsLong() ==
          1);
    CHECK(vm.interpreter.StringUtf8(
              vm.Virtual(root_first.value.ref, "getName",
                         "()Ljava/lang/String;")
                  .value.ref) == "main");
    CHECK(vm.Virtual(root_first.value.ref, "getPriority", "()I")
              .value.AsInt() == 5);
    CHECK(vm.Virtual(root_first.value.ref, "isDaemon", "()Z")
              .value.AsInt() == 0);
    RequireOk(vm.Virtual(root_first.value.ref, "setPriority", "(I)V",
                         {VmValue::Int(7)}));

    const auto runnable = vm.Runnable("LThreadProbe;");
    const auto plain = vm.New("Ljava/lang/Thread;");
    const auto with_target = vm.New("Ljava/lang/Thread;");
    const auto with_name = vm.New("Ljava/lang/Thread;");
    const auto with_both = vm.New("Ljava/lang/Thread;");
    RequireOk(vm.Construct(plain, "Ljava/lang/Thread;", "()V"));
    RequireOk(vm.Construct(with_target, "Ljava/lang/Thread;",
                           "(Ljava/lang/Runnable;)V",
                           {VmValue::Ref(runnable)}));
    const auto named = vm.interpreter.NewStringUtf8("worker");
    RequireOk(vm.Construct(with_name, "Ljava/lang/Thread;",
                           "(Ljava/lang/String;)V",
                           {VmValue::Ref(named)}));
    RequireOk(vm.Construct(with_both, "Ljava/lang/Thread;",
                           "(Ljava/lang/Runnable;Ljava/lang/String;)V",
                           {VmValue::Ref(runnable), VmValue::Ref(named)}));

    const auto id_plain = vm.Virtual(plain, "getId", "()J").value.AsLong();
    const auto id_target =
        vm.Virtual(with_target, "getId", "()J").value.AsLong();
    CHECK(id_plain > 1);
    CHECK(id_target > id_plain);
    CHECK(vm.Virtual(plain, "getPriority", "()I").value.AsInt() == 7);
    CHECK(vm.Virtual(plain, "getId", "()J").value.AsLong() == id_plain);
    CHECK(vm.interpreter.StringUtf8(
              vm.Virtual(plain, "getName", "()Ljava/lang/String;").value.ref) ==
          "Thread-" + std::to_string(id_plain));
    CHECK(vm.interpreter.StringUtf8(
              vm.Virtual(with_name, "getName", "()Ljava/lang/String;")
                  .value.ref) == "worker");

    const auto bad = vm.New("Ljava/lang/Thread;");
    RequireThrow(vm,
                 vm.Construct(bad, "Ljava/lang/Thread;",
                              "(Ljava/lang/String;)V",
                              {VmValue::Ref(VmObjectRef{})}),
                 "Ljava/lang/NullPointerException;");
}

TEST_CASE("dexvm Thread context ClassLoader defaults inherits and accepts null") {
    for (const auto backend : {InterpreterBackend::switch_dispatch,
                               InterpreterBackend::threaded}) {
        CAPTURE(backend == InterpreterBackend::threaded ? "threaded" :
                                                         "switch");
        ThreadVm vm(backend);
        const auto root = vm.Static(
            "Ljava/lang/Thread;", "currentThread", "()Ljava/lang/Thread;");
        RequireOk(root);
        const auto application = vm.interpreter.ClassLoaders().ApplicationLoader();
        const auto bootstrap = vm.interpreter.ClassLoaders().BootstrapLoader();
        CHECK(vm.Virtual(root.value.ref, "getContextClassLoader",
                         "()Ljava/lang/ClassLoader;").value.ref == application);

        RequireOk(vm.Virtual(
            root.value.ref, "setContextClassLoader",
            "(Ljava/lang/ClassLoader;)V", {VmValue::Ref(bootstrap)}));
        CHECK(vm.Virtual(root.value.ref, "getContextClassLoader",
                         "()Ljava/lang/ClassLoader;").value.ref == bootstrap);

        const auto child = vm.New("Ljava/lang/Thread;");
        RequireOk(vm.Construct(child, "Ljava/lang/Thread;", "()V"));
        CHECK(vm.Virtual(child, "getContextClassLoader",
                         "()Ljava/lang/ClassLoader;").value.ref == bootstrap);
        RequireOk(vm.Virtual(
            child, "setContextClassLoader",
            "(Ljava/lang/ClassLoader;)V", {VmValue::Ref(VmObjectRef{})}));
        CHECK_FALSE(vm.Virtual(child, "getContextClassLoader",
                               "()Ljava/lang/ClassLoader;").value.ref.IsValid());
        CHECK(vm.Virtual(root.value.ref, "getContextClassLoader",
                         "()Ljava/lang/ClassLoader;").value.ref == bootstrap);
    }
}

TEST_CASE("dexvm Thread.start virtual-dispatches this.run and currentThread") {
    ThreadVm vm;
    const auto target = vm.Runnable("LThreadProbe;");
    const auto overridden = vm.New("LThreadOverride;");
    RequireOk(vm.Construct(overridden, "LThreadOverride;",
                           "(Ljava/lang/Runnable;)V",
                           {VmValue::Ref(target)}));
    RequireOk(vm.Virtual(overridden, "start", "()V"));
    RequireOk(vm.Virtual(overridden, "join", "()V"));
    CHECK(vm.Observed("LThreadOverride;") == 1);
    const auto target_counter =
        vm.Static("LThreadProbe;", "counter", "()I");
    RequireOk(target_counter);
    CHECK(target_counter.value.AsInt() == 0);
    RequireThrow(vm, vm.Virtual(overridden, "start", "()V"),
                 "Ljava/lang/IllegalThreadStateException;");

    const auto base = vm.New("Ljava/lang/Thread;");
    RequireOk(vm.Construct(base, "Ljava/lang/Thread;",
                           "(Ljava/lang/Runnable;)V",
                           {VmValue::Ref(target)}));
    const auto stable_id = vm.Virtual(base, "getId", "()J").value.AsLong();
    RequireOk(vm.Virtual(base, "start", "()V"));
    RequireOk(vm.Virtual(base, "join", "()V"));
    CHECK(vm.Static("LThreadProbe;", "counter", "()I").value.AsInt() ==
          4950);
    CHECK(vm.Virtual(base, "getId", "()J").value.AsLong() == stable_id);
    RequireOk(vm.Virtual(base, "interrupt", "()V"));
    CHECK(vm.Virtual(base, "isInterrupted", "()Z").value.AsInt() == 0);

    const auto identity = vm.New("LThreadIdentity;");
    RequireOk(vm.Construct(identity, "LThreadIdentity;", "()V"));
    RequireOk(vm.Virtual(identity, "start", "()V"));
    RequireOk(vm.Virtual(identity, "join", "()V"));
    CHECK(vm.Observed("LThreadIdentity;") == 1);
    CHECK(vm.Virtual(identity, "isAlive", "()Z").value.AsInt() == 0);
}

TEST_CASE("dexvm Thread name priority daemon and holdsLock are Java facts") {
    ThreadVm vm;
    const auto thread = vm.New("Ljava/lang/Thread;");
    RequireOk(vm.Construct(thread, "Ljava/lang/Thread;", "()V"));
    RequireOk(vm.Virtual(thread, "setPriority", "(I)V", {VmValue::Int(7)}));
    CHECK(vm.Virtual(thread, "getPriority", "()I").value.AsInt() == 7);
    RequireThrow(vm,
                 vm.Virtual(thread, "setPriority", "(I)V",
                            {VmValue::Int(11)}),
                 "Ljava/lang/IllegalArgumentException;");
    RequireOk(vm.Virtual(thread, "setDaemon", "(Z)V", {VmValue::Int(1)}));
    CHECK(vm.Virtual(thread, "isDaemon", "()Z").value.AsInt() == 1);

    const auto renamed = vm.interpreter.NewStringUtf8("renamed");
    RequireOk(vm.Virtual(thread, "setName", "(Ljava/lang/String;)V",
                         {VmValue::Ref(renamed)}));
    CHECK(vm.interpreter.StringUtf8(
              vm.Virtual(thread, "getName", "()Ljava/lang/String;").value.ref) ==
          "renamed");
    RequireThrow(vm,
                 vm.Virtual(thread, "setName", "(Ljava/lang/String;)V",
                            {VmValue::Ref(VmObjectRef{})}),
                 "Ljava/lang/NullPointerException;");

    const auto lock = vm.New("Ljava/lang/Object;");
    const auto outside = vm.Static("Ljava/lang/Thread;", "holdsLock",
                                   "(Ljava/lang/Object;)Z",
                                   {VmValue::Ref(lock)});
    RequireOk(outside);
    CHECK(outside.value.AsInt() == 0);
    vm.interpreter.Monitors().Enter(lock,
                                    vm.interpreter.CurrentContextToken());
    const auto inside = vm.Static("Ljava/lang/Thread;", "holdsLock",
                                  "(Ljava/lang/Object;)Z",
                                  {VmValue::Ref(lock)});
    RequireOk(inside);
    CHECK(inside.value.AsInt() == 1);
    vm.interpreter.Monitors().Exit(lock,
                                   vm.interpreter.CurrentContextToken());
    vm.interpreter.Monitors().Enter(lock, 2U);
    const auto owned_by_other =
        vm.Static("Ljava/lang/Thread;", "holdsLock",
                  "(Ljava/lang/Object;)Z", {VmValue::Ref(lock)});
    RequireOk(owned_by_other);
    CHECK(owned_by_other.value.AsInt() == 0);
    vm.interpreter.Monitors().Exit(lock, 2U);
    RequireThrow(vm,
                 vm.Static("Ljava/lang/Thread;", "holdsLock",
                           "(Ljava/lang/Object;)Z",
                           {VmValue::Ref(VmObjectRef{})}),
                 "Ljava/lang/NullPointerException;");
}

TEST_CASE("dexvm Thread interrupt and sleep share one runtime flag and clock") {
    ThreadVm vm;
    const auto root =
        vm.Static("Ljava/lang/Thread;", "currentThread",
                  "()Ljava/lang/Thread;")
            .value.ref;
    RequireOk(vm.Virtual(root, "interrupt", "()V"));
    CHECK(vm.Virtual(root, "isInterrupted", "()Z").value.AsInt() == 1);
    CHECK(vm.Virtual(root, "isInterrupted", "()Z").value.AsInt() == 1);
    CHECK(vm.Static("Ljava/lang/Thread;", "interrupted", "()Z")
              .value.AsInt() == 1);
    CHECK(vm.Static("Ljava/lang/Thread;", "interrupted", "()Z")
              .value.AsInt() == 0);

    RequireOk(vm.Static("Ljava/lang/Thread;", "sleep", "(JI)V",
                        {VmValue::Long(0), VmValue::Int(0)}));
    RequireThrow(vm,
                 vm.Static("Ljava/lang/Thread;", "sleep", "(JI)V",
                           {VmValue::Long(-1), VmValue::Int(0)}),
                 "Ljava/lang/IllegalArgumentException;");
    RequireThrow(vm,
                 vm.Static("Ljava/lang/Thread;", "sleep", "(JI)V",
                           {VmValue::Long(0), VmValue::Int(1'000'000)}),
                 "Ljava/lang/IllegalArgumentException;");

    const auto sleeper = vm.New("LThreadSleeper;");
    RequireOk(vm.Construct(sleeper, "LThreadSleeper;", "()V"));
    RequireOk(vm.Virtual(sleeper, "start", "()V"));
    REQUIRE(WaitFor([&] { return vm.threads.IsAlive(sleeper); }));
    RequireOk(vm.Virtual(sleeper, "interrupt", "()V"));
    RequireOk(vm.Virtual(sleeper, "join", "()V"));
    CHECK(vm.Observed("LThreadSleeper;") == 2);
    CHECK(vm.Virtual(sleeper, "isInterrupted", "()Z").value.AsInt() == 0);
}

TEST_CASE("dexvm timed join returns at the injected monotonic deadline") {
    ThreadVm vm;
    const auto target = vm.New("LThreadSleeper;");
    RequireOk(vm.Construct(target, "LThreadSleeper;", "()V"));
    const auto thread = vm.New("Ljava/lang/Thread;");
    RequireOk(vm.Construct(thread, "Ljava/lang/Thread;",
                           "(Ljava/lang/Runnable;)V",
                           {VmValue::Ref(target)}));
    RequireOk(vm.Virtual(thread, "start", "()V"));
    REQUIRE(WaitFor([&] { return vm.threads.IsAlive(thread); }));

    const auto root =
        vm.Static("Ljava/lang/Thread;", "currentThread",
                  "()Ljava/lang/Thread;")
            .value.ref;
    RequireOk(vm.Virtual(root, "interrupt", "()V"));
    const auto interrupted_join = vm.Virtual(thread, "join", "()V");
    RequireThrow(vm, interrupted_join, "Ljava/lang/InterruptedException;");
    CHECK(vm.Virtual(root, "isInterrupted", "()Z").value.AsInt() == 0);

    const auto renamed = vm.interpreter.NewStringUtf8("live-spin");
    RequireOk(vm.Virtual(thread, "setName", "(Ljava/lang/String;)V",
                         {VmValue::Ref(renamed)}));
    bool saw_rename = false;
    for (const auto& entry : vm.threads.Snapshot()) {
        if (entry.object == thread.Value() && entry.name == "live-spin") {
            saw_rename = true;
        }
    }
    CHECK(saw_rename);
    RequireThrow(vm,
                 vm.Virtual(thread, "setDaemon", "(Z)V",
                            {VmValue::Int(1)}),
                 "Ljava/lang/IllegalThreadStateException;");

    const auto active_mark = vm.interpreter.MarkReachable();
    CHECK(active_mark.IsMarked(root));
    CHECK(active_mark.IsMarked(thread));
    CHECK(active_mark.IsMarked(target));

    std::thread clock_driver([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        vm.clock_millis.store(49);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        vm.clock_millis.store(50);
    });
    RequireOk(vm.Virtual(thread, "join", "(JI)V",
                         {VmValue::Long(49), VmValue::Int(1)}));
    clock_driver.join();
    CHECK(vm.threads.IsAlive(thread));
    vm.threads.Shutdown();
    const auto finished_mark = vm.interpreter.MarkReachable();
    CHECK(finished_mark.IsMarked(root));
    CHECK_FALSE(finished_mark.IsMarked(thread));
}

TEST_CASE("dexvm timed self join follows the Clock instead of throwing") {
    ThreadVm vm;
    const auto root =
        vm.Static("Ljava/lang/Thread;", "currentThread",
                  "()Ljava/lang/Thread;")
            .value.ref;
    std::thread clock_driver([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        vm.clock_millis.store(1);
    });
    RequireOk(vm.Virtual(root, "join", "(J)V", {VmValue::Long(1)}));
    clock_driver.join();
    CHECK(vm.Virtual(root, "isAlive", "()Z").value.AsInt() == 1);
}

// Root lifecycle timed parks cannot wait a deterministic clock out: the
// host thread parking here is the frame pump that would advance it. The
// session publishes a clock advance for exactly that case (the Android
// bridge wires AdvanceAndroidClock); pre-fix this sleep parked forever,
// which is how a title licence poll inside onCreate black-screened runs.
TEST_CASE("dexvm root Thread.sleep fast-forwards a pump-driven clock") {
    ThreadVm vm;
    std::vector<std::int64_t> advances;
    vm.interpreter.Monitors().SetClockAdvance(
        [&](const std::int64_t delta_millis) {
            vm.clock_millis += delta_millis;
            advances.push_back(delta_millis);
        });
    RequireOk(vm.Static("Ljava/lang/Thread;", "sleep", "(JI)V",
                        {VmValue::Long(50), VmValue::Int(0)}));
    CHECK(advances.size() == 1U);
    CHECK(advances.front() == 50);
    CHECK(vm.clock_millis.load() == 50);
}

TEST_CASE("dexvm root timed join fast-forwards without an external pump") {
    ThreadVm vm;
    std::vector<std::int64_t> advances;
    bool observed_joining{};
    vm.interpreter.Monitors().SetClockAdvance(
        [&](const std::int64_t delta_millis) {
            observed_joining = std::ranges::any_of(
                vm.threads.Snapshot(), [](const auto& thread) {
                    return thread.context_token == kRootLifecycleToken &&
                           thread.wait_state == VmThreadWaitState::joining;
                });
            vm.clock_millis += delta_millis;
            advances.push_back(delta_millis);
        });
    const auto sleeper = vm.New("LThreadSleeper;");
    RequireOk(vm.Construct(sleeper, "LThreadSleeper;", "()V"));
    RequireOk(vm.Virtual(sleeper, "start", "()V"));
    REQUIRE(WaitFor([&] { return vm.threads.IsAlive(sleeper); }));
    RequireOk(vm.Virtual(sleeper, "join", "(JI)V",
                         {VmValue::Long(30), VmValue::Int(0)}));
    CHECK(advances.size() == 1U);
    CHECK(advances.front() == 30);
    CHECK(observed_joining);
    // ThreadSleeper parks in sleep(100) on its own context; only the root
    // park fast-forwarded, so the target outlives its 30 ms join.
    CHECK(vm.threads.IsAlive(sleeper));
}

// Worker contexts normally stay frame-driven. Without a blocked-driver
// signal they must not consume the root fast-forward hook.
TEST_CASE("dexvm worker Thread.sleep parks on the clock, not the advance") {
    ThreadVm vm;
    std::atomic<int> advances{0};
    vm.interpreter.Monitors().SetClockAdvance([&](const std::int64_t) {
        ++advances;
    });
    const auto sleeper = vm.New("LThreadSleeper;");
    RequireOk(vm.Construct(sleeper, "LThreadSleeper;", "()V"));
    RequireOk(vm.Virtual(sleeper, "start", "()V"));
    REQUIRE(WaitFor([&] {
        return std::ranges::any_of(
            vm.threads.Snapshot(), [sleeper](const auto& thread) {
                return thread.object == sleeper.Value() &&
                       thread.wait_state == VmThreadWaitState::sleeping;
            });
    }));
    while (vm.threads.IsAlive(sleeper)) {
        vm.clock_millis += 10;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    RequireOk(vm.Virtual(sleeper, "join", "()V"));
    CHECK(vm.Observed("LThreadSleeper;") == 1);
    CHECK(advances.load() == 0);
}

// A synchronous SurfaceView resize can park the lifecycle clock driver while
// its GL worker applies a frame limiter with Thread.sleep. In that state the
// worker is the only execution capable of completing the callback, so its
// timed park may advance the same deterministic Clock. Closing the gate below
// leaves it parked; opening it must release exactly one deadline.
TEST_CASE("dexvm worker Thread.sleep advances only while clock driver is blocked") {
    ThreadVm vm;
    std::atomic<bool> driver_blocked{};
    std::atomic<int> advances{};
    vm.interpreter.Monitors().SetClockAdvance(
        [&](const std::int64_t delta_millis) {
            vm.clock_millis += delta_millis;
            ++advances;
        });
    vm.interpreter.Monitors().SetClockDriverBlockedProbe(
        [&] { return driver_blocked.load(); });
    const auto sleeper = vm.New("LThreadSleeper;");
    RequireOk(vm.Construct(sleeper, "LThreadSleeper;", "()V"));
    RequireOk(vm.Virtual(sleeper, "start", "()V"));
    REQUIRE(WaitFor([&] {
        return std::ranges::any_of(
            vm.threads.Snapshot(), [sleeper](const auto& thread) {
                return thread.object == sleeper.Value() &&
                       thread.wait_state == VmThreadWaitState::sleeping;
            });
    }));
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    CHECK(vm.threads.IsAlive(sleeper));
    CHECK(advances.load() == 0);

    driver_blocked.store(true);
    REQUIRE(WaitFor([&] { return !vm.threads.IsAlive(sleeper); }));
    RequireOk(vm.Virtual(sleeper, "join", "()V"));
    CHECK(vm.Observed("LThreadSleeper;") == 1);
    CHECK(advances.load() == 1);
    CHECK(vm.clock_millis.load() == 100);
}
