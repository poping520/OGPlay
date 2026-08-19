// java.lang.Thread core facade for the pinned Android 4.4.4 libdvm class.
// Java-visible fields and validation stay here; lifecycle, parking,
// interrupt state and execution-context identity stay in VmThreadRuntime.

#include "catalog.h"

#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

#include "ogplay/runtime/dexvm/intrinsic_builder.h"
#include "ogplay/runtime/dexvm/vm_monitors.h"
#include "ogplay/runtime/dexvm/vm_threads.h"

namespace ogplay::runtime::dexvm::intrinsics {
    namespace {
        struct ThreadFields final {
            IntrinsicFieldHandle target;
            IntrinsicFieldHandle name;
            IntrinsicFieldHandle priority;
            IntrinsicFieldHandle daemon;
            IntrinsicFieldHandle stack_size;
            IntrinsicFieldHandle id;
            IntrinsicFieldHandle has_been_started;
        };

        void InitializeFields(const IntrinsicCall& call,
                              const ThreadFields& fields,
                              const VmObjectRef object,
                              const VmObjectRef target, const VmObjectRef name,
                              const std::int64_t id, const std::int32_t priority) {
            call.SetRef(fields.target, object, target);
            call.SetRef(fields.name, object, name);
            call.SetInt(fields.priority, object, priority);
            call.SetInt(fields.daemon, object, 0);
            call.SetLong(fields.stack_size, object, 0);
            call.SetLong(fields.id, object, id);
            call.SetInt(fields.has_been_started, object, 0);
        }

        [[nodiscard]] VmObjectRef EnsureCurrentThread(
            const IntrinsicCall& call, const ThreadFields& fields) {
            auto& vm = call.Vm();
            auto& runtime = vm.Threads();
            if (const auto current = runtime.CurrentThreadObject(); current.IsValid()) {
                return current;
            }
            if (vm.CurrentContextToken() != 1U) {
                throw DexVmError(DexVmErrorReason::internal_invariant, "child execution context has no Thread object");
            }
            const auto root = vm.NewIntrinsicInstance("Ljava/lang/Thread;");
            // Publish the object before allocating its name: NewStringUtf8 may cross
            // a GC safe allocation point, so the fresh root must already be strong.
            runtime.SetRootThreadObject(root);
            InitializeFields(call, fields, root, VmObjectRef{},
                             vm.NewStringUtf8("main"), 1, 5);
            return root;
        }

        [[nodiscard]] VmValue Construct(IntrinsicContext& context,
                                        const ThreadFields& fields,
                                        const VmObjectRef target,
                                        const VmObjectRef explicit_name,
                                        const bool has_explicit_name) {
            if (has_explicit_name && !explicit_name.IsValid()) {
                throw VmJavaThrow{
                    "Ljava/lang/NullPointerException;", "threadName == null"
                };
            }
            const IntrinsicCall call(context);
            auto& runtime = context.vm.Threads();
            const auto current = EnsureCurrentThread(call, fields);
            const auto priority = call.GetInt(fields.priority, current);
            const auto id = runtime.AllocateThreadId();
            const auto name = has_explicit_name
                                  ? explicit_name
                                  : context.vm.NewStringUtf8("Thread-" + std::to_string(id));
            InitializeFields(call, fields, context.receiver, target, name,
                             static_cast<std::int64_t>(id), priority);
            return VmValue::Void();
        }

        void PropagateOutcome(Interpreter& vm, const VmCallOutcome& outcome) {
            if (!outcome.exception.IsValid()) return;
            throw VmJavaThrow{
                vm.Linker().Class(outcome.exception_class).descriptor,
                outcome.exception_message
            };
        }

        [[nodiscard]] std::int64_t RoundedMillis(const std::int64_t millis,
                                                 const std::int32_t nanos) {
            if (nanos == 0) return millis;
            if (millis == std::numeric_limits<std::int64_t>::max()) return millis;
            return millis + 1;
        }

        void ValidateTimeout(const std::int64_t millis, const std::int32_t nanos) {
            if (millis < 0 || nanos < 0 || nanos >= 1'000'000) {
                throw VmJavaThrow{
                    "Ljava/lang/IllegalArgumentException;", "timeout arguments out of range"
                };
            }
        }
    } // namespace

