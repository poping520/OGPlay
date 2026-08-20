#include "catalog.h"
#include "shared.h"

#include <charconv>
#include <limits>
#include <vector>

#include "ogplay/runtime/dexvm/intrinsic_builder.h"

namespace ogplay::runtime::dexvm::intrinsics {
    using namespace detail;
    namespace {

    [[nodiscard]] std::string HashCodeHex(const std::int32_t hash) {
        char buffer[8];
        const auto [end, error] = std::to_chars(
            buffer, buffer + sizeof(buffer),
            static_cast<std::uint32_t>(hash), 16);
        if (error != std::errc{}) {
            throw DexVmError(DexVmErrorReason::internal_invariant,
                             "hashCode formatting failed");
        }
        return std::string(buffer, end);
    }

    }  // namespace

    IntrinsicClassDecl Declare_java_lang_Object() {
        auto builder = IntrinsicClassBuilder::RootClass("Ljava/lang/Object;");

        builder.Constructor("()V", [](IntrinsicContext&) {
            return VmValue::Void();
        });

        builder.VirtualMethod("equals", "(Ljava/lang/Object;)Z", [](IntrinsicContext& context) {
            const auto other = context.arguments.empty() ? VmObjectRef{} : context.arguments[0].ref;
            return VmValue::Int(context.receiver == other ? 1 : 0);
        });

        builder.VirtualMethod("hashCode", "()I", [](IntrinsicContext& context) {
            return VmValue::Int(
                context.vm.Model().IdentityHashCode(context.receiver));
        });

        builder.VirtualMethod("toString", "()Ljava/lang/String;", [](IntrinsicContext& context) {
            auto& vm = context.vm;
            const auto java_class = vm.Model().ObjectClass(context.receiver);
            if (!java_class.IsValid()) {
                throw VmJavaThrow{"Ljava/lang/IllegalStateException;",
                                  "object has no VM class"};
            }
            auto& linker = vm.Linker();
            const auto hash_index =
                linker.FindVtableIndex(java_class, "hashCode", "()I");
            if (!hash_index.has_value()) {
                throw DexVmError(DexVmErrorReason::internal_invariant,
                                 "Object.toString receiver has no hashCode()");
            }
            const auto hash_outcome = vm.Call(
                linker.Class(java_class).vtable[*hash_index],
                std::vector<VmValue>{VmValue::Ref(context.receiver)});
            if (hash_outcome.exception.IsValid()) {
                throw VmJavaThrow{
                    linker.Class(hash_outcome.exception_class).descriptor,
                    hash_outcome.exception_message};
            }
            if (hash_outcome.value.kind != VmValue::Kind::cat1) {
                throw DexVmError(DexVmErrorReason::internal_invariant,
                                 "hashCode() returned a non-int value");
            }
            const auto descriptor = linker.Class(java_class).descriptor;
            return VmValue::Ref(vm.NewStringUtf8(
                DottedName(descriptor) + "@" +
                HashCodeHex(hash_outcome.value.AsInt())));
        });

        builder.VirtualMethod("clone", "()Ljava/lang/Object;", [](IntrinsicContext& context) {
            // Folded Object.clone() + internalClone: Cloneable check is the
            // Java half (libcore Object.java); CloneObject is dvmCloneObject.
            auto& vm = context.vm;
            const auto java_class = vm.Model().ObjectClass(context.receiver);
            if (!java_class.IsValid()) {
                throw VmJavaThrow{"Ljava/lang/CloneNotSupportedException;",
                                  "object has no VM class"};
            }
            const auto cloneable =
                vm.Linker().ResolveDescriptor("Ljava/lang/Cloneable;");
            if (!vm.Linker().IsAssignable(cloneable, java_class)) {
                throw VmJavaThrow{"Ljava/lang/CloneNotSupportedException;",
                                  "Class doesn't implement Cloneable"};
            }
            return VmValue::Ref(vm.CloneObject(context.receiver));
        });

        builder.FinalMethod("getClass", "()Ljava/lang/Class;", [](IntrinsicContext& context) {
            auto& vm = context.vm;
            const auto java_class = vm.Model().ObjectClass(context.receiver);
            if (!java_class.IsValid()) {
                throw VmJavaThrow{"Ljava/lang/IllegalStateException;", "object has no VM class"};
            }
            return VmValue::Ref(vm.Model().ClassObject(java_class));
        });

        builder.FinalMethod("notify", "()V", [](IntrinsicContext& context) {
            context.vm.NotifyMonitor(context.receiver, false);
            return VmValue::Void();
        });

        builder.FinalMethod("notifyAll", "()V", [](IntrinsicContext& context) {
            context.vm.NotifyMonitor(context.receiver, true);
            return VmValue::Void();
        });

        builder.FinalMethod("wait", "()V", [](IntrinsicContext& context) {
            context.vm.WaitOnMonitor(context.receiver, 0);
            return VmValue::Void();
        });

        builder.FinalMethod("wait", "(J)V", [](IntrinsicContext& context) {
            // 0 means "no deadline" in the Java contract, which is exactly what
            // WaitOnMonitor treats it as.
            context.vm.WaitOnMonitor(context.receiver, context.arguments[0].AsLong());
            return VmValue::Void();
        });

        builder.FinalMethod("wait", "(JI)V", [](IntrinsicContext& context) {
            // AOSP vm/Sync.cpp waitMonitor validates the (msec, nsec) pair
            // as a unit before parking. The monitor table parks on whole
            // milliseconds only, so a nonzero nanos rounds the deadline up
            // to the next millisecond; (0, 0) stays untimed like wait().
            const auto millis = context.arguments[0].AsLong();
            const auto nanos = context.arguments[1].AsInt();
            if (millis < 0 || nanos < 0 || nanos > 999999) {
                throw VmJavaThrow{"Ljava/lang/IllegalArgumentException;",
                                  "timeout arguments out of range"};
            }
            auto deadline = millis;
            if (nanos > 0 && deadline < std::numeric_limits<std::int64_t>::max()) {
                ++deadline;
            }
            context.vm.WaitOnMonitor(context.receiver, deadline);
            return VmValue::Void();
        });

        auto result = std::move(builder).Build();
        return result;
    }
} // namespace ogplay::runtime::dexvm::intrinsics
