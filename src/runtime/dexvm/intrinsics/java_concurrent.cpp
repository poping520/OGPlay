#include "catalog.h"
#include "shared.h"

#include <limits>
#include <string>
#include <vector>

#include "ogplay/runtime/dexvm/intrinsic_builder.h"
#include "ogplay/runtime/dexvm/vm_threads.h"
#include "ogplay/runtime/dexvm/unsafe_runtime.h"

namespace ogplay::runtime::dexvm::intrinsics {
namespace {

IntrinsicClassDecl DeclareAtomicLongNative() {
    auto builder = IntrinsicClassBuilder::Class("Ljava/util/concurrent/atomic/AtomicLong;");
    builder.StaticMethod("VMSupportsCS8", "()Z", [](IntrinsicContext&) {
        // UnsafeRuntime implements long CAS under the VM execution lock.
        return VmValue::Int(1);
    }, kAccPrivate | kAccNative);
    return std::move(builder).Build();
}

}  // namespace

// API 19 libdvm sun.misc.Unsafe: VM primitives serving libcore concurrency.
IntrinsicClassDecl Declare_sun_misc_Unsafe(const CoreIntrinsicServices& services) {
    auto builder = IntrinsicClassBuilder::Class(
        "Lsun/misc/Unsafe;", "Ljava/lang/Object;", {}, kAccPublic | kAccFinal);
    const auto singleton = builder.BoundStaticField(
        "THE_ONE", "Lsun/misc/Unsafe;", kAccPrivate | kAccFinal);
    const auto alias = builder.BoundStaticField(
        "theUnsafe", "Lsun/misc/Unsafe;", kAccPrivate | kAccFinal);
    builder.Constructor("()V", [](IntrinsicContext&) { return VmValue::Void(); },
                        kAccPrivate);
    builder.ClassInitializer([singleton, alias](IntrinsicContext& context) {
        const auto object = context.vm.NewIntrinsicInstance("Lsun/misc/Unsafe;");
        IntrinsicCall call(context);
        call.SetRef(singleton, object);
        call.SetRef(alias, object);
        return VmValue::Void();
    });
    builder.StaticMethod("getUnsafe", "()Lsun/misc/Unsafe;",
        [singleton](IntrinsicContext& context) {
            const auto caller = context.vm.CurrentCallerClass();
            if (caller && context.vm.Linker().Class(*caller).defining_loader !=
                              kBootstrapLoader)
                throw VmJavaThrow{"Ljava/lang/SecurityException;", "Unsafe access denied"};
            return VmValue::Ref(IntrinsicCall(context).GetRef(singleton));
        });
    const auto field_offset = [](IntrinsicContext& context) {
        return VmValue::Long(context.vm.Unsafe().ObjectFieldOffset(
            IntrinsicCall(context).NonNullRef(0, "field")));
    };
    builder.VirtualMethod("objectFieldOffset", "(Ljava/lang/reflect/Field;)J", field_offset);
    builder.StaticMethod("objectFieldOffset0", "(Ljava/lang/reflect/Field;)J",
                         field_offset, kAccPrivate | kAccNative);
    const auto array_base = [](IntrinsicContext& context) {
        return VmValue::Int(context.vm.Unsafe().ArrayBaseOffset(
            IntrinsicCall(context).NonNullRef(0, "class")));
    };
    const auto array_scale = [](IntrinsicContext& context) {
        return VmValue::Int(context.vm.Unsafe().ArrayIndexScale(
            IntrinsicCall(context).NonNullRef(0, "class")));
    };
    builder.VirtualMethod("arrayBaseOffset", "(Ljava/lang/Class;)I", array_base);
    builder.StaticMethod("arrayBaseOffset0", "(Ljava/lang/Class;)I", array_base,
                         kAccPrivate | kAccNative);
    builder.VirtualMethod("arrayIndexScale", "(Ljava/lang/Class;)I", array_scale);
    builder.StaticMethod("arrayIndexScale0", "(Ljava/lang/Class;)I", array_scale,
                         kAccPrivate | kAccNative);
    for (const auto kind : {UnsafeValueKind::integer, UnsafeValueKind::long_integer,
                            UnsafeValueKind::reference}) {
        const std::string name = kind == UnsafeValueKind::integer ? "Int" :
            kind == UnsafeValueKind::long_integer ? "Long" : "Object";
        const std::string type = kind == UnsafeValueKind::integer ? "I" :
            kind == UnsafeValueKind::long_integer ? "J" : "Ljava/lang/Object;";
        const auto argument = [kind](const IntrinsicCall& call, const std::size_t index) {
            if (kind == UnsafeValueKind::reference) return VmValue::Ref(call.Ref(index));
            if (kind == UnsafeValueKind::long_integer) return VmValue::Long(call.Long(index));
            return VmValue::Int(call.Int(index));
        };
        const auto get = [kind](IntrinsicContext& context) {
            IntrinsicCall call(context);
            return context.vm.Unsafe().Get(call.NonNullRef(0, "object"), call.Long(1), kind);
        };
        const auto put = [kind, argument](IntrinsicContext& context) {
            IntrinsicCall call(context);
            context.vm.Unsafe().Put(call.NonNullRef(0, "object"), call.Long(1), kind,
                                     argument(call, 2));
            return VmValue::Void();
        };
        // VmExecutionLock serializes Java/JNI field access and CAS. Its release/
        // acquire handoff is stronger than the API's plain/ordered requirements.
        for (const std::string suffix : {"", "Volatile"}) {
            builder.VirtualMethod("get" + name + suffix, "(Ljava/lang/Object;J)" + type,
                                  get, kAccPublic | kAccNative);
            builder.VirtualMethod("put" + name + suffix, "(Ljava/lang/Object;J" + type + ")V",
                                  put, kAccPublic | kAccNative);
        }
        builder.VirtualMethod("putOrdered" + name, "(Ljava/lang/Object;J" + type + ")V",
                              put, kAccPublic | kAccNative);
        builder.VirtualMethod("compareAndSwap" + name,
            "(Ljava/lang/Object;J" + type + type + ")Z",
            [kind, argument](IntrinsicContext& context) {
                IntrinsicCall call(context);
                return VmValue::Int(context.vm.Unsafe().CompareAndSwap(
                    call.NonNullRef(0, "object"), call.Long(1), kind,
                    argument(call, 2), argument(call, 3)) ? 1 : 0);
            }, kAccPublic | kAccNative);
    }
    builder.VirtualMethod("park", "(ZJ)V",
        [epoch_now = services.current_time_millis](IntrinsicContext& context) {
            IntrinsicCall call(context);
            const auto thread = context.vm.Threads().CurrentThreadObject();
            auto time = call.Long(1);
            const bool absolute = call.Int(0) != 0;
            if (absolute) {
                const auto monotonic = context.vm.Monitors().TimeSource();
                if (!epoch_now || !monotonic)
                    throw VmJavaThrow{"Ljava/lang/UnsupportedOperationException;",
                                      "Unsafe absolute park needs unified epoch and monotonic Clock"};
                const auto now = epoch_now();
                const auto uptime = monotonic();
                if (time <= now) time = uptime;
                else {
                    const auto delta = static_cast<std::uint64_t>(time) -
                                       static_cast<std::uint64_t>(now);
                    const auto max = std::numeric_limits<std::int64_t>::max();
                    if (uptime < 0 || delta > static_cast<std::uint64_t>(max - uptime))
                        throw VmJavaThrow{"Ljava/lang/IllegalArgumentException;",
                                          "Unsafe park deadline overflow"};
                    time = uptime + static_cast<std::int64_t>(delta);
                }
            }
            static_cast<void>(detail::InvokeGuest(context.vm, thread,
                absolute ? "parkUntil" : "parkFor", "(J)V", {VmValue::Long(time)}));
            return VmValue::Void();
        });
    builder.VirtualMethod("unpark", "(Ljava/lang/Object;)V", [](IntrinsicContext& context) {
        const auto thread = IntrinsicCall(context).Ref(0);
        if (!thread.IsValid() || !context.vm.Linker().IsAssignable(
                context.vm.Linker().ResolveDescriptor("Ljava/lang/Thread;"),
                context.vm.Model().ObjectClass(thread)))
            throw VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "valid for Threads only"};
        static_cast<void>(detail::InvokeGuest(context.vm, thread, "unpark", "()V"));
        return VmValue::Void();
    });
    builder.VirtualMethod("allocateInstance", "(Ljava/lang/Class;)Ljava/lang/Object;",
        [](IntrinsicContext& context) {
            const auto java_class = context.vm.Model().ClassOfClassObject(
                IntrinsicCall(context).NonNullRef(0, "class"));
            const auto& linked = context.vm.Linker().Class(java_class);
            if (linked.is_array || linked.is_interface || linked.descriptor.size() == 1U ||
                (linked.access_flags & kAccAbstract) != 0U)
                throw VmJavaThrow{"Ljava/lang/InstantiationException;", linked.descriptor};
            const auto initialized = context.vm.EnsureClassInitialized(java_class);
            if (initialized.exception.IsValid()) {
                context.vm.SetPendingException(initialized.exception);
                return VmValue::Ref(VmObjectRef{});
            }
            return VmValue::Ref(context.vm.NewIntrinsicInstance(linked.descriptor));
        }, kAccPublic | kAccNative);
    return std::move(builder).Build();
}

void AppendJavaConcurrent(std::vector<IntrinsicClassDecl>& catalog,
                          const CoreIntrinsicServices& services) {
    catalog.push_back(Declare_sun_misc_Unsafe(services));
    catalog.push_back(DeclareAtomicLongNative());
}

}  // namespace ogplay::runtime::dexvm::intrinsics