    IntrinsicClassDecl Declare_java_lang_Thread() {
        auto builder = IntrinsicClassBuilder::Class(
            "Ljava/lang/Thread;",
            "Ljava/lang/Object;",
            {"Ljava/lang/Runnable;"}
        );
        const ThreadFields fields{
            builder.BoundInstanceField("target", "Ljava/lang/Runnable;"),
            builder.BoundInstanceField("name", "Ljava/lang/String;"),
            builder.BoundInstanceField("priority", "I"),
            builder.BoundInstanceField("daemon", "Z"),
            builder.BoundInstanceField("stackSize", "J"),
            builder.BoundInstanceField("id", "J"),
            builder.BoundInstanceField("hasBeenStarted", "Z"),
        };
        builder.ConstantInt("MIN_PRIORITY", "I", 1)
                .ConstantInt("NORM_PRIORITY", "I", 5)
                .ConstantInt("MAX_PRIORITY", "I", 10);

        builder.Constructor("()V", [fields](IntrinsicContext& context) {
            return Construct(context, fields, VmObjectRef{}, VmObjectRef{}, false);
        });
        builder.Constructor("(Ljava/lang/Runnable;)V", [fields](IntrinsicContext& context) {
            IntrinsicCall call(context);
            return Construct(context, fields, call.Ref(0), VmObjectRef{}, false);
        });
        builder.Constructor("(Ljava/lang/String;)V", [fields](IntrinsicContext& context) {
            IntrinsicCall call(context);
            return Construct(context, fields, VmObjectRef{}, call.Ref(0), true);
        });
        builder.Constructor("(Ljava/lang/Runnable;Ljava/lang/String;)V", [fields](IntrinsicContext& context) {
            IntrinsicCall call(context);
            return Construct(context, fields, call.Ref(0), call.Ref(1), true);
        });

        // AOSP Thread.run: target dispatch belongs here. VmThreadRuntime always
        // dispatches virtual this.run(), so a Thread subclass override wins.
        builder.VirtualMethod("run", "()V", [fields](IntrinsicContext& context) {
            IntrinsicCall call(context);
            const auto target = call.GetRef(fields.target);
            if (!target.IsValid()) return VmValue::Void();
            auto& linker = context.vm.Linker();
            const auto target_class = context.vm.Model().ObjectClass(target);
            const auto index = linker.FindVtableIndex(target_class, "run", "()V");
            if (!index.has_value()) {
                throw VmJavaThrow{"Ljava/lang/AbstractMethodError;", "Runnable target has no run()"};
            }
            const auto outcome = context.vm.Call(linker.Class(target_class).vtable[*index],
                                                 std::vector<VmValue>{VmValue::Ref(target)});
            PropagateOutcome(context.vm, outcome);
            return VmValue::Void();
        });

        builder.VirtualMethod("start", "()V", [fields](IntrinsicContext& context) {
            IntrinsicCall call(context);
            if (call.GetInt(fields.has_been_started) != 0) {
                throw VmJavaThrow{
                    "Ljava/lang/IllegalThreadStateException;", "Thread already started"
                };
            }
            call.SetInt(fields.has_been_started, 1);
            const auto name = call.GetRef(fields.name);
            context.vm.Threads().Start(
                context.receiver, context.vm.StringUtf8(name),
                static_cast<std::uint64_t>(call.GetLong(fields.id))
            );
            return VmValue::Void();
        });

        builder.StaticMethod("currentThread", "()Ljava/lang/Thread;", [fields](IntrinsicContext& context) {
            IntrinsicCall call(context);
            return VmValue::Ref(EnsureCurrentThread(call, fields));
        });
        builder.VirtualMethod("getId", "()J", [fields](IntrinsicContext& context) {
            return VmValue::Long(IntrinsicCall(context).GetLong(fields.id));
        });
        builder.FinalMethod("getName", "()Ljava/lang/String;", [fields](IntrinsicContext& context) {
            return VmValue::Ref(IntrinsicCall(context).GetRef(fields.name));
        });
        builder.FinalMethod("setName", "(Ljava/lang/String;)V", [fields](IntrinsicContext& context) {
            IntrinsicCall call(context);
            const auto name = call.NonNullRef(0, "threadName");
            call.SetRef(fields.name, name);
            context.vm.Threads().Rename(context.receiver, context.vm.StringUtf8(name));
            return VmValue::Void();
        });
        builder.FinalMethod("isAlive", "()Z", [](IntrinsicContext& context) {
            return VmValue::Int(context.vm.Threads().IsAlive(context.receiver) ? 1 : 0);
        });
        builder.FinalMethod("getPriority", "()I", [fields](IntrinsicContext& context) {
            return VmValue::Int(IntrinsicCall(context).GetInt(fields.priority));
        });
        builder.FinalMethod("setPriority", "(I)V", [fields](IntrinsicContext& context) {
            IntrinsicCall call(context);
            const auto priority = call.Int(0);
            if (priority < 1 || priority > 10) {
                throw VmJavaThrow{
                    "Ljava/lang/IllegalArgumentException;", "Priority out of range: " + std::to_string(priority)
                };
            }
            // Guest-visible only: host scheduler priority is
            // deliberately not modeled yet.
            call.SetInt(fields.priority, priority);
            return VmValue::Void();
        });

        builder.VirtualMethod("interrupt", "()V", [](IntrinsicContext& context) {
            context.vm.Threads().Interrupt(context.receiver);
            return VmValue::Void();
        });
        builder.VirtualMethod("isInterrupted", "()Z", [](IntrinsicContext& context) {
            return VmValue::Int(context.vm.Threads().IsInterrupted(context.receiver) ? 1 : 0);
        });
        builder.StaticMethod("interrupted", "()Z", [](IntrinsicContext& context) {
            return VmValue::Int(context.vm.Threads().ClearCurrentInterrupt() ? 1 : 0);
        });

        builder.FinalMethod("join", "()V", [](IntrinsicContext& context) {
            context.vm.Threads().Join(context.receiver);
            return VmValue::Void();
        });
        builder.FinalMethod("join", "(J)V", [](IntrinsicContext& context) {
            IntrinsicCall call(context);
            const auto millis = call.Long(0);
            ValidateTimeout(millis, 0);
            if (millis == 0) {
                context.vm.Threads().Join(context.receiver);
            } else {
                context.vm.Threads().Join(context.receiver, millis);
            }
            return VmValue::Void();
        });
        builder.FinalMethod("join", "(JI)V", [](IntrinsicContext& context) {
            IntrinsicCall call(context);
            const auto millis = call.Long(0);
            const auto nanos = call.Int(1);
            ValidateTimeout(millis, nanos);
            if (millis == 0 && nanos == 0) {
                context.vm.Threads().Join(context.receiver);
            } else {
                context.vm.Threads().Join(context.receiver, RoundedMillis(millis, nanos));
            }
            return VmValue::Void();
        });

        builder.StaticMethod("sleep", "(J)V", [](IntrinsicContext& context) {
            IntrinsicCall call(context);
            const auto millis = call.Long(0);
            ValidateTimeout(millis, 0);
            context.vm.Threads().Sleep(millis);
            return VmValue::Void();
        });
        builder.StaticMethod("sleep", "(JI)V", [](IntrinsicContext& context) {
            IntrinsicCall call(context);
            const auto millis = call.Long(0);
            const auto nanos = call.Int(1);
            ValidateTimeout(millis, nanos);
            context.vm.Threads().Sleep(RoundedMillis(millis, nanos));
            return VmValue::Void();
        });
        builder.StaticMethod("yield", "()V", [](IntrinsicContext& context) {
            context.vm.Threads().Yield();
            return VmValue::Void();
        });
        builder.StaticMethod("holdsLock", "(Ljava/lang/Object;)Z", [](IntrinsicContext& context) {
            IntrinsicCall call(context);
            const auto object = call.NonNullRef(0, "object");
            return VmValue::Int(context.vm.Monitors().IsOwner(object, context.vm.CurrentContextToken()) ? 1 : 0);
        });

        builder.FinalMethod("isDaemon", "()Z", [fields](IntrinsicContext& context) {
            return VmValue::Int(IntrinsicCall(context).GetInt(fields.daemon));
        });
        builder.FinalMethod("setDaemon", "(Z)V", [fields](IntrinsicContext& context) {
            IntrinsicCall call(context);
            if (call.GetInt(fields.has_been_started) != 0) {
                throw VmJavaThrow{"Ljava/lang/IllegalThreadStateException;", "Thread already started"};
            }
            // Bounded semantics: the flag is exposed, but session termination is
            // not daemon-driven in OGPlay.
            call.SetInt(fields.daemon, call.Int(0) != 0 ? 1 : 0);
            return VmValue::Void();
        });

        builder.UnimplementedFinal("stop", "()V");
        builder.UnimplementedFinal("suspend", "()V");
        builder.UnimplementedFinal("resume", "()V");
        builder.UnimplementedFinal("destroy", "()V");
        return std::move(builder).Build();
    }
} // namespace ogplay::runtime::dexvm::intrinsics
