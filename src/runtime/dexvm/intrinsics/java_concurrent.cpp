#include "catalog.h"
#include "shared.h"

#include <array>
#include <string>
#include <vector>

#include "ogplay/runtime/dexvm/intrinsic_builder.h"
#include "ogplay/runtime/dexvm/vm_threads.h"

namespace ogplay::runtime::dexvm::intrinsics {
namespace {

[[nodiscard]] VmCallOutcome Dvm87ConcurrentInvoke(
    IntrinsicContext& context, const VmObjectRef receiver,
    const std::string_view name, const std::string_view descriptor,
    std::vector<VmValue> arguments = {}) {
    if (!receiver.IsValid())
        throw VmJavaThrow{"Ljava/lang/NullPointerException;", "receiver == null"};
    auto& linker = context.vm.Linker();
    const auto java_class = context.vm.Model().ObjectClass(receiver);
    const auto index = linker.FindVtableIndex(
        java_class, std::string(name), std::string(descriptor));
    if (!index.has_value())
        throw VmJavaThrow{"Ljava/lang/AbstractMethodError;",
                          std::string(name) + std::string(descriptor)};
    arguments.insert(arguments.begin(), VmValue::Ref(receiver));
    const auto outcome = context.vm.Call(
        linker.Class(java_class).vtable[*index], arguments);
    return outcome;
}

[[nodiscard]] VmCallOutcome Dvm87DirectCall(
    Interpreter& vm, const VmMethodId method,
    const std::span<const VmValue> arguments) {
    return vm.Call(method, arguments);
}

[[nodiscard]] VmObjectRef Dvm87StartWorker(IntrinsicContext& context,
                                           const VmObjectRef runnable) {
    if (!runnable.IsValid())
        throw VmJavaThrow{"Ljava/lang/NullPointerException;", "command == null"};
    auto& linker = context.vm.Linker();
    const auto thread_class = linker.ResolveDescriptor("Ljava/lang/Thread;");
    const auto constructor = linker.FindDirectMethod(
        thread_class, "<init>", "(Ljava/lang/Runnable;)V");
    const auto start_index = linker.FindVtableIndex(thread_class, "start", "()V");
    if (!constructor.has_value() || !start_index.has_value())
        throw DexVmError(DexVmErrorReason::internal_invariant,
                         "Thread constructor/start missing");
    const auto thread = context.vm.NewIntrinsicInstance("Ljava/lang/Thread;");
    const std::array constructor_args{VmValue::Ref(thread), VmValue::Ref(runnable)};
    const auto constructed = Dvm87DirectCall(
        context.vm, *constructor, constructor_args);
    if (constructed.exception.IsValid()) {
        context.vm.SetPendingException(constructed.exception);
        return VmObjectRef{0};
    }
    const std::array start_args{VmValue::Ref(thread)};
    const auto started = Dvm87DirectCall(
        context.vm, linker.Class(thread_class).vtable[*start_index], start_args);
    if (started.exception.IsValid()) {
        context.vm.SetPendingException(started.exception);
        return VmObjectRef{0};
    }
    return thread;
}

IntrinsicClassDecl Dvm87DeclareInterface(
    std::string descriptor, std::vector<std::string> parents,
    std::vector<std::pair<std::string, std::string>> methods) {
    auto builder = IntrinsicClassBuilder::Interface(
        std::move(descriptor), std::move(parents));
    for (auto& [name, signature] : methods)
        builder.UnimplementedVirtual(std::move(name), std::move(signature),
                                     0x0001U | 0x0400U);
    return std::move(builder).Build();
}

struct Dvm87FutureFields final {
    IntrinsicFieldHandle callable;
    IntrinsicFieldHandle runnable;
    IntrinsicFieldHandle result;
    IntrinsicFieldHandle exception;
    IntrinsicFieldHandle runner;
    IntrinsicFieldHandle state;
};

IntrinsicClassDecl Dvm87DeclareFutureTask() {
    auto builder = IntrinsicClassBuilder::Class(
        "Ljava/util/concurrent/FutureTask;", "Ljava/lang/Object;",
        {"Ljava/util/concurrent/RunnableFuture;"});
    const Dvm87FutureFields fields{
        builder.BoundInstanceField("callable", "Ljava/util/concurrent/Callable;"),
        builder.BoundInstanceField("runnable", "Ljava/lang/Runnable;"),
        builder.BoundInstanceField("result", "Ljava/lang/Object;"),
        builder.BoundInstanceField("exception", "Ljava/lang/Throwable;"),
        builder.BoundInstanceField("runner", "Ljava/lang/Thread;"),
        builder.BoundInstanceField("state", "I")};
    builder.Constructor("(Ljava/util/concurrent/Callable;)V",
        [fields](IntrinsicContext& context) {
            IntrinsicCall call(context);
            call.SetRef(fields.callable, call.NonNullRef(0, "callable"));
            call.SetInt(fields.state, 0);
            return VmValue::Void();
        });
    builder.Constructor("(Ljava/lang/Runnable;Ljava/lang/Object;)V",
        [fields](IntrinsicContext& context) {
            IntrinsicCall call(context);
            call.SetRef(fields.runnable, call.NonNullRef(0, "runnable"));
            call.SetRef(fields.result, call.Ref(1));
            call.SetInt(fields.state, 0);
            return VmValue::Void();
        });
    builder.FinalMethod("run", "()V", [fields](IntrinsicContext& context) {
        IntrinsicCall call(context);
        if (call.GetInt(fields.state) != 0) return VmValue::Void();
        call.SetInt(fields.state, 1);
        // Publish the actual worker Thread identity before invoking user code.
        const auto thread_class = context.vm.Linker().ResolveDescriptor(
            "Ljava/lang/Thread;");
        const auto current_method = context.vm.Linker().FindDirectMethod(
            thread_class, "currentThread", "()Ljava/lang/Thread;");
        const auto current_outcome = context.vm.Call(*current_method, {});
        if (!current_outcome.exception.IsValid())
            call.SetRef(fields.runner, current_outcome.value.ref);
        const auto callable = call.GetRef(fields.callable);
        const auto runnable = call.GetRef(fields.runnable);
        VmCallOutcome outcome{VmValue::Void(), VmObjectRef{0}, DexClassId{0},
                              {}, {}};
        if (callable.IsValid()) {
            outcome = Dvm87ConcurrentInvoke(
                context, callable, "call", "()Ljava/lang/Object;");
        } else {
            outcome = Dvm87ConcurrentInvoke(context, runnable, "run", "()V");
        }
        if (call.GetInt(fields.state) != 3) {
            if (outcome.exception.IsValid())
                call.SetRef(fields.exception, outcome.exception);
            else if (callable.IsValid())
                call.SetRef(fields.result, outcome.value.ref);
            call.SetInt(fields.state, 2);
        }
        return VmValue::Void();
    });
    builder.FinalMethod("cancel", "(Z)Z", [fields](IntrinsicContext& context) {
        IntrinsicCall call(context);
        if (call.GetInt(fields.state) >= 2) return VmValue::Int(0);
        call.SetInt(fields.state, 3);
        if (call.Int(0) != 0) {
            const auto runner = call.GetRef(fields.runner);
            if (runner.IsValid()) context.vm.Threads().Interrupt(runner);
        }
        return VmValue::Int(1);
    });
    builder.FinalMethod("isCancelled", "()Z", [fields](IntrinsicContext& context) {
        return VmValue::Int(IntrinsicCall(context).GetInt(fields.state) == 3 ? 1 : 0);
    });
    builder.FinalMethod("isDone", "()Z", [fields](IntrinsicContext& context) {
        return VmValue::Int(IntrinsicCall(context).GetInt(fields.state) >= 2 ? 1 : 0);
    });
    builder.FinalMethod("get", "()Ljava/lang/Object;", [fields](IntrinsicContext& context) {
        IntrinsicCall call(context);
        while (call.GetInt(fields.state) < 2) context.vm.Threads().Yield();
        if (call.GetInt(fields.state) == 3)
            throw VmJavaThrow{"Ljava/util/concurrent/CancellationException;", {}};
        const auto exception = call.GetRef(fields.exception);
        if (exception.IsValid())
            throw VmJavaThrow{"Ljava/util/concurrent/ExecutionException;",
                              "task raised an exception"};
        return VmValue::Ref(call.GetRef(fields.result));
    });
    return std::move(builder).Build();
}

struct Dvm87ExecutorFields final {
    IntrinsicFieldHandle shutdown;
    IntrinsicFieldHandle worker;
};

IntrinsicClassDecl Dvm87DeclareSingleThreadExecutor() {
    auto builder = IntrinsicClassBuilder::Class(
        "Ljava/util/concurrent/Executors$SingleThreadExecutor;",
        "Ljava/lang/Object;", {"Ljava/util/concurrent/ExecutorService;"});
    const Dvm87ExecutorFields fields{
        builder.BoundInstanceField("shutdown", "Z"),
        builder.BoundInstanceField("worker", "Ljava/lang/Thread;")};
    builder.Constructor("()V", [fields](IntrinsicContext& context) {
        IntrinsicCall(context).SetInt(fields.shutdown, 0);
        return VmValue::Void();
    });
    builder.FinalMethod("execute", "(Ljava/lang/Runnable;)V",
        [fields](IntrinsicContext& context) {
            if (IntrinsicCall(context).GetInt(fields.shutdown) != 0)
                throw VmJavaThrow{"Ljava/util/concurrent/RejectedExecutionException;",
                                  "executor is shut down"};
            IntrinsicCall call(context);
            const auto previous = call.GetRef(fields.worker);
            if (previous.IsValid()) {
                const auto joined = Dvm87ConcurrentInvoke(
                    context, previous, "join", "()V");
                if (joined.exception.IsValid()) {
                    context.vm.SetPendingException(joined.exception);
                    return VmValue::Void();
                }
            }
            call.SetRef(fields.worker,
                        Dvm87StartWorker(context, context.arguments[0].ref));
            return VmValue::Void();
        });
    builder.FinalMethod("submit",
        "(Ljava/util/concurrent/Callable;)Ljava/util/concurrent/Future;",
        [fields](IntrinsicContext& context) {
            if (IntrinsicCall(context).GetInt(fields.shutdown) != 0)
                throw VmJavaThrow{"Ljava/util/concurrent/RejectedExecutionException;",
                                  "executor is shut down"};
            const auto future = context.vm.NewIntrinsicInstance(
                "Ljava/util/concurrent/FutureTask;");
            const auto owner = context.vm.Linker().ResolveDescriptor(
                "Ljava/util/concurrent/FutureTask;");
            const auto constructor = context.vm.Linker().FindDirectMethod(
                owner, "<init>", "(Ljava/util/concurrent/Callable;)V");
            const std::array args{VmValue::Ref(future), context.arguments[0]};
            const auto outcome = context.vm.Call(*constructor, args);
            if (outcome.exception.IsValid()) {
                context.vm.SetPendingException(outcome.exception);
                return VmValue::Ref(VmObjectRef{});
            }
            IntrinsicCall call(context);
            const auto previous = call.GetRef(fields.worker);
            if (previous.IsValid()) {
                const auto joined = Dvm87ConcurrentInvoke(
                    context, previous, "join", "()V");
                if (joined.exception.IsValid()) {
                    context.vm.SetPendingException(joined.exception);
                    return VmValue::Ref(VmObjectRef{0});
                }
            }
            call.SetRef(fields.worker, Dvm87StartWorker(context, future));
            return VmValue::Ref(future);
        });
    builder.FinalMethod("shutdown", "()V", [fields](IntrinsicContext& context) {
        IntrinsicCall(context).SetInt(fields.shutdown, 1);
        return VmValue::Void();
    });
    builder.FinalMethod("isShutdown", "()Z", [fields](IntrinsicContext& context) {
        return VmValue::Int(IntrinsicCall(context).GetInt(fields.shutdown));
    });
    builder.FinalMethod("isTerminated", "()Z", [fields](IntrinsicContext& context) {
        return VmValue::Int(IntrinsicCall(context).GetInt(fields.shutdown) != 0 &&
                            context.vm.Threads().LiveCount() == 0 ? 1 : 0);
    });
    return std::move(builder).Build();
}

IntrinsicClassDecl Dvm87DeclareExecutors() {
    auto builder = IntrinsicClassBuilder::Class("Ljava/util/concurrent/Executors;");
    builder.StaticMethod("newSingleThreadExecutor",
        "()Ljava/util/concurrent/ExecutorService;",
        [](IntrinsicContext& context) {
            const auto executor = context.vm.NewIntrinsicInstance(
                "Ljava/util/concurrent/Executors$SingleThreadExecutor;");
            const auto owner = context.vm.Linker().ResolveDescriptor(
                "Ljava/util/concurrent/Executors$SingleThreadExecutor;");
            const auto constructor = context.vm.Linker().FindDirectMethod(
                owner, "<init>", "()V");
            const std::array args{VmValue::Ref(executor)};
            const auto outcome = context.vm.Call(*constructor, args);
            if (outcome.exception.IsValid()) context.vm.SetPendingException(outcome.exception);
            return VmValue::Ref(executor);
        });
    return std::move(builder).Build();
}

template <typename T>
IntrinsicClassDecl Dvm87DeclareAtomicIntegral(const std::string& descriptor,
                                              const std::string& value_descriptor) {
    auto builder = IntrinsicClassBuilder::Class(descriptor, "Ljava/lang/Number;",
                                                {"Ljava/io/Serializable;"});
    const auto value = value_descriptor == "J"
        ? builder.BoundInstanceField("value", "J")
        : builder.BoundInstanceField("value", "I");
    const auto get = [value](IntrinsicCall& call) -> std::int64_t {
        if constexpr (sizeof(T) == 8) {
            return call.GetLong(value);
        } else {
            return call.GetInt(value);
        }
    };
    const auto set = [value](IntrinsicCall& call, const std::int64_t next) {
        if constexpr (sizeof(T) == 8) call.SetLong(value, next);
        else call.SetInt(value, static_cast<std::int32_t>(next));
    };
    builder.Constructor("()V", [set](IntrinsicContext& context) {
        IntrinsicCall call(context); set(call, 0); return VmValue::Void();
    });
    builder.Constructor("(" + value_descriptor + ")V", [set](IntrinsicContext& context) {
        IntrinsicCall call(context);
        if constexpr (sizeof(T) == 8) set(call, call.Long(0));
        else set(call, call.Int(0));
        return VmValue::Void();
    });
    builder.FinalMethod("get", "()" + value_descriptor,
        [get](IntrinsicContext& context) {
            IntrinsicCall call(context);
            if constexpr (sizeof(T) == 8) {
                return VmValue::Long(get(call));
            } else {
                return VmValue::Int(static_cast<std::int32_t>(get(call)));
            }
        });
    builder.FinalMethod("set", "(" + value_descriptor + ")V",
        [set](IntrinsicContext& context) {
            IntrinsicCall call(context);
            if constexpr (sizeof(T) == 8) set(call, call.Long(0));
            else set(call, call.Int(0));
            return VmValue::Void();
        });
    builder.FinalMethod("getAndSet", "(" + value_descriptor + ")" + value_descriptor,
        [get, set](IntrinsicContext& context) {
            IntrinsicCall call(context); const auto old = get(call);
            if constexpr (sizeof(T) == 8) set(call, call.Long(0));
            else set(call, call.Int(0));
            if constexpr (sizeof(T) == 8) {
                return VmValue::Long(old);
            } else {
                return VmValue::Int(static_cast<std::int32_t>(old));
            }
        });
    builder.FinalMethod("compareAndSet",
        "(" + value_descriptor + value_descriptor + ")Z",
        [get, set](IntrinsicContext& context) {
            IntrinsicCall call(context);
            const auto expected = sizeof(T) == 8 ? call.Long(0) : call.Int(0);
            const auto next = sizeof(T) == 8 ? call.Long(1) : call.Int(1);
            if (get(call) != expected) return VmValue::Int(0);
            set(call, next); return VmValue::Int(1);
        });
    builder.FinalMethod("getAndIncrement", "()" + value_descriptor,
        [get, set](IntrinsicContext& context) {
            IntrinsicCall call(context); const auto old = get(call); set(call, old + 1);
            if constexpr (sizeof(T) == 8) {
                return VmValue::Long(old);
            } else {
                return VmValue::Int(static_cast<std::int32_t>(old));
            }
        });
    builder.FinalMethod("incrementAndGet", "()" + value_descriptor,
        [get, set](IntrinsicContext& context) {
            IntrinsicCall call(context); const auto next = get(call) + 1; set(call, next);
            if constexpr (sizeof(T) == 8) {
                return VmValue::Long(next);
            } else {
                return VmValue::Int(static_cast<std::int32_t>(next));
            }
        });
    return std::move(builder).Build();
}

IntrinsicClassDecl Dvm87DeclareAtomicBoolean() {
    auto builder = IntrinsicClassBuilder::Class(
        "Ljava/util/concurrent/atomic/AtomicBoolean;", "Ljava/lang/Object;",
        {"Ljava/io/Serializable;"});
    const auto value = builder.BoundInstanceField("value", "Z");
    builder.Constructor("()V", [value](IntrinsicContext& context) {
        IntrinsicCall(context).SetInt(value, 0); return VmValue::Void();
    });
    builder.Constructor("(Z)V", [value](IntrinsicContext& context) {
        IntrinsicCall call(context); call.SetInt(value, call.Int(0)); return VmValue::Void();
    });
    builder.FinalMethod("get", "()Z", [value](IntrinsicContext& context) {
        return VmValue::Int(IntrinsicCall(context).GetInt(value));
    });
    builder.FinalMethod("set", "(Z)V", [value](IntrinsicContext& context) {
        IntrinsicCall call(context); call.SetInt(value, call.Int(0)); return VmValue::Void();
    });
    builder.FinalMethod("compareAndSet", "(ZZ)Z", [value](IntrinsicContext& context) {
        IntrinsicCall call(context);
        if (call.GetInt(value) != call.Int(0)) return VmValue::Int(0);
        call.SetInt(value, call.Int(1)); return VmValue::Int(1);
    });
    builder.FinalMethod("getAndSet", "(Z)Z", [value](IntrinsicContext& context) {
        IntrinsicCall call(context); const auto old = call.GetInt(value);
        call.SetInt(value, call.Int(0)); return VmValue::Int(old);
    });
    return std::move(builder).Build();
}

IntrinsicClassDecl Dvm87DeclareAtomicReference() {
    auto builder = IntrinsicClassBuilder::Class(
        "Ljava/util/concurrent/atomic/AtomicReference;", "Ljava/lang/Object;",
        {"Ljava/io/Serializable;"});
    const auto value = builder.BoundInstanceField("value", "Ljava/lang/Object;");
    builder.Constructor("()V", [value](IntrinsicContext& context) {
        IntrinsicCall(context).SetRef(value, VmObjectRef{0});
        return VmValue::Void();
    });
    builder.Constructor("(Ljava/lang/Object;)V", [value](IntrinsicContext& context) {
        IntrinsicCall call(context); call.SetRef(value, call.Ref(0)); return VmValue::Void();
    });
    builder.FinalMethod("get", "()Ljava/lang/Object;", [value](IntrinsicContext& context) {
        return VmValue::Ref(IntrinsicCall(context).GetRef(value));
    });
    builder.FinalMethod("set", "(Ljava/lang/Object;)V", [value](IntrinsicContext& context) {
        IntrinsicCall call(context); call.SetRef(value, call.Ref(0)); return VmValue::Void();
    });
    builder.FinalMethod("compareAndSet", "(Ljava/lang/Object;Ljava/lang/Object;)Z",
        [value](IntrinsicContext& context) {
            IntrinsicCall call(context);
            if (call.GetRef(value) != call.Ref(0)) return VmValue::Int(0);
            call.SetRef(value, call.Ref(1)); return VmValue::Int(1);
        });
    builder.FinalMethod("getAndSet", "(Ljava/lang/Object;)Ljava/lang/Object;",
        [value](IntrinsicContext& context) {
            IntrinsicCall call(context); const auto old = call.GetRef(value);
            call.SetRef(value, call.Ref(0)); return VmValue::Ref(old);
        });
    return std::move(builder).Build();
}

IntrinsicClassDecl Dvm87DeclareConcurrentException(
    std::string descriptor, std::string superclass) {
    auto builder = IntrinsicClassBuilder::Class(std::move(descriptor),
                                                std::move(superclass));
    builder.Constructor("()V", [](IntrinsicContext&) { return VmValue::Void(); });
    builder.Constructor("(Ljava/lang/String;)V", [](IntrinsicContext& context) {
        context.vm.SetThrowableMessage(context.receiver, context.arguments[0].ref);
        return VmValue::Void();
    });
    return std::move(builder).Build();
}

}  // namespace

void AppendJavaConcurrent(std::vector<IntrinsicClassDecl>& catalog) {
    catalog.push_back(Dvm87DeclareInterface(
        "Ljava/util/concurrent/Callable;", {},
        {{"call", "()Ljava/lang/Object;"}}));
    catalog.push_back(Dvm87DeclareInterface(
        "Ljava/util/concurrent/Future;", {},
        {{"cancel", "(Z)Z"}, {"isCancelled", "()Z"},
         {"isDone", "()Z"}, {"get", "()Ljava/lang/Object;"}}));
    catalog.push_back(Dvm87DeclareInterface(
        "Ljava/util/concurrent/RunnableFuture;",
        {"Ljava/lang/Runnable;", "Ljava/util/concurrent/Future;"}, {}));
    catalog.push_back(Dvm87DeclareInterface(
        "Ljava/util/concurrent/Executor;", {},
        {{"execute", "(Ljava/lang/Runnable;)V"}}));
    catalog.push_back(Dvm87DeclareInterface(
        "Ljava/util/concurrent/ExecutorService;",
        {"Ljava/util/concurrent/Executor;"},
        {{"shutdown", "()V"}, {"isShutdown", "()Z"},
         {"isTerminated", "()Z"},
         {"submit", "(Ljava/util/concurrent/Callable;)Ljava/util/concurrent/Future;"}}));
    catalog.push_back(Dvm87DeclareFutureTask());
    catalog.push_back(Dvm87DeclareSingleThreadExecutor());
    catalog.push_back(Dvm87DeclareExecutors());
    catalog.push_back(Dvm87DeclareAtomicIntegral<std::int32_t>(
        "Ljava/util/concurrent/atomic/AtomicInteger;", "I"));
    catalog.push_back(Dvm87DeclareAtomicIntegral<std::int64_t>(
        "Ljava/util/concurrent/atomic/AtomicLong;", "J"));
    catalog.push_back(Dvm87DeclareAtomicBoolean());
    catalog.push_back(Dvm87DeclareAtomicReference());
    catalog.push_back(Dvm87DeclareConcurrentException(
        "Ljava/util/concurrent/CancellationException;",
        "Ljava/lang/IllegalStateException;"));
    catalog.push_back(Dvm87DeclareConcurrentException(
        "Ljava/util/concurrent/ExecutionException;", "Ljava/lang/Exception;"));
    catalog.push_back(Dvm87DeclareConcurrentException(
        "Ljava/util/concurrent/RejectedExecutionException;",
        "Ljava/lang/RuntimeException;"));
}

}  // namespace ogplay::runtime::dexvm::intrinsics
