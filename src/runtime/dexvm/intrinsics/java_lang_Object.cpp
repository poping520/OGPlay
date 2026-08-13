#include "catalog.h"
#include "shared.h"

#include "ogplay/runtime/dexvm/intrinsic_builder.h"

namespace ogplay::runtime::dexvm::intrinsics {
using namespace detail;

IntrinsicClassDecl Declare_java_lang_Object() {
    IntrinsicClassBuilder builder("Ljava/lang/Object;");
    builder.Virtual("<init>", "()V",
        [](IntrinsicContext &) { return VmValue::Void(); });
    builder.Overridable("equals", "(Ljava/lang/Object;)Z",
        [](IntrinsicContext& context) {
            const auto other =
                context.arguments.empty() ? VmObjectRef{} : context.arguments[0].ref;
                return VmValue::Int(context.receiver == other ? 1 : 0);
            });
    builder.Overridable("hashCode", "()I",
        [](IntrinsicContext& context) {
            return VmValue::Int(static_cast<std::int32_t>(context.receiver.Value()));
            });
    builder.Overridable("toString", "()Ljava/lang/String;",
        [](IntrinsicContext& context) {
                auto& vm = context.vm;
                const auto java_class = vm.Model().ObjectClass(context.receiver);
            const auto descriptor = java_class.IsValid()
                                        ? vm.Linker().Class(java_class).descriptor
                                         : std::string("<external>");
            return VmValue::Ref(
                vm.NewStringUtf8(DottedName(descriptor) + "@" +
                    std::to_string(context.receiver.Value())));
            });
    builder.Virtual("getClass", "()Ljava/lang/Class;",
        [](IntrinsicContext& context) {
                auto& vm = context.vm;
                const auto java_class = vm.Model().ObjectClass(context.receiver);
                if (!java_class.IsValid()) {
                    throw VmJavaThrow{"Ljava/lang/IllegalStateException;",
                                      "object has no VM class"};
                }
                return VmValue::Ref(vm.Model().ClassObject(java_class));
            });
    builder.Virtual("notify", "()V",
        [](IntrinsicContext &context) {
            context.vm.NotifyMonitor(context.receiver, false);
            return VmValue::Void();
          });
    builder.Virtual("notifyAll", "()V",
        [](IntrinsicContext &context) {
            context.vm.NotifyMonitor(context.receiver, true);
            return VmValue::Void();
          });
    builder.Virtual("wait", "()V",
        [](IntrinsicContext& context) {
                context.vm.WaitOnMonitor(context.receiver, 0);
                return VmValue::Void();
            });
    builder.Virtual("wait", "(J)V",
        [](IntrinsicContext& context) {
                // 0 means "no deadline" in the Java contract, which is exactly what
                // WaitOnMonitor treats it as.
                context.vm.WaitOnMonitor(context.receiver,
                                         context.arguments[0].AsLong());
                return VmValue::Void();
            });
    auto result = std::move(builder).Build();
    return result;
}

}  // namespace ogplay::runtime::dexvm::intrinsics
