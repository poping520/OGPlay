#include "catalog.h"
#include "shared.h"

#include "ogplay/runtime/dexvm/intrinsic_builder.h"

namespace ogplay::runtime::dexvm::intrinsics {
using namespace detail;

namespace {

void SetThrowableRefField(IntrinsicContext& context,
                          const std::string_view name,
                          const std::string_view descriptor,
                          const VmObjectRef value) {
    const auto java_class = context.vm.Model().ObjectClass(context.receiver);
    const auto field = context.vm.Linker().FindFieldRecursive(
        java_class, std::string(name), std::string(descriptor));
    if (!field.has_value()) {
        throw DexVmError(DexVmErrorReason::internal_invariant,
                         "throwable field is missing: " + std::string(name));
    }
    const auto& linked = context.vm.Linker().Field(*field);
    auto slots = context.vm.Model().InstanceSlots(context.receiver);
    if (linked.slot >= slots.size() || !linked.is_ref) {
        throw DexVmError(DexVmErrorReason::internal_invariant,
                         "throwable field slot is invalid: " +
                             std::string(name));
    }
    slots[linked.slot] = {value.Value(), SlotTag::ref};
}

[[nodiscard]] VmObjectRef GetThrowableRefField(
    IntrinsicContext& context, const std::string_view name,
    const std::string_view descriptor) {
    const auto java_class = context.vm.Model().ObjectClass(context.receiver);
    const auto field = context.vm.Linker().FindFieldRecursive(
        java_class, std::string(name), std::string(descriptor));
    if (!field.has_value()) {
        throw DexVmError(DexVmErrorReason::internal_invariant,
                         "throwable field is missing: " + std::string(name));
    }
    const auto& linked = context.vm.Linker().Field(*field);
    const auto slots = context.vm.Model().InstanceSlots(context.receiver);
    if (linked.slot >= slots.size() || !linked.is_ref) {
        throw DexVmError(DexVmErrorReason::internal_invariant,
                         "throwable field slot is invalid: " +
                             std::string(name));
    }
    return VmObjectRef(slots[linked.slot].bits);
}

IntrinsicClassDecl Declare_java_lang_ArithmeticException() {
    IntrinsicClassBuilder builder("Ljava/lang/ArithmeticException;");
    builder.Super("Ljava/lang/RuntimeException;");
    builder.Virtual("<init>", "()V",
        [](IntrinsicContext &) { return VmValue::Void(); });
    builder.Virtual("<init>", "(Ljava/lang/String;)V",
        [](IntrinsicContext& context) {
                const auto message = context.arguments[0].ref;
                context.vm.SetThrowableMessage(context.receiver, message);
                return VmValue::Void();
            });
    auto result = std::move(builder).Build();
    return result;
}

IntrinsicClassDecl Declare_java_lang_ArrayIndexOutOfBoundsException() {
    IntrinsicClassBuilder builder("Ljava/lang/ArrayIndexOutOfBoundsException;");
    builder.Super("Ljava/lang/IndexOutOfBoundsException;");
    builder.Virtual("<init>", "()V",
        [](IntrinsicContext &) { return VmValue::Void(); });
    builder.Virtual("<init>", "(Ljava/lang/String;)V",
        [](IntrinsicContext& context) {
                const auto message = context.arguments[0].ref;
                context.vm.SetThrowableMessage(context.receiver, message);
                return VmValue::Void();
            });
    auto result = std::move(builder).Build();
    return result;
}

IntrinsicClassDecl Declare_java_lang_ArrayStoreException() {
    IntrinsicClassBuilder builder("Ljava/lang/ArrayStoreException;");
    builder.Super("Ljava/lang/RuntimeException;");
    builder.Virtual("<init>", "()V",
        [](IntrinsicContext &) { return VmValue::Void(); });
    builder.Virtual("<init>", "(Ljava/lang/String;)V",
        [](IntrinsicContext& context) {
                const auto message = context.arguments[0].ref;
                context.vm.SetThrowableMessage(context.receiver, message);
                return VmValue::Void();
            });
    auto result = std::move(builder).Build();
    return result;
}

IntrinsicClassDecl Declare_java_lang_ClassCastException() {
    IntrinsicClassBuilder builder("Ljava/lang/ClassCastException;");
    builder.Super("Ljava/lang/RuntimeException;");
    builder.Virtual("<init>", "()V",
        [](IntrinsicContext &) { return VmValue::Void(); });
    builder.Virtual("<init>", "(Ljava/lang/String;)V",
        [](IntrinsicContext& context) {
                const auto message = context.arguments[0].ref;
                context.vm.SetThrowableMessage(context.receiver, message);
                return VmValue::Void();
            });
    auto result = std::move(builder).Build();
    return result;
}

IntrinsicClassDecl Declare_java_lang_ClassNotFoundException() {
    IntrinsicClassBuilder builder("Ljava/lang/ClassNotFoundException;");
    builder.Super("Ljava/lang/ReflectiveOperationException;");
    builder.Field("ex", "Ljava/lang/Throwable;", false);
    builder.Virtual("<init>", "()V",
        [](IntrinsicContext &) { return VmValue::Void(); });
    builder.Virtual("<init>", "(Ljava/lang/String;)V",
        [](IntrinsicContext& context) {
                const auto message = context.arguments[0].ref;
                context.vm.SetThrowableMessage(context.receiver, message);
                return VmValue::Void();
            });
    builder.Virtual(
        "<init>", "(Ljava/lang/String;Ljava/lang/Throwable;)V",
        [](IntrinsicContext& context) {
                const auto message = context.arguments[0].ref;
                context.vm.SetThrowableMessage(context.receiver, message);
                SetThrowableRefField(context, "ex", "Ljava/lang/Throwable;",
                                     context.arguments[1].ref);
                return VmValue::Void();
            });
    builder.Overridable("getException", "()Ljava/lang/Throwable;",
        [](IntrinsicContext& context) {
                return VmValue::Ref(GetThrowableRefField(
                    context, "ex", "Ljava/lang/Throwable;"));
            });
    builder.Overridable("getCause", "()Ljava/lang/Throwable;",
        [](IntrinsicContext& context) {
                return VmValue::Ref(GetThrowableRefField(
                    context, "ex", "Ljava/lang/Throwable;"));
            });
    auto result = std::move(builder).Build();
    return result;
}

IntrinsicClassDecl Declare_java_lang_Error() {
    IntrinsicClassBuilder builder("Ljava/lang/Error;");
    builder.Super("Ljava/lang/Throwable;");
    builder.Virtual("<init>", "()V",
        [](IntrinsicContext &) { return VmValue::Void(); });
    builder.Virtual("<init>", "(Ljava/lang/String;)V",
        [](IntrinsicContext& context) {
                const auto message = context.arguments[0].ref;
                context.vm.SetThrowableMessage(context.receiver, message);
                return VmValue::Void();
            });
    auto result = std::move(builder).Build();
    return result;
}

IntrinsicClassDecl Declare_java_lang_Exception() {
    IntrinsicClassBuilder builder("Ljava/lang/Exception;");
    builder.Super("Ljava/lang/Throwable;");
    builder.Virtual("<init>", "()V",
        [](IntrinsicContext &) { return VmValue::Void(); });
    builder.Virtual("<init>", "(Ljava/lang/String;)V",
        [](IntrinsicContext& context) {
                const auto message = context.arguments[0].ref;
                context.vm.SetThrowableMessage(context.receiver, message);
                return VmValue::Void();
            });
    auto result = std::move(builder).Build();
    return result;
}

IntrinsicClassDecl Declare_java_lang_IllegalArgumentException() {
    IntrinsicClassBuilder builder("Ljava/lang/IllegalArgumentException;");
    builder.Super("Ljava/lang/RuntimeException;");
    builder.Virtual("<init>", "()V",
        [](IntrinsicContext &) { return VmValue::Void(); });
    builder.Virtual("<init>", "(Ljava/lang/String;)V",
        [](IntrinsicContext& context) {
                const auto message = context.arguments[0].ref;
                context.vm.SetThrowableMessage(context.receiver, message);
                return VmValue::Void();
            });
    auto result = std::move(builder).Build();
    return result;
}

IntrinsicClassDecl Declare_java_lang_IllegalMonitorStateException() {
    IntrinsicClassBuilder builder("Ljava/lang/IllegalMonitorStateException;");
    builder.Super("Ljava/lang/RuntimeException;");
    builder.Virtual("<init>", "()V",
        [](IntrinsicContext &) { return VmValue::Void(); });
    builder.Virtual("<init>", "(Ljava/lang/String;)V",
        [](IntrinsicContext& context) {
                const auto message = context.arguments[0].ref;
                context.vm.SetThrowableMessage(context.receiver, message);
                return VmValue::Void();
            });
    auto result = std::move(builder).Build();
    return result;
}

IntrinsicClassDecl Declare_java_lang_IllegalStateException() {
    IntrinsicClassBuilder builder("Ljava/lang/IllegalStateException;");
    builder.Super("Ljava/lang/RuntimeException;");
    builder.Virtual("<init>", "()V",
        [](IntrinsicContext &) { return VmValue::Void(); });
    builder.Virtual("<init>", "(Ljava/lang/String;)V",
        [](IntrinsicContext& context) {
                const auto message = context.arguments[0].ref;
                context.vm.SetThrowableMessage(context.receiver, message);
                return VmValue::Void();
            });
    auto result = std::move(builder).Build();
    return result;
}

IntrinsicClassDecl Declare_java_lang_IllegalThreadStateException() {
    IntrinsicClassBuilder builder("Ljava/lang/IllegalThreadStateException;");
    builder.Super("Ljava/lang/IllegalArgumentException;");
    builder.Virtual("<init>", "()V",
        [](IntrinsicContext &) { return VmValue::Void(); });
    builder.Virtual("<init>", "(Ljava/lang/String;)V",
        [](IntrinsicContext& context) {
                const auto message = context.arguments[0].ref;
                context.vm.SetThrowableMessage(context.receiver, message);
                return VmValue::Void();
            });
    auto result = std::move(builder).Build();
    return result;
}

IntrinsicClassDecl Declare_java_lang_IndexOutOfBoundsException() {
    IntrinsicClassBuilder builder("Ljava/lang/IndexOutOfBoundsException;");
    builder.Super("Ljava/lang/RuntimeException;");
    builder.Virtual("<init>", "()V",
        [](IntrinsicContext &) { return VmValue::Void(); });
    builder.Virtual("<init>", "(Ljava/lang/String;)V",
        [](IntrinsicContext& context) {
                const auto message = context.arguments[0].ref;
                context.vm.SetThrowableMessage(context.receiver, message);
                return VmValue::Void();
            });
    auto result = std::move(builder).Build();
    return result;
}

IntrinsicClassDecl Declare_java_lang_InterruptedException() {
    IntrinsicClassBuilder builder("Ljava/lang/InterruptedException;");
    builder.Super("Ljava/lang/Exception;");
    builder.Virtual("<init>", "()V",
        [](IntrinsicContext &) { return VmValue::Void(); });
    builder.Virtual("<init>", "(Ljava/lang/String;)V",
        [](IntrinsicContext& context) {
                const auto message = context.arguments[0].ref;
                context.vm.SetThrowableMessage(context.receiver, message);
                return VmValue::Void();
            });
    auto result = std::move(builder).Build();
    return result;
}

IntrinsicClassDecl Declare_java_lang_LinkageError() {
    IntrinsicClassBuilder builder("Ljava/lang/LinkageError;");
    builder.Super("Ljava/lang/Error;");
    builder.Virtual("<init>", "()V",
        [](IntrinsicContext &) { return VmValue::Void(); });
    builder.Virtual("<init>", "(Ljava/lang/String;)V",
        [](IntrinsicContext& context) {
                const auto message = context.arguments[0].ref;
                context.vm.SetThrowableMessage(context.receiver, message);
                return VmValue::Void();
            });
    auto result = std::move(builder).Build();
    return result;
}

IntrinsicClassDecl Declare_java_lang_NegativeArraySizeException() {
    IntrinsicClassBuilder builder("Ljava/lang/NegativeArraySizeException;");
    builder.Super("Ljava/lang/RuntimeException;");
    builder.Virtual("<init>", "()V",
        [](IntrinsicContext &) { return VmValue::Void(); });
    builder.Virtual("<init>", "(Ljava/lang/String;)V",
        [](IntrinsicContext& context) {
                const auto message = context.arguments[0].ref;
                context.vm.SetThrowableMessage(context.receiver, message);
                return VmValue::Void();
            });
    auto result = std::move(builder).Build();
    return result;
}

IntrinsicClassDecl Declare_java_lang_NoClassDefFoundError() {
    IntrinsicClassBuilder builder("Ljava/lang/NoClassDefFoundError;");
    builder.Super("Ljava/lang/LinkageError;");
    builder.Virtual("<init>", "()V",
        [](IntrinsicContext &) { return VmValue::Void(); });
    builder.Virtual("<init>", "(Ljava/lang/String;)V",
        [](IntrinsicContext& context) {
                const auto message = context.arguments[0].ref;
                context.vm.SetThrowableMessage(context.receiver, message);
                return VmValue::Void();
            });
    auto result = std::move(builder).Build();
    return result;
}

IntrinsicClassDecl Declare_java_lang_NullPointerException() {
    IntrinsicClassBuilder builder("Ljava/lang/NullPointerException;");
    builder.Super("Ljava/lang/RuntimeException;");
    builder.Virtual("<init>", "()V",
        [](IntrinsicContext &) { return VmValue::Void(); });
    builder.Virtual("<init>", "(Ljava/lang/String;)V",
        [](IntrinsicContext& context) {
                const auto message = context.arguments[0].ref;
                context.vm.SetThrowableMessage(context.receiver, message);
                return VmValue::Void();
            });
    auto result = std::move(builder).Build();
    return result;
}

IntrinsicClassDecl Declare_java_lang_NumberFormatException() {
    IntrinsicClassBuilder builder("Ljava/lang/NumberFormatException;");
    builder.Super("Ljava/lang/IllegalArgumentException;");
    builder.Virtual("<init>", "()V",
        [](IntrinsicContext &) { return VmValue::Void(); });
    builder.Virtual("<init>", "(Ljava/lang/String;)V",
        [](IntrinsicContext& context) {
                const auto message = context.arguments[0].ref;
                context.vm.SetThrowableMessage(context.receiver, message);
                return VmValue::Void();
            });
    auto result = std::move(builder).Build();
    return result;
}

IntrinsicClassDecl Declare_java_lang_OutOfMemoryError() {
    IntrinsicClassBuilder builder("Ljava/lang/OutOfMemoryError;");
    builder.Super("Ljava/lang/VirtualMachineError;");
    builder.Virtual("<init>", "()V",
        [](IntrinsicContext &) { return VmValue::Void(); });
    builder.Virtual("<init>", "(Ljava/lang/String;)V",
        [](IntrinsicContext& context) {
                const auto message = context.arguments[0].ref;
                context.vm.SetThrowableMessage(context.receiver, message);
                return VmValue::Void();
            });
    auto result = std::move(builder).Build();
    return result;
}

IntrinsicClassDecl Declare_java_lang_RuntimeException() {
    IntrinsicClassBuilder builder("Ljava/lang/RuntimeException;");
    builder.Super("Ljava/lang/Exception;");
    builder.Virtual("<init>", "()V",
        [](IntrinsicContext &) { return VmValue::Void(); });
    builder.Virtual("<init>", "(Ljava/lang/String;)V",
        [](IntrinsicContext& context) {
                const auto message = context.arguments[0].ref;
                context.vm.SetThrowableMessage(context.receiver, message);
                return VmValue::Void();
            });
    auto result = std::move(builder).Build();
    return result;
}

IntrinsicClassDecl Declare_java_lang_StackOverflowError() {
    IntrinsicClassBuilder builder("Ljava/lang/StackOverflowError;");
    builder.Super("Ljava/lang/VirtualMachineError;");
    builder.Virtual("<init>", "()V",
        [](IntrinsicContext &) { return VmValue::Void(); });
    builder.Virtual("<init>", "(Ljava/lang/String;)V",
        [](IntrinsicContext& context) {
                const auto message = context.arguments[0].ref;
                context.vm.SetThrowableMessage(context.receiver, message);
                return VmValue::Void();
            });
    auto result = std::move(builder).Build();
    return result;
}

IntrinsicClassDecl Declare_java_lang_StringIndexOutOfBoundsException() {
    IntrinsicClassBuilder builder("Ljava/lang/StringIndexOutOfBoundsException;");
    builder.Super("Ljava/lang/IndexOutOfBoundsException;");
    builder.Virtual("<init>", "()V",
        [](IntrinsicContext &) { return VmValue::Void(); });
    builder.Virtual("<init>", "(Ljava/lang/String;)V",
        [](IntrinsicContext& context) {
                const auto message = context.arguments[0].ref;
                context.vm.SetThrowableMessage(context.receiver, message);
                return VmValue::Void();
            });
    auto result = std::move(builder).Build();
    return result;
}

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

IntrinsicClassDecl Declare_java_lang_UnsatisfiedLinkError() {
    IntrinsicClassBuilder builder("Ljava/lang/UnsatisfiedLinkError;");
    builder.Super("Ljava/lang/LinkageError;");
    builder.Virtual("<init>", "()V",
        [](IntrinsicContext &) { return VmValue::Void(); });
    builder.Virtual("<init>", "(Ljava/lang/String;)V",
        [](IntrinsicContext& context) {
                const auto message = context.arguments[0].ref;
                context.vm.SetThrowableMessage(context.receiver, message);
                return VmValue::Void();
            });
    auto result = std::move(builder).Build();
    return result;
}

IntrinsicClassDecl Declare_java_lang_UnsupportedOperationException() {
    IntrinsicClassBuilder builder("Ljava/lang/UnsupportedOperationException;");
    builder.Super("Ljava/lang/RuntimeException;");
    builder.Virtual("<init>", "()V",
        [](IntrinsicContext &) { return VmValue::Void(); });
    builder.Virtual("<init>", "(Ljava/lang/String;)V",
        [](IntrinsicContext& context) {
                const auto message = context.arguments[0].ref;
                context.vm.SetThrowableMessage(context.receiver, message);
                return VmValue::Void();
            });
    auto result = std::move(builder).Build();
    return result;
}

IntrinsicClassDecl Declare_java_lang_VirtualMachineError() {
    IntrinsicClassBuilder builder("Ljava/lang/VirtualMachineError;");
    builder.Super("Ljava/lang/Error;");
    builder.Virtual("<init>", "()V",
        [](IntrinsicContext &) { return VmValue::Void(); });
    builder.Virtual("<init>", "(Ljava/lang/String;)V",
        [](IntrinsicContext& context) {
                const auto message = context.arguments[0].ref;
                context.vm.SetThrowableMessage(context.receiver, message);
                return VmValue::Void();
            });
    auto result = std::move(builder).Build();
    return result;
}

IntrinsicClassDecl Declare_java_lang_AbstractMethodError() {
    IntrinsicClassBuilder builder("Ljava/lang/AbstractMethodError;");
    builder.Super("Ljava/lang/IncompatibleClassChangeError;");
    builder.Virtual("<init>", "()V",
        [](IntrinsicContext &) { return VmValue::Void(); });
    builder.Virtual("<init>", "(Ljava/lang/String;)V",
        [](IntrinsicContext& context) {
                const auto message = context.arguments[0].ref;
                context.vm.SetThrowableMessage(context.receiver, message);
                return VmValue::Void();
            });
    auto result = std::move(builder).Build();
    return result;
}

IntrinsicClassDecl Declare_java_lang_ClassCircularityError() {
    IntrinsicClassBuilder builder("Ljava/lang/ClassCircularityError;");
    builder.Super("Ljava/lang/LinkageError;");
    builder.Virtual("<init>", "()V",
        [](IntrinsicContext &) { return VmValue::Void(); });
    builder.Virtual("<init>", "(Ljava/lang/String;)V",
        [](IntrinsicContext& context) {
                const auto message = context.arguments[0].ref;
                context.vm.SetThrowableMessage(context.receiver, message);
                return VmValue::Void();
            });
    auto result = std::move(builder).Build();
    return result;
}

IntrinsicClassDecl Declare_java_lang_ClassFormatError() {
    IntrinsicClassBuilder builder("Ljava/lang/ClassFormatError;");
    builder.Super("Ljava/lang/LinkageError;");
    builder.Virtual("<init>", "()V",
        [](IntrinsicContext &) { return VmValue::Void(); });
    builder.Virtual("<init>", "(Ljava/lang/String;)V",
        [](IntrinsicContext& context) {
                const auto message = context.arguments[0].ref;
                context.vm.SetThrowableMessage(context.receiver, message);
                return VmValue::Void();
            });
    auto result = std::move(builder).Build();
    return result;
}

IntrinsicClassDecl Declare_java_lang_CloneNotSupportedException() {
    IntrinsicClassBuilder builder("Ljava/lang/CloneNotSupportedException;");
    builder.Super("Ljava/lang/Exception;");
    builder.Virtual("<init>", "()V",
        [](IntrinsicContext &) { return VmValue::Void(); });
    builder.Virtual("<init>", "(Ljava/lang/String;)V",
        [](IntrinsicContext& context) {
                const auto message = context.arguments[0].ref;
                context.vm.SetThrowableMessage(context.receiver, message);
                return VmValue::Void();
            });
    auto result = std::move(builder).Build();
    return result;
}

IntrinsicClassDecl Declare_java_lang_IllegalAccessError() {
    IntrinsicClassBuilder builder("Ljava/lang/IllegalAccessError;");
    builder.Super("Ljava/lang/IncompatibleClassChangeError;");
    builder.Virtual("<init>", "()V",
        [](IntrinsicContext &) { return VmValue::Void(); });
    builder.Virtual("<init>", "(Ljava/lang/String;)V",
        [](IntrinsicContext& context) {
                const auto message = context.arguments[0].ref;
                context.vm.SetThrowableMessage(context.receiver, message);
                return VmValue::Void();
            });
    auto result = std::move(builder).Build();
    return result;
}

IntrinsicClassDecl Declare_java_lang_IllegalAccessException() {
    IntrinsicClassBuilder builder("Ljava/lang/IllegalAccessException;");
    builder.Super("Ljava/lang/ReflectiveOperationException;");
    builder.Virtual("<init>", "()V",
        [](IntrinsicContext &) { return VmValue::Void(); });
    builder.Virtual("<init>", "(Ljava/lang/String;)V",
        [](IntrinsicContext& context) {
                const auto message = context.arguments[0].ref;
                context.vm.SetThrowableMessage(context.receiver, message);
                return VmValue::Void();
            });
    auto result = std::move(builder).Build();
    return result;
}

IntrinsicClassDecl Declare_java_lang_IncompatibleClassChangeError() {
    IntrinsicClassBuilder builder("Ljava/lang/IncompatibleClassChangeError;");
    builder.Super("Ljava/lang/LinkageError;");
    builder.Virtual("<init>", "()V",
        [](IntrinsicContext &) { return VmValue::Void(); });
    builder.Virtual("<init>", "(Ljava/lang/String;)V",
        [](IntrinsicContext& context) {
                const auto message = context.arguments[0].ref;
                context.vm.SetThrowableMessage(context.receiver, message);
                return VmValue::Void();
            });
    auto result = std::move(builder).Build();
    return result;
}

IntrinsicClassDecl Declare_java_lang_InternalError() {
    IntrinsicClassBuilder builder("Ljava/lang/InternalError;");
    builder.Super("Ljava/lang/VirtualMachineError;");
    builder.Virtual("<init>", "()V",
        [](IntrinsicContext &) { return VmValue::Void(); });
    builder.Virtual("<init>", "(Ljava/lang/String;)V",
        [](IntrinsicContext& context) {
                const auto message = context.arguments[0].ref;
                context.vm.SetThrowableMessage(context.receiver, message);
                return VmValue::Void();
            });
    auto result = std::move(builder).Build();
    return result;
}

IntrinsicClassDecl Declare_java_lang_NoSuchFieldError() {
    IntrinsicClassBuilder builder("Ljava/lang/NoSuchFieldError;");
    builder.Super("Ljava/lang/IncompatibleClassChangeError;");
    builder.Virtual("<init>", "()V",
        [](IntrinsicContext &) { return VmValue::Void(); });
    builder.Virtual("<init>", "(Ljava/lang/String;)V",
        [](IntrinsicContext& context) {
                const auto message = context.arguments[0].ref;
                context.vm.SetThrowableMessage(context.receiver, message);
                return VmValue::Void();
            });
    auto result = std::move(builder).Build();
    return result;
}

IntrinsicClassDecl Declare_java_lang_NoSuchFieldException() {
    IntrinsicClassBuilder builder("Ljava/lang/NoSuchFieldException;");
    builder.Super("Ljava/lang/ReflectiveOperationException;");
    builder.Virtual("<init>", "()V",
        [](IntrinsicContext &) { return VmValue::Void(); });
    builder.Virtual("<init>", "(Ljava/lang/String;)V",
        [](IntrinsicContext& context) {
                const auto message = context.arguments[0].ref;
                context.vm.SetThrowableMessage(context.receiver, message);
                return VmValue::Void();
            });
    auto result = std::move(builder).Build();
    return result;
}

IntrinsicClassDecl Declare_java_lang_NoSuchMethodError() {
    IntrinsicClassBuilder builder("Ljava/lang/NoSuchMethodError;");
    builder.Super("Ljava/lang/IncompatibleClassChangeError;");
    builder.Virtual("<init>", "()V",
        [](IntrinsicContext &) { return VmValue::Void(); });
    builder.Virtual("<init>", "(Ljava/lang/String;)V",
        [](IntrinsicContext& context) {
                const auto message = context.arguments[0].ref;
                context.vm.SetThrowableMessage(context.receiver, message);
                return VmValue::Void();
            });
    auto result = std::move(builder).Build();
    return result;
}

IntrinsicClassDecl Declare_java_lang_NoSuchMethodException() {
    IntrinsicClassBuilder builder("Ljava/lang/NoSuchMethodException;");
    builder.Super("Ljava/lang/ReflectiveOperationException;");
    builder.Virtual("<init>", "()V",
        [](IntrinsicContext &) { return VmValue::Void(); });
    builder.Virtual("<init>", "(Ljava/lang/String;)V",
        [](IntrinsicContext& context) {
                const auto message = context.arguments[0].ref;
                context.vm.SetThrowableMessage(context.receiver, message);
                return VmValue::Void();
            });
    auto result = std::move(builder).Build();
    return result;
}

IntrinsicClassDecl Declare_java_lang_UnknownError() {
    IntrinsicClassBuilder builder("Ljava/lang/UnknownError;");
    builder.Super("Ljava/lang/VirtualMachineError;");
    builder.Virtual("<init>", "()V",
        [](IntrinsicContext &) { return VmValue::Void(); });
    builder.Virtual("<init>", "(Ljava/lang/String;)V",
        [](IntrinsicContext& context) {
                const auto message = context.arguments[0].ref;
                context.vm.SetThrowableMessage(context.receiver, message);
                return VmValue::Void();
            });
    auto result = std::move(builder).Build();
    return result;
}

IntrinsicClassDecl Declare_java_lang_UnsupportedClassVersionError() {
    IntrinsicClassBuilder builder("Ljava/lang/UnsupportedClassVersionError;");
    builder.Super("Ljava/lang/ClassFormatError;");
    builder.Virtual("<init>", "()V",
        [](IntrinsicContext &) { return VmValue::Void(); });
    builder.Virtual("<init>", "(Ljava/lang/String;)V",
        [](IntrinsicContext& context) {
                const auto message = context.arguments[0].ref;
                context.vm.SetThrowableMessage(context.receiver, message);
                return VmValue::Void();
            });
    auto result = std::move(builder).Build();
    return result;
}

IntrinsicClassDecl Declare_java_lang_VerifyError() {
    IntrinsicClassBuilder builder("Ljava/lang/VerifyError;");
    builder.Super("Ljava/lang/LinkageError;");
    builder.Virtual("<init>", "()V",
        [](IntrinsicContext &) { return VmValue::Void(); });
    builder.Virtual("<init>", "(Ljava/lang/String;)V",
        [](IntrinsicContext& context) {
                const auto message = context.arguments[0].ref;
                context.vm.SetThrowableMessage(context.receiver, message);
                return VmValue::Void();
            });
    auto result = std::move(builder).Build();
    return result;
}

IntrinsicClassDecl Declare_java_lang_AssertionError() {
    IntrinsicClassBuilder builder("Ljava/lang/AssertionError;");
    builder.Super("Ljava/lang/Error;");
    builder.Virtual("<init>", "()V",
        [](IntrinsicContext &) { return VmValue::Void(); });
    builder.Unimplemented("<init>",
                          "(Ljava/lang/String;Ljava/lang/Throwable;)V", false,
                          false);
    builder.Unimplemented("<init>", "(Ljava/lang/Object;)V", false, false);
    builder.Unimplemented("<init>", "(Z)V", false, false);
    builder.Unimplemented("<init>", "(C)V", false, false);
    builder.Unimplemented("<init>", "(I)V", false, false);
    builder.Unimplemented("<init>", "(J)V", false, false);
    builder.Unimplemented("<init>", "(F)V", false, false);
    builder.Unimplemented("<init>", "(D)V", false, false);
    auto result = std::move(builder).Build();
    return result;
}

IntrinsicClassDecl Declare_java_lang_EnumConstantNotPresentException() {
    IntrinsicClassBuilder builder(
        "Ljava/lang/EnumConstantNotPresentException;");
    builder.Super("Ljava/lang/RuntimeException;");
    builder.Field("enumType", "Ljava/lang/Class;", false);
    builder.Field("constantName", "Ljava/lang/String;", false);
    builder.Virtual("<init>",
                    "(Ljava/lang/Class;Ljava/lang/String;)V",
        [](IntrinsicContext& context) {
                const auto enum_type = context.arguments[0].ref;
                const auto constant_name = context.arguments[1].ref;
                if (!enum_type.IsValid()) {
                    throw VmJavaThrow{"Ljava/lang/NullPointerException;",
                                      "enum type is null"};
                }
                const auto enum_class =
                    context.vm.Model().ClassOfClassObject(enum_type);
                const auto message =
                    "enum constant " +
                    DottedName(context.vm.Linker().Class(enum_class).descriptor) +
                    "." + (constant_name.IsValid()
                                 ? context.vm.StringUtf8(constant_name)
                                 : std::string("null")) +
                    " is missing";
                context.vm.SetThrowableMessage(
                    context.receiver, context.vm.NewStringUtf8(message));
                SetThrowableRefField(context, "enumType",
                                     "Ljava/lang/Class;", enum_type);
                SetThrowableRefField(context, "constantName",
                                     "Ljava/lang/String;", constant_name);
                return VmValue::Void();
            });
    builder.Overridable("enumType", "()Ljava/lang/Class;",
        [](IntrinsicContext& context) {
                return VmValue::Ref(GetThrowableRefField(
                    context, "enumType", "Ljava/lang/Class;"));
            });
    builder.Overridable("constantName", "()Ljava/lang/String;",
        [](IntrinsicContext& context) {
                return VmValue::Ref(GetThrowableRefField(
                    context, "constantName", "Ljava/lang/String;"));
            });
    auto result = std::move(builder).Build();
    return result;
}

IntrinsicClassDecl Declare_java_lang_ExceptionInInitializerError() {
    IntrinsicClassBuilder builder(
        "Ljava/lang/ExceptionInInitializerError;");
    builder.Super("Ljava/lang/LinkageError;");
    builder.Field("exception", "Ljava/lang/Throwable;", false);
    builder.Virtual("<init>", "()V",
        [](IntrinsicContext &) { return VmValue::Void(); });
    builder.Virtual("<init>", "(Ljava/lang/String;)V",
        [](IntrinsicContext& context) {
                const auto message = context.arguments[0].ref;
                context.vm.SetThrowableMessage(context.receiver, message);
                return VmValue::Void();
            });
    builder.Virtual("<init>", "(Ljava/lang/Throwable;)V",
        [](IntrinsicContext& context) {
                SetThrowableRefField(context, "exception",
                                     "Ljava/lang/Throwable;",
                                     context.arguments[0].ref);
                return VmValue::Void();
            });
    builder.Overridable("getException", "()Ljava/lang/Throwable;",
        [](IntrinsicContext& context) {
                return VmValue::Ref(GetThrowableRefField(
                    context, "exception", "Ljava/lang/Throwable;"));
            });
    builder.Overridable("getCause", "()Ljava/lang/Throwable;",
        [](IntrinsicContext& context) {
                return VmValue::Ref(GetThrowableRefField(
                    context, "exception", "Ljava/lang/Throwable;"));
            });
    auto result = std::move(builder).Build();
    return result;
}

IntrinsicClassDecl Declare_java_lang_ReflectiveOperationException() {
    IntrinsicClassBuilder builder(
        "Ljava/lang/ReflectiveOperationException;");
    builder.Super("Ljava/lang/Exception;");
    builder.Virtual("<init>", "()V",
        [](IntrinsicContext &) { return VmValue::Void(); });
    builder.Virtual("<init>", "(Ljava/lang/String;)V",
        [](IntrinsicContext& context) {
                const auto message = context.arguments[0].ref;
                context.vm.SetThrowableMessage(context.receiver, message);
                return VmValue::Void();
            });
    builder.Unimplemented("<init>", "(Ljava/lang/Throwable;)V", false,
                          false);
    builder.Unimplemented(
        "<init>", "(Ljava/lang/String;Ljava/lang/Throwable;)V", false,
        false);
    auto result = std::move(builder).Build();
    return result;
}

IntrinsicClassDecl Declare_java_lang_TypeNotPresentException() {
    IntrinsicClassBuilder builder("Ljava/lang/TypeNotPresentException;");
    builder.Super("Ljava/lang/RuntimeException;");
    builder.Field("typeName", "Ljava/lang/String;", false);
    builder.Unimplemented(
        "<init>", "(Ljava/lang/String;Ljava/lang/Throwable;)V", false,
        false);
    builder.Overridable("typeName", "()Ljava/lang/String;",
        [](IntrinsicContext& context) {
                return VmValue::Ref(GetThrowableRefField(
                    context, "typeName", "Ljava/lang/String;"));
            });
    auto result = std::move(builder).Build();
    return result;
}

IntrinsicClassDecl Declare_java_lang_SecurityException() {
    IntrinsicClassBuilder builder("Ljava/lang/SecurityException;");
    builder.Super("Ljava/lang/RuntimeException;");
    builder.Virtual("<init>", "()V",
        [](IntrinsicContext &) { return VmValue::Void(); });
    builder.Virtual("<init>", "(Ljava/lang/String;)V",
        [](IntrinsicContext& context) {
                const auto message = context.arguments[0].ref;
                context.vm.SetThrowableMessage(context.receiver, message);
                return VmValue::Void();
            });
    builder.Unimplemented(
        "<init>", "(Ljava/lang/String;Ljava/lang/Throwable;)V", false,
        false);
    builder.Unimplemented("<init>", "(Ljava/lang/Throwable;)V", false,
                          false);
    auto result = std::move(builder).Build();
    return result;
}

IntrinsicClassDecl Declare_java_lang_ThreadDeath() {
    IntrinsicClassBuilder builder("Ljava/lang/ThreadDeath;");
    builder.Super("Ljava/lang/Error;");
    builder.Virtual("<init>", "()V",
        [](IntrinsicContext &) { return VmValue::Void(); });
    auto result = std::move(builder).Build();
    return result;
}

IntrinsicClassDecl Declare_java_lang_InstantiationError() {
    IntrinsicClassBuilder builder("Ljava/lang/InstantiationError;");
    builder.Super("Ljava/lang/IncompatibleClassChangeError;");
    builder.Virtual("<init>", "()V",
        [](IntrinsicContext &) { return VmValue::Void(); });
    builder.Virtual("<init>", "(Ljava/lang/String;)V",
        [](IntrinsicContext& context) {
                const auto message = context.arguments[0].ref;
                context.vm.SetThrowableMessage(context.receiver, message);
                return VmValue::Void();
            });
    builder.Virtual("<init>", "(Ljava/lang/Class;)V",
        [](IntrinsicContext& context) {
                const auto class_id = context.vm.Model().ClassOfClassObject(
                    context.arguments[0].ref);
                const auto message = DottedName(
                    context.vm.Linker().Class(class_id).descriptor);
                context.vm.SetThrowableMessage(
                    context.receiver, context.vm.NewStringUtf8(message));
                return VmValue::Void();
            });
    auto result = std::move(builder).Build();
    return result;
}

IntrinsicClassDecl Declare_java_lang_InstantiationException() {
    IntrinsicClassBuilder builder("Ljava/lang/InstantiationException;");
    builder.Super("Ljava/lang/ReflectiveOperationException;");
    builder.Virtual("<init>", "()V",
        [](IntrinsicContext &) { return VmValue::Void(); });
    builder.Virtual("<init>", "(Ljava/lang/String;)V",
        [](IntrinsicContext& context) {
                const auto message = context.arguments[0].ref;
                context.vm.SetThrowableMessage(context.receiver, message);
                return VmValue::Void();
            });
    builder.Virtual("<init>", "(Ljava/lang/Class;)V",
        [](IntrinsicContext& context) {
                const auto class_id = context.vm.Model().ClassOfClassObject(
                    context.arguments[0].ref);
                const auto message = DottedName(
                    context.vm.Linker().Class(class_id).descriptor);
                context.vm.SetThrowableMessage(
                    context.receiver, context.vm.NewStringUtf8(message));
                return VmValue::Void();
            });
    auto result = std::move(builder).Build();
    return result;
}

}  // namespace

void AppendJavaLangThrowables(std::vector<IntrinsicClassDecl>& catalog) {
    catalog.reserve(catalog.size() + 50);

    catalog.push_back(Declare_java_lang_Throwable());
    catalog.push_back(Declare_java_lang_Exception());
    catalog.push_back(Declare_java_lang_RuntimeException());
    catalog.push_back(Declare_java_lang_NullPointerException());
    catalog.push_back(Declare_java_lang_ArithmeticException());
    catalog.push_back(Declare_java_lang_IndexOutOfBoundsException());
    catalog.push_back(Declare_java_lang_ArrayIndexOutOfBoundsException());
    catalog.push_back(Declare_java_lang_StringIndexOutOfBoundsException());
    catalog.push_back(Declare_java_lang_ClassCastException());
    catalog.push_back(Declare_java_lang_NegativeArraySizeException());
    catalog.push_back(Declare_java_lang_ArrayStoreException());
    catalog.push_back(Declare_java_lang_IllegalMonitorStateException());
    catalog.push_back(Declare_java_lang_IllegalArgumentException());
    catalog.push_back(Declare_java_lang_IllegalStateException());
    catalog.push_back(Declare_java_lang_UnsupportedOperationException());
    catalog.push_back(Declare_java_lang_ClassNotFoundException());
    catalog.push_back(Declare_java_lang_InterruptedException());
    catalog.push_back(Declare_java_lang_NumberFormatException());
    catalog.push_back(Declare_java_lang_IllegalThreadStateException());
    catalog.push_back(Declare_java_lang_Error());
    catalog.push_back(Declare_java_lang_LinkageError());
    catalog.push_back(Declare_java_lang_NoClassDefFoundError());
    catalog.push_back(Declare_java_lang_UnsatisfiedLinkError());
    catalog.push_back(Declare_java_lang_VirtualMachineError());
    catalog.push_back(Declare_java_lang_StackOverflowError());
    catalog.push_back(Declare_java_lang_OutOfMemoryError());
    catalog.push_back(Declare_java_lang_AbstractMethodError());
    catalog.push_back(Declare_java_lang_AssertionError());
    catalog.push_back(Declare_java_lang_ClassCircularityError());
    catalog.push_back(Declare_java_lang_ClassFormatError());
    catalog.push_back(Declare_java_lang_CloneNotSupportedException());
    catalog.push_back(Declare_java_lang_EnumConstantNotPresentException());
    catalog.push_back(Declare_java_lang_ExceptionInInitializerError());
    catalog.push_back(Declare_java_lang_IllegalAccessError());
    catalog.push_back(Declare_java_lang_IllegalAccessException());
    catalog.push_back(Declare_java_lang_IncompatibleClassChangeError());
    catalog.push_back(Declare_java_lang_InstantiationError());
    catalog.push_back(Declare_java_lang_InstantiationException());
    catalog.push_back(Declare_java_lang_InternalError());
    catalog.push_back(Declare_java_lang_NoSuchFieldError());
    catalog.push_back(Declare_java_lang_NoSuchFieldException());
    catalog.push_back(Declare_java_lang_NoSuchMethodError());
    catalog.push_back(Declare_java_lang_NoSuchMethodException());
    catalog.push_back(Declare_java_lang_ReflectiveOperationException());
    catalog.push_back(Declare_java_lang_SecurityException());
    catalog.push_back(Declare_java_lang_ThreadDeath());
    catalog.push_back(Declare_java_lang_TypeNotPresentException());
    catalog.push_back(Declare_java_lang_UnknownError());
    catalog.push_back(Declare_java_lang_UnsupportedClassVersionError());
    catalog.push_back(Declare_java_lang_VerifyError());
}

}  // namespace ogplay::runtime::dexvm::intrinsics
