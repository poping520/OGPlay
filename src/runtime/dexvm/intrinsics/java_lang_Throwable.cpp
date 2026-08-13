#include "catalog.h"
#include "shared.h"

#include "ogplay/runtime/dexvm/intrinsic_builder.h"

namespace ogplay::runtime::dexvm::intrinsics {
using namespace detail;

IntrinsicClassDecl Declare_java_lang_Throwable() {
    IntrinsicClassBuilder builder("Ljava/lang/Throwable;");
    builder.Super("Ljava/lang/Object;");
    builder.Implements("Ljava/io/Serializable;");
    builder.Virtual("<init>", "()V",
        [](IntrinsicContext &) { return VmValue::Void(); });
    builder.Virtual("<init>", "(Ljava/lang/String;)V",
        [](IntrinsicContext& context) {
                const auto message = context.arguments[0].ref;
                context.vm.SetThrowableMessage(context.receiver, message);
                return VmValue::Void();
            });
    builder.Overridable("getMessage", "()Ljava/lang/String;",
        [](IntrinsicContext &context) {
                return VmValue::Ref(context.vm.ThrowableMessage(context.receiver));
            });
    builder.Overridable("toString", "()Ljava/lang/String;",
        [](IntrinsicContext &context) {
                auto& vm = context.vm;
                const auto java_class = vm.Model().ObjectClass(context.receiver);
                std::string rendered = DottedName(
                    java_class.IsValid() ? vm.Linker().Class(java_class).descriptor
                                         : std::string("<throwable>"));
                const auto message = vm.ThrowableMessage(context.receiver);
                if (message.IsValid()) {
                    rendered += ": " + vm.StringUtf8(message);
                }
                return VmValue::Ref(vm.NewStringUtf8(rendered));
            });
    builder.Virtual("printStackTrace", "()V",
        [](IntrinsicContext &context) {
                auto* logger = context.vm.Log();
                if (logger != nullptr) {
                  const auto message = context.vm.ThrowableMessage(context.receiver);
                    logger->Write(core::LogLevel::warn, "runtime.dexvm.guest",
                                  "printStackTrace: " +
                                    (message.IsValid() ? context.vm.StringUtf8(message)
                                           : std::string("<no message>")));
                }
                return VmValue::Void();
            });
    auto result = std::move(builder).Build();
    return result;
}

}  // namespace ogplay::runtime::dexvm::intrinsics
