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
        [[nodiscard]] const LinkedField& ThreadField(
            Interpreter& vm, const VmObjectRef object, const std::string_view name,
            const std::string_view descriptor) {
            const auto java_class = vm.Model().ObjectClass(object);
            const auto field = vm.Linker().FindFieldRecursive(java_class, std::string(name), std::string(descriptor));
            if (!field.has_value()) {
                throw DexVmError(DexVmErrorReason::internal_invariant, "Thread field is missing: " + std::string(name));
            }
            return vm.Linker().Field(*field);
        }

        [[nodiscard]] std::span<Slot> CheckedSlots(Interpreter& vm,
                                                   const VmObjectRef object,
                                                   const LinkedField& field,
                                                   const bool wide = false) {
            auto slots = vm.Model().InstanceSlots(object);
            const auto needed = static_cast<std::size_t>(field.slot) + (wide ? 2U : 1U);
            if (needed > slots.size()) {
                throw DexVmError(DexVmErrorReason::internal_invariant,
                                 "Thread field slot is out of range: " + field.name);
            }
            return slots;
        }

        void SetRef(Interpreter& vm, const VmObjectRef object,
                    const std::string_view name, const std::string_view descriptor,
                    const VmObjectRef value) {
            const auto& field = ThreadField(vm, object, name, descriptor);
            auto slots = CheckedSlots(vm, object, field);
            slots[field.slot] = {value.Value(), SlotTag::ref};
        }

        [[nodiscard]] VmObjectRef GetRef(Interpreter& vm, const VmObjectRef object,
                                         const std::string_view name,
                                         const std::string_view descriptor) {
            const auto& field = ThreadField(vm, object, name, descriptor);
            const auto slots = CheckedSlots(vm, object, field);
            return VmObjectRef(slots[field.slot].bits);
        }

        void SetInt(Interpreter& vm, const VmObjectRef object,
                    const std::string_view name, const std::string_view descriptor,
                    const std::int32_t value) {
            const auto& field = ThreadField(vm, object, name, descriptor);
            auto slots = CheckedSlots(vm, object, field);
            slots[field.slot] = {static_cast<std::uint32_t>(value), SlotTag::cat1};
        }

        [[nodiscard]] std::int32_t GetInt(Interpreter& vm,
                                          const VmObjectRef object,
                                          const std::string_view name,
                                          const std::string_view descriptor) {
            const auto& field = ThreadField(vm, object, name, descriptor);
            const auto slots = CheckedSlots(vm, object, field);
            return static_cast<std::int32_t>(slots[field.slot].bits);
        }

        void SetLong(Interpreter& vm, const VmObjectRef object,
                     const std::string_view name, const std::int64_t value) {
            const auto& field = ThreadField(vm, object, name, "J");
            auto slots = CheckedSlots(vm, object, field, true);
            const auto bits = static_cast<std::uint64_t>(value);
            slots[field.slot] = {static_cast<std::uint32_t>(bits), SlotTag::wide_lo};
            slots[field.slot + 1U] = {
                static_cast<std::uint32_t>(bits >> 32U),
                SlotTag::wide_hi
            };
        }

        [[nodiscard]] std::int64_t GetLong(Interpreter& vm,
                                           const VmObjectRef object,
                                           const std::string_view name) {
            const auto& field = ThreadField(vm, object, name, "J");
            const auto slots = CheckedSlots(vm, object, field, true);
            const auto bits = static_cast<std::uint64_t>(slots[field.slot].bits) |
                              (static_cast<std::uint64_t>(slots[field.slot + 1U].bits) << 32U);
            return static_cast<std::int64_t>(bits);
        }

        void InitializeFields(Interpreter& vm, const VmObjectRef object,
                              const VmObjectRef target, const VmObjectRef name,
                              const std::int64_t id, const std::int32_t priority) {
            SetRef(vm, object, "target", "Ljava/lang/Runnable;", target);
            SetRef(vm, object, "name", "Ljava/lang/String;", name);
            SetInt(vm, object, "priority", "I", priority);
            SetInt(vm, object, "daemon", "Z", 0);
            SetLong(vm, object, "stackSize", 0);
            SetLong(vm, object, "id", id);
            SetInt(vm, object, "hasBeenStarted", "Z", 0);
        }

        [[nodiscard]] VmObjectRef EnsureCurrentThread(Interpreter& vm) {
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
            InitializeFields(vm, root, VmObjectRef{}, vm.NewStringUtf8("main"), 1, 5);
            return root;
        }

        [[nodiscard]] VmValue Construct(IntrinsicContext& context,
                                        const VmObjectRef target,
                                        const VmObjectRef explicit_name,
                                        const bool has_explicit_name) {
            if (has_explicit_name && !explicit_name.IsValid()) {
                throw VmJavaThrow{
                    "Ljava/lang/NullPointerException;", "threadName == null"
                };
            }
            auto& runtime = context.vm.Threads();
            const auto current = EnsureCurrentThread(context.vm);
            const auto priority =
                    GetInt(context.vm, current, "priority", "I");
            const auto id = runtime.AllocateThreadId();
            const auto name = has_explicit_name
                                  ? explicit_name
                                  : context.vm.NewStringUtf8("Thread-" + std::to_string(id));
            InitializeFields(context.vm, context.receiver, target, name, static_cast<std::int64_t>(id), priority);
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
        builder.InstanceField("target", "Ljava/lang/Runnable;")
                .InstanceField("name", "Ljava/lang/String;")
                .InstanceField("priority", "I")
                .InstanceField("daemon", "Z")
                .InstanceField("stackSize", "J")
                .InstanceField("id", "J")
                .InstanceField("hasBeenStarted", "Z")
                .ConstantInt("MIN_PRIORITY", "I", 1)
                .ConstantInt("NORM_PRIORITY", "I", 5)
                .ConstantInt("MAX_PRIORITY", "I", 10);

        builder.Constructor("()V", [](IntrinsicContext& context) {
            return Construct(context, VmObjectRef{}, VmObjectRef{}, false);
        });
        builder.Constructor("(Ljava/lang/Runnable;)V", [](IntrinsicContext& context) {
            return Construct(context, context.arguments[0].ref, VmObjectRef{}, false);
        });
        builder.Constructor("(Ljava/lang/String;)V", [](IntrinsicContext& context) {
            return Construct(context, VmObjectRef{}, context.arguments[0].ref, true);
        });
        builder.Constructor("(Ljava/lang/Runnable;Ljava/lang/String;)V", [](IntrinsicContext& context) {
            return Construct(context, context.arguments[0].ref, context.arguments[1].ref, true);
        });

        // AOSP Thread.run: target dispatch belongs here. VmThreadRuntime always
        // dispatches virtual this.run(), so a Thread subclass override wins.
        builder.VirtualMethod("run", "()V", [](IntrinsicContext& context) {
            const auto target = GetRef(context.vm, context.receiver, "target", "Ljava/lang/Runnable;");
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

        builder.VirtualMethod("start", "()V", [](IntrinsicContext& context) {
            if (GetInt(context.vm, context.receiver, "hasBeenStarted", "Z") != 0) {
                throw VmJavaThrow{
                    "Ljava/lang/IllegalThreadStateException;", "Thread already started"
                };
            }
            SetInt(context.vm, context.receiver, "hasBeenStarted", "Z", 1);
            const auto name = GetRef(context.vm, context.receiver, "name", "Ljava/lang/String;");
            context.vm.Threads().Start(
                context.receiver, context.vm.StringUtf8(name),
                static_cast<std::uint64_t>(GetLong(context.vm, context.receiver, "id"))
            );
            return VmValue::Void();
        });

        builder.StaticMethod("currentThread", "()Ljava/lang/Thread;", [](IntrinsicContext& context) {
            return VmValue::Ref(EnsureCurrentThread(context.vm));
        });
        builder.VirtualMethod("getId", "()J", [](IntrinsicContext& context) {
            return VmValue::Long(GetLong(context.vm, context.receiver, "id"));
        });
        builder.FinalMethod("getName", "()Ljava/lang/String;", [](IntrinsicContext& context) {
            return VmValue::Ref(GetRef(
                context.vm, context.receiver, "name", "Ljava/lang/String;"));
        });
        builder.FinalMethod("setName", "(Ljava/lang/String;)V", [](IntrinsicContext& context) {
            const auto name = context.arguments[0].ref;
            if (!name.IsValid()) {
                throw VmJavaThrow{"Ljava/lang/NullPointerException;", "threadName == null"};
            }
            SetRef(context.vm, context.receiver, "name", "Ljava/lang/String;", name);
            context.vm.Threads().Rename(context.receiver, context.vm.StringUtf8(name));
            return VmValue::Void();
        });
        builder.FinalMethod("isAlive", "()Z", [](IntrinsicContext& context) {
            return VmValue::Int(context.vm.Threads().IsAlive(context.receiver) ? 1 : 0);
        });
        builder.FinalMethod("getPriority", "()I", [](IntrinsicContext& context) {
            return VmValue::Int(GetInt(context.vm, context.receiver, "priority", "I"));
        });
        builder.FinalMethod("setPriority", "(I)V", [](IntrinsicContext& context) {
            const auto priority = context.arguments[0].AsInt();
            if (priority < 1 || priority > 10) {
                throw VmJavaThrow{
                    "Ljava/lang/IllegalArgumentException;", "Priority out of range: " + std::to_string(priority)
                };
            }
            // Guest-visible only: host scheduler priority is
            // deliberately not modeled yet.
            SetInt(context.vm, context.receiver, "priority", "I", priority);
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
            const auto millis = context.arguments[0].AsLong();
            ValidateTimeout(millis, 0);
            if (millis == 0) {
                context.vm.Threads().Join(context.receiver);
            } else {
                context.vm.Threads().Join(context.receiver, millis);
            }
            return VmValue::Void();
        });
        builder.FinalMethod("join", "(JI)V", [](IntrinsicContext& context) {
            const auto millis = context.arguments[0].AsLong();
            const auto nanos = context.arguments[1].AsInt();
            ValidateTimeout(millis, nanos);
            if (millis == 0 && nanos == 0) {
                context.vm.Threads().Join(context.receiver);
            } else {
                context.vm.Threads().Join(context.receiver, RoundedMillis(millis, nanos));
            }
            return VmValue::Void();
        });

        builder.StaticMethod("sleep", "(J)V", [](IntrinsicContext& context) {
            const auto millis = context.arguments[0].AsLong();
            ValidateTimeout(millis, 0);
            context.vm.Threads().Sleep(millis);
            return VmValue::Void();
        });
        builder.StaticMethod("sleep", "(JI)V", [](IntrinsicContext& context) {
            const auto millis = context.arguments[0].AsLong();
            const auto nanos = context.arguments[1].AsInt();
            ValidateTimeout(millis, nanos);
            context.vm.Threads().Sleep(RoundedMillis(millis, nanos));
            return VmValue::Void();
        });
        builder.StaticMethod("yield", "()V", [](IntrinsicContext& context) {
            context.vm.Threads().Yield();
            return VmValue::Void();
        });
        builder.StaticMethod("holdsLock", "(Ljava/lang/Object;)Z", [](IntrinsicContext& context) {
            const auto object = context.arguments[0].ref;
            if (!object.IsValid()) {
                throw VmJavaThrow{"Ljava/lang/NullPointerException;", "object == null"};
            }
            return VmValue::Int(context.vm.Monitors().IsOwner(object, context.vm.CurrentContextToken()) ? 1 : 0);
        });

        builder.FinalMethod("isDaemon", "()Z", [](IntrinsicContext& context) {
            return VmValue::Int(GetInt(context.vm, context.receiver, "daemon", "Z"));
        });
        builder.FinalMethod("setDaemon", "(Z)V", [](IntrinsicContext& context) {
            if (GetInt(context.vm, context.receiver, "hasBeenStarted", "Z") != 0) {
                throw VmJavaThrow{"Ljava/lang/IllegalThreadStateException;", "Thread already started"};
            }
            // Bounded semantics: the flag is exposed, but session termination is
            // not daemon-driven in OGPlay.
            SetInt(context.vm, context.receiver, "daemon", "Z", context.arguments[0].AsInt() != 0 ? 1 : 0);
            return VmValue::Void();
        });

        builder.UnimplementedFinal("stop", "()V");
        builder.UnimplementedFinal("suspend", "()V");
        builder.UnimplementedFinal("resume", "()V");
        builder.UnimplementedFinal("destroy", "()V");
        return std::move(builder).Build();
    }
} // namespace ogplay::runtime::dexvm::intrinsics
