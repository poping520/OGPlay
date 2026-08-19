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
    auto builder = IntrinsicClassBuilder::Class("Ljava/lang/ArithmeticException;", "Ljava/lang/RuntimeException;");
    builder.Constructor("()V",
        [](IntrinsicContext &) { return VmValue::Void(); });
    builder.Constructor("(Ljava/lang/String;)V",
        [](IntrinsicContext& context) {
                const auto message = context.arguments[0].ref;
                context.vm.SetThrowableMessage(context.receiver, message);
                return VmValue::Void();
            });
    auto result = std::move(builder).Build();
    return result;
}

IntrinsicClassDecl Declare_java_lang_ArrayIndexOutOfBoundsException() {
    auto builder = IntrinsicClassBuilder::Class("Ljava/lang/ArrayIndexOutOfBoundsException;", "Ljava/lang/IndexOutOfBoundsException;");
    builder.Constructor("()V",
        [](IntrinsicContext &) { return VmValue::Void(); });
    builder.Constructor("(Ljava/lang/String;)V",
        [](IntrinsicContext& context) {
                const auto message = context.arguments[0].ref;
                context.vm.SetThrowableMessage(context.receiver, message);
                return VmValue::Void();
            });
    auto result = std::move(builder).Build();
    return result;
}

IntrinsicClassDecl Declare_java_lang_ArrayStoreException() {
    auto builder = IntrinsicClassBuilder::Class("Ljava/lang/ArrayStoreException;", "Ljava/lang/RuntimeException;");
    builder.Constructor("()V",
        [](IntrinsicContext &) { return VmValue::Void(); });
    builder.Constructor("(Ljava/lang/String;)V",
        [](IntrinsicContext& context) {
                const auto message = context.arguments[0].ref;
                context.vm.SetThrowableMessage(context.receiver, message);
                return VmValue::Void();
            });
    auto result = std::move(builder).Build();
    return result;
}

IntrinsicClassDecl Declare_java_lang_ClassCastException() {
    auto builder = IntrinsicClassBuilder::Class("Ljava/lang/ClassCastException;", "Ljava/lang/RuntimeException;");
    builder.Constructor("()V",
        [](IntrinsicContext &) { return VmValue::Void(); });
    builder.Constructor("(Ljava/lang/String;)V",
        [](IntrinsicContext& context) {
                const auto message = context.arguments[0].ref;
                context.vm.SetThrowableMessage(context.receiver, message);
                return VmValue::Void();
            });
    auto result = std::move(builder).Build();
    return result;
}

IntrinsicClassDecl Declare_java_lang_ClassNotFoundException() {
    auto builder = IntrinsicClassBuilder::Class("Ljava/lang/ClassNotFoundException;", "Ljava/lang/ReflectiveOperationException;");
    builder.InstanceField("ex", "Ljava/lang/Throwable;");
    builder.Constructor("()V",
        [](IntrinsicContext &) { return VmValue::Void(); });
    builder.Constructor("(Ljava/lang/String;)V",
        [](IntrinsicContext& context) {
                const auto message = context.arguments[0].ref;
                context.vm.SetThrowableMessage(context.receiver, message);
                return VmValue::Void();
            });
    builder.Constructor("(Ljava/lang/String;Ljava/lang/Throwable;)V",
        [](IntrinsicContext& context) {
                const auto message = context.arguments[0].ref;
                context.vm.SetThrowableMessage(context.receiver, message);
                SetThrowableRefField(context, "ex", "Ljava/lang/Throwable;",
                                     context.arguments[1].ref);
                return VmValue::Void();
            });
    builder.VirtualMethod("getException", "()Ljava/lang/Throwable;",
        [](IntrinsicContext& context) {
                return VmValue::Ref(GetThrowableRefField(
                    context, "ex", "Ljava/lang/Throwable;"));
            });
    builder.VirtualMethod("getCause", "()Ljava/lang/Throwable;",
        [](IntrinsicContext& context) {
                return VmValue::Ref(GetThrowableRefField(
                    context, "ex", "Ljava/lang/Throwable;"));
            });
    auto result = std::move(builder).Build();
    return result;
}

IntrinsicClassDecl Declare_java_lang_Error() {
    auto builder = IntrinsicClassBuilder::Class("Ljava/lang/Error;", "Ljava/lang/Throwable;");
    builder.Constructor("()V",
        [](IntrinsicContext &) { return VmValue::Void(); });
    builder.Constructor("(Ljava/lang/String;)V",
        [](IntrinsicContext& context) {
                const auto message = context.arguments[0].ref;
                context.vm.SetThrowableMessage(context.receiver, message);
                return VmValue::Void();
            });
    auto result = std::move(builder).Build();
    return result;
}

IntrinsicClassDecl Declare_java_lang_Exception() {
    auto builder = IntrinsicClassBuilder::Class("Ljava/lang/Exception;", "Ljava/lang/Throwable;");
    builder.Constructor("()V",
        [](IntrinsicContext &) { return VmValue::Void(); });
    builder.Constructor("(Ljava/lang/String;)V",
        [](IntrinsicContext& context) {
                const auto message = context.arguments[0].ref;
                context.vm.SetThrowableMessage(context.receiver, message);
                return VmValue::Void();
            });
    auto result = std::move(builder).Build();
    return result;
}

IntrinsicClassDecl Declare_java_lang_IllegalArgumentException() {
    auto builder = IntrinsicClassBuilder::Class("Ljava/lang/IllegalArgumentException;", "Ljava/lang/RuntimeException;");
    builder.Constructor("()V",
        [](IntrinsicContext &) { return VmValue::Void(); });
    builder.Constructor("(Ljava/lang/String;)V",
        [](IntrinsicContext& context) {
                const auto message = context.arguments[0].ref;
                context.vm.SetThrowableMessage(context.receiver, message);
                return VmValue::Void();
            });
    auto result = std::move(builder).Build();
    return result;
}

IntrinsicClassDecl Declare_java_lang_IllegalMonitorStateException() {
    auto builder = IntrinsicClassBuilder::Class("Ljava/lang/IllegalMonitorStateException;", "Ljava/lang/RuntimeException;");
    builder.Constructor("()V",
        [](IntrinsicContext &) { return VmValue::Void(); });
    builder.Constructor("(Ljava/lang/String;)V",
        [](IntrinsicContext& context) {
                const auto message = context.arguments[0].ref;
                context.vm.SetThrowableMessage(context.receiver, message);
                return VmValue::Void();
            });
    auto result = std::move(builder).Build();
    return result;
}

IntrinsicClassDecl Declare_java_lang_IllegalStateException() {
    auto builder = IntrinsicClassBuilder::Class("Ljava/lang/IllegalStateException;", "Ljava/lang/RuntimeException;");
    builder.Constructor("()V",
        [](IntrinsicContext &) { return VmValue::Void(); });
    builder.Constructor("(Ljava/lang/String;)V",
        [](IntrinsicContext& context) {
                const auto message = context.arguments[0].ref;
                context.vm.SetThrowableMessage(context.receiver, message);
                return VmValue::Void();
            });
    auto result = std::move(builder).Build();
    return result;
}

IntrinsicClassDecl Declare_java_lang_IllegalThreadStateException() {
    auto builder = IntrinsicClassBuilder::Class("Ljava/lang/IllegalThreadStateException;", "Ljava/lang/IllegalArgumentException;");
    builder.Constructor("()V",
        [](IntrinsicContext &) { return VmValue::Void(); });
    builder.Constructor("(Ljava/lang/String;)V",
        [](IntrinsicContext& context) {
                const auto message = context.arguments[0].ref;
                context.vm.SetThrowableMessage(context.receiver, message);
                return VmValue::Void();
            });
    auto result = std::move(builder).Build();
    return result;
}

IntrinsicClassDecl Declare_java_lang_IndexOutOfBoundsException() {
    auto builder = IntrinsicClassBuilder::Class("Ljava/lang/IndexOutOfBoundsException;", "Ljava/lang/RuntimeException;");
    builder.Constructor("()V",
        [](IntrinsicContext &) { return VmValue::Void(); });
    builder.Constructor("(Ljava/lang/String;)V",
        [](IntrinsicContext& context) {
                const auto message = context.arguments[0].ref;
                context.vm.SetThrowableMessage(context.receiver, message);
                return VmValue::Void();
            });
    auto result = std::move(builder).Build();
    return result;
}

IntrinsicClassDecl Declare_java_lang_InterruptedException() {
    auto builder = IntrinsicClassBuilder::Class("Ljava/lang/InterruptedException;", "Ljava/lang/Exception;");
    builder.Constructor("()V",
        [](IntrinsicContext &) { return VmValue::Void(); });
    builder.Constructor("(Ljava/lang/String;)V",
        [](IntrinsicContext& context) {
                const auto message = context.arguments[0].ref;
                context.vm.SetThrowableMessage(context.receiver, message);
                return VmValue::Void();
            });
    auto result = std::move(builder).Build();
    return result;
}

IntrinsicClassDecl Declare_java_lang_LinkageError() {
    auto builder = IntrinsicClassBuilder::Class("Ljava/lang/LinkageError;", "Ljava/lang/Error;");
    builder.Constructor("()V",
        [](IntrinsicContext &) { return VmValue::Void(); });
    builder.Constructor("(Ljava/lang/String;)V",
        [](IntrinsicContext& context) {
                const auto message = context.arguments[0].ref;
                context.vm.SetThrowableMessage(context.receiver, message);
                return VmValue::Void();
            });
    auto result = std::move(builder).Build();
    return result;
}

IntrinsicClassDecl Declare_java_lang_NegativeArraySizeException() {
    auto builder = IntrinsicClassBuilder::Class("Ljava/lang/NegativeArraySizeException;", "Ljava/lang/RuntimeException;");
    builder.Constructor("()V",
        [](IntrinsicContext &) { return VmValue::Void(); });
    builder.Constructor("(Ljava/lang/String;)V",
        [](IntrinsicContext& context) {
                const auto message = context.arguments[0].ref;
                context.vm.SetThrowableMessage(context.receiver, message);
                return VmValue::Void();
            });
    auto result = std::move(builder).Build();
    return result;
}

IntrinsicClassDecl Declare_java_lang_NoClassDefFoundError() {
    auto builder = IntrinsicClassBuilder::Class("Ljava/lang/NoClassDefFoundError;", "Ljava/lang/LinkageError;");
    builder.Constructor("()V",
        [](IntrinsicContext &) { return VmValue::Void(); });
    builder.Constructor("(Ljava/lang/String;)V",
        [](IntrinsicContext& context) {
                const auto message = context.arguments[0].ref;
                context.vm.SetThrowableMessage(context.receiver, message);
                return VmValue::Void();
            });
    auto result = std::move(builder).Build();
    return result;
}

IntrinsicClassDecl Declare_java_lang_NullPointerException() {
    auto builder = IntrinsicClassBuilder::Class("Ljava/lang/NullPointerException;", "Ljava/lang/RuntimeException;");
    builder.Constructor("()V",
        [](IntrinsicContext &) { return VmValue::Void(); });
    builder.Constructor("(Ljava/lang/String;)V",
        [](IntrinsicContext& context) {
                const auto message = context.arguments[0].ref;
                context.vm.SetThrowableMessage(context.receiver, message);
                return VmValue::Void();
            });
    auto result = std::move(builder).Build();
    return result;
}

IntrinsicClassDecl Declare_java_lang_NumberFormatException() {
    auto builder = IntrinsicClassBuilder::Class("Ljava/lang/NumberFormatException;", "Ljava/lang/IllegalArgumentException;");
    builder.Constructor("()V",
        [](IntrinsicContext &) { return VmValue::Void(); });
    builder.Constructor("(Ljava/lang/String;)V",
        [](IntrinsicContext& context) {
                const auto message = context.arguments[0].ref;
                context.vm.SetThrowableMessage(context.receiver, message);
                return VmValue::Void();
            });
    auto result = std::move(builder).Build();
    return result;
}

IntrinsicClassDecl Declare_java_lang_OutOfMemoryError() {
    auto builder = IntrinsicClassBuilder::Class("Ljava/lang/OutOfMemoryError;", "Ljava/lang/VirtualMachineError;");
    builder.Constructor("()V",
        [](IntrinsicContext &) { return VmValue::Void(); });
    builder.Constructor("(Ljava/lang/String;)V",
        [](IntrinsicContext& context) {
                const auto message = context.arguments[0].ref;
                context.vm.SetThrowableMessage(context.receiver, message);
                return VmValue::Void();
            });
    auto result = std::move(builder).Build();
    return result;
}

IntrinsicClassDecl Declare_java_lang_RuntimeException() {
    auto builder = IntrinsicClassBuilder::Class("Ljava/lang/RuntimeException;", "Ljava/lang/Exception;");
    builder.Constructor("()V",
        [](IntrinsicContext &) { return VmValue::Void(); });
    builder.Constructor("(Ljava/lang/String;)V",
        [](IntrinsicContext& context) {
                const auto message = context.arguments[0].ref;
                context.vm.SetThrowableMessage(context.receiver, message);
                return VmValue::Void();
            });
    auto result = std::move(builder).Build();
    return result;
}

IntrinsicClassDecl Declare_java_lang_StackOverflowError() {
    auto builder = IntrinsicClassBuilder::Class("Ljava/lang/StackOverflowError;", "Ljava/lang/VirtualMachineError;");
    builder.Constructor("()V",
        [](IntrinsicContext &) { return VmValue::Void(); });
    builder.Constructor("(Ljava/lang/String;)V",
        [](IntrinsicContext& context) {
                const auto message = context.arguments[0].ref;
                context.vm.SetThrowableMessage(context.receiver, message);
                return VmValue::Void();
            });
    auto result = std::move(builder).Build();
    return result;
}

IntrinsicClassDecl Declare_java_lang_StringIndexOutOfBoundsException() {
    auto builder = IntrinsicClassBuilder::Class("Ljava/lang/StringIndexOutOfBoundsException;", "Ljava/lang/IndexOutOfBoundsException;");
    builder.Constructor("()V",
        [](IntrinsicContext &) { return VmValue::Void(); });
    builder.Constructor("(Ljava/lang/String;)V",
        [](IntrinsicContext& context) {
                const auto message = context.arguments[0].ref;
                context.vm.SetThrowableMessage(context.receiver, message);
                return VmValue::Void();
            });
    auto result = std::move(builder).Build();
    return result;
}

IntrinsicClassDecl Declare_java_lang_Throwable() {
    auto builder = IntrinsicClassBuilder::Class("Ljava/lang/Throwable;", "Ljava/lang/Object;", {"Ljava/io/Serializable;"});
    builder.Constructor("()V",
        [](IntrinsicContext &) { return VmValue::Void(); });
    builder.Constructor("(Ljava/lang/String;)V",
        [](IntrinsicContext& context) {
                const auto message = context.arguments[0].ref;
                context.vm.SetThrowableMessage(context.receiver, message);
                return VmValue::Void();
            });
    builder.VirtualMethod("getMessage", "()Ljava/lang/String;",
        [](IntrinsicContext &context) {
                return VmValue::Ref(context.vm.ThrowableMessage(context.receiver));
            });
    builder.VirtualMethod("toString", "()Ljava/lang/String;",
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
    builder.FinalMethod("printStackTrace", "()V",
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
    auto builder = IntrinsicClassBuilder::Class("Ljava/lang/UnsatisfiedLinkError;", "Ljava/lang/LinkageError;");
    builder.Constructor("()V",
        [](IntrinsicContext &) { return VmValue::Void(); });
    builder.Constructor("(Ljava/lang/String;)V",
        [](IntrinsicContext& context) {
                const auto message = context.arguments[0].ref;
                context.vm.SetThrowableMessage(context.receiver, message);
                return VmValue::Void();
            });
    auto result = std::move(builder).Build();
    return result;
}

IntrinsicClassDecl Declare_java_lang_UnsupportedOperationException() {
    auto builder = IntrinsicClassBuilder::Class("Ljava/lang/UnsupportedOperationException;", "Ljava/lang/RuntimeException;");
    builder.Constructor("()V",
        [](IntrinsicContext &) { return VmValue::Void(); });
    builder.Constructor("(Ljava/lang/String;)V",
        [](IntrinsicContext& context) {
                const auto message = context.arguments[0].ref;
                context.vm.SetThrowableMessage(context.receiver, message);
                return VmValue::Void();
            });
    auto result = std::move(builder).Build();
    return result;
}

IntrinsicClassDecl Declare_java_lang_VirtualMachineError() {
    auto builder = IntrinsicClassBuilder::Class("Ljava/lang/VirtualMachineError;", "Ljava/lang/Error;");
    builder.Constructor("()V",
        [](IntrinsicContext &) { return VmValue::Void(); });
    builder.Constructor("(Ljava/lang/String;)V",
        [](IntrinsicContext& context) {
                const auto message = context.arguments[0].ref;
                context.vm.SetThrowableMessage(context.receiver, message);
                return VmValue::Void();
            });
    auto result = std::move(builder).Build();
    return result;
}

IntrinsicClassDecl Declare_java_lang_AbstractMethodError() {
    auto builder = IntrinsicClassBuilder::Class("Ljava/lang/AbstractMethodError;", "Ljava/lang/IncompatibleClassChangeError;");
    builder.Constructor("()V",
        [](IntrinsicContext &) { return VmValue::Void(); });
    builder.Constructor("(Ljava/lang/String;)V",
        [](IntrinsicContext& context) {
                const auto message = context.arguments[0].ref;
                context.vm.SetThrowableMessage(context.receiver, message);
                return VmValue::Void();
            });
    auto result = std::move(builder).Build();
    return result;
}

IntrinsicClassDecl Declare_java_lang_ClassCircularityError() {
    auto builder = IntrinsicClassBuilder::Class("Ljava/lang/ClassCircularityError;", "Ljava/lang/LinkageError;");
    builder.Constructor("()V",
        [](IntrinsicContext &) { return VmValue::Void(); });
    builder.Constructor("(Ljava/lang/String;)V",
        [](IntrinsicContext& context) {
                const auto message = context.arguments[0].ref;
                context.vm.SetThrowableMessage(context.receiver, message);
                return VmValue::Void();
            });
    auto result = std::move(builder).Build();
    return result;
}

IntrinsicClassDecl Declare_java_lang_ClassFormatError() {
    auto builder = IntrinsicClassBuilder::Class("Ljava/lang/ClassFormatError;", "Ljava/lang/LinkageError;");
    builder.Constructor("()V",
        [](IntrinsicContext &) { return VmValue::Void(); });
    builder.Constructor("(Ljava/lang/String;)V",
        [](IntrinsicContext& context) {
                const auto message = context.arguments[0].ref;
                context.vm.SetThrowableMessage(context.receiver, message);
                return VmValue::Void();
            });
    auto result = std::move(builder).Build();
    return result;
}

IntrinsicClassDecl Declare_java_lang_CloneNotSupportedException() {
    auto builder = IntrinsicClassBuilder::Class("Ljava/lang/CloneNotSupportedException;", "Ljava/lang/Exception;");
    builder.Constructor("()V",
        [](IntrinsicContext &) { return VmValue::Void(); });
    builder.Constructor("(Ljava/lang/String;)V",
        [](IntrinsicContext& context) {
                const auto message = context.arguments[0].ref;
                context.vm.SetThrowableMessage(context.receiver, message);
                return VmValue::Void();
            });
    auto result = std::move(builder).Build();
    return result;
}

IntrinsicClassDecl Declare_java_lang_IllegalAccessError() {
    auto builder = IntrinsicClassBuilder::Class("Ljava/lang/IllegalAccessError;", "Ljava/lang/IncompatibleClassChangeError;");
    builder.Constructor("()V",
        [](IntrinsicContext &) { return VmValue::Void(); });
    builder.Constructor("(Ljava/lang/String;)V",
        [](IntrinsicContext& context) {
                const auto message = context.arguments[0].ref;
                context.vm.SetThrowableMessage(context.receiver, message);
                return VmValue::Void();
            });
    auto result = std::move(builder).Build();
    return result;
}

IntrinsicClassDecl Declare_java_lang_IllegalAccessException() {
    auto builder = IntrinsicClassBuilder::Class("Ljava/lang/IllegalAccessException;", "Ljava/lang/ReflectiveOperationException;");
    builder.Constructor("()V",
        [](IntrinsicContext &) { return VmValue::Void(); });
    builder.Constructor("(Ljava/lang/String;)V",
        [](IntrinsicContext& context) {
                const auto message = context.arguments[0].ref;
                context.vm.SetThrowableMessage(context.receiver, message);
                return VmValue::Void();
            });
    auto result = std::move(builder).Build();
    return result;
}

IntrinsicClassDecl Declare_java_lang_IncompatibleClassChangeError() {
    auto builder = IntrinsicClassBuilder::Class("Ljava/lang/IncompatibleClassChangeError;", "Ljava/lang/LinkageError;");
    builder.Constructor("()V",
        [](IntrinsicContext &) { return VmValue::Void(); });
    builder.Constructor("(Ljava/lang/String;)V",
        [](IntrinsicContext& context) {
                const auto message = context.arguments[0].ref;
                context.vm.SetThrowableMessage(context.receiver, message);
                return VmValue::Void();
            });
    auto result = std::move(builder).Build();
    return result;
}

IntrinsicClassDecl Declare_java_lang_InternalError() {
    auto builder = IntrinsicClassBuilder::Class("Ljava/lang/InternalError;", "Ljava/lang/VirtualMachineError;");
    builder.Constructor("()V",
        [](IntrinsicContext &) { return VmValue::Void(); });
    builder.Constructor("(Ljava/lang/String;)V",
        [](IntrinsicContext& context) {
                const auto message = context.arguments[0].ref;
                context.vm.SetThrowableMessage(context.receiver, message);
                return VmValue::Void();
            });
    auto result = std::move(builder).Build();
    return result;
}

IntrinsicClassDecl Declare_java_lang_NoSuchFieldError() {
    auto builder = IntrinsicClassBuilder::Class("Ljava/lang/NoSuchFieldError;", "Ljava/lang/IncompatibleClassChangeError;");
    builder.Constructor("()V",
        [](IntrinsicContext &) { return VmValue::Void(); });
    builder.Constructor("(Ljava/lang/String;)V",
        [](IntrinsicContext& context) {
                const auto message = context.arguments[0].ref;
                context.vm.SetThrowableMessage(context.receiver, message);
                return VmValue::Void();
            });
    auto result = std::move(builder).Build();
    return result;
}

IntrinsicClassDecl Declare_java_lang_NoSuchFieldException() {
    auto builder = IntrinsicClassBuilder::Class("Ljava/lang/NoSuchFieldException;", "Ljava/lang/ReflectiveOperationException;");
    builder.Constructor("()V",
        [](IntrinsicContext &) { return VmValue::Void(); });
    builder.Constructor("(Ljava/lang/String;)V",
        [](IntrinsicContext& context) {
                const auto message = context.arguments[0].ref;
                context.vm.SetThrowableMessage(context.receiver, message);
                return VmValue::Void();
            });
    auto result = std::move(builder).Build();
    return result;
}

IntrinsicClassDecl Declare_java_lang_NoSuchMethodError() {
    auto builder = IntrinsicClassBuilder::Class("Ljava/lang/NoSuchMethodError;", "Ljava/lang/IncompatibleClassChangeError;");
    builder.Constructor("()V",
        [](IntrinsicContext &) { return VmValue::Void(); });
    builder.Constructor("(Ljava/lang/String;)V",
        [](IntrinsicContext& context) {
                const auto message = context.arguments[0].ref;
                context.vm.SetThrowableMessage(context.receiver, message);
                return VmValue::Void();
            });
    auto result = std::move(builder).Build();
    return result;
}

IntrinsicClassDecl Declare_java_lang_NoSuchMethodException() {
    auto builder = IntrinsicClassBuilder::Class("Ljava/lang/NoSuchMethodException;", "Ljava/lang/ReflectiveOperationException;");
    builder.Constructor("()V",
        [](IntrinsicContext &) { return VmValue::Void(); });
    builder.Constructor("(Ljava/lang/String;)V",
        [](IntrinsicContext& context) {
                const auto message = context.arguments[0].ref;
                context.vm.SetThrowableMessage(context.receiver, message);
                return VmValue::Void();
            });
    auto result = std::move(builder).Build();
    return result;
}

IntrinsicClassDecl Declare_java_lang_UnknownError() {
    auto builder = IntrinsicClassBuilder::Class("Ljava/lang/UnknownError;", "Ljava/lang/VirtualMachineError;");
    builder.Constructor("()V",
        [](IntrinsicContext &) { return VmValue::Void(); });
    builder.Constructor("(Ljava/lang/String;)V",
        [](IntrinsicContext& context) {
                const auto message = context.arguments[0].ref;
                context.vm.SetThrowableMessage(context.receiver, message);
                return VmValue::Void();
            });
    auto result = std::move(builder).Build();
    return result;
}

IntrinsicClassDecl Declare_java_lang_UnsupportedClassVersionError() {
    auto builder = IntrinsicClassBuilder::Class("Ljava/lang/UnsupportedClassVersionError;", "Ljava/lang/ClassFormatError;");
    builder.Constructor("()V",
        [](IntrinsicContext &) { return VmValue::Void(); });
    builder.Constructor("(Ljava/lang/String;)V",
        [](IntrinsicContext& context) {
                const auto message = context.arguments[0].ref;
                context.vm.SetThrowableMessage(context.receiver, message);
                return VmValue::Void();
            });
    auto result = std::move(builder).Build();
    return result;
}

IntrinsicClassDecl Declare_java_lang_VerifyError() {
    auto builder = IntrinsicClassBuilder::Class("Ljava/lang/VerifyError;", "Ljava/lang/LinkageError;");
    builder.Constructor("()V",
        [](IntrinsicContext &) { return VmValue::Void(); });
    builder.Constructor("(Ljava/lang/String;)V",
        [](IntrinsicContext& context) {
                const auto message = context.arguments[0].ref;
                context.vm.SetThrowableMessage(context.receiver, message);
                return VmValue::Void();
            });
    auto result = std::move(builder).Build();
    return result;
}

IntrinsicClassDecl Declare_java_lang_AssertionError() {
    auto builder = IntrinsicClassBuilder::Class("Ljava/lang/AssertionError;", "Ljava/lang/Error;");
    builder.Constructor("()V",
        [](IntrinsicContext &) { return VmValue::Void(); });
    builder.UnimplementedConstructor("(Ljava/lang/String;Ljava/lang/Throwable;)V");
    builder.UnimplementedConstructor("(Ljava/lang/Object;)V");
    builder.UnimplementedConstructor("(Z)V");
    builder.UnimplementedConstructor("(C)V");
    builder.UnimplementedConstructor("(I)V");
    builder.UnimplementedConstructor("(J)V");
    builder.UnimplementedConstructor("(F)V");
    builder.UnimplementedConstructor("(D)V");
    auto result = std::move(builder).Build();
    return result;
}

IntrinsicClassDecl Declare_java_lang_EnumConstantNotPresentException() {
    auto builder = IntrinsicClassBuilder::Class("Ljava/lang/EnumConstantNotPresentException;", "Ljava/lang/RuntimeException;");
    builder.InstanceField("enumType", "Ljava/lang/Class;");
    builder.InstanceField("constantName", "Ljava/lang/String;");
    builder.Constructor("(Ljava/lang/Class;Ljava/lang/String;)V",
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
    builder.VirtualMethod("enumType", "()Ljava/lang/Class;",
        [](IntrinsicContext& context) {
                return VmValue::Ref(GetThrowableRefField(
                    context, "enumType", "Ljava/lang/Class;"));
            });
    builder.VirtualMethod("constantName", "()Ljava/lang/String;",
        [](IntrinsicContext& context) {
                return VmValue::Ref(GetThrowableRefField(
                    context, "constantName", "Ljava/lang/String;"));
            });
    auto result = std::move(builder).Build();
    return result;
}

IntrinsicClassDecl Declare_java_lang_ExceptionInInitializerError() {
    auto builder = IntrinsicClassBuilder::Class("Ljava/lang/ExceptionInInitializerError;", "Ljava/lang/LinkageError;");
    builder.InstanceField("exception", "Ljava/lang/Throwable;");
    builder.Constructor("()V",
        [](IntrinsicContext &) { return VmValue::Void(); });
    builder.Constructor("(Ljava/lang/String;)V",
        [](IntrinsicContext& context) {
                const auto message = context.arguments[0].ref;
                context.vm.SetThrowableMessage(context.receiver, message);
                return VmValue::Void();
            });
    builder.Constructor("(Ljava/lang/Throwable;)V",
        [](IntrinsicContext& context) {
                SetThrowableRefField(context, "exception",
                                     "Ljava/lang/Throwable;",
                                     context.arguments[0].ref);
                return VmValue::Void();
            });
    builder.VirtualMethod("getException", "()Ljava/lang/Throwable;",
        [](IntrinsicContext& context) {
                return VmValue::Ref(GetThrowableRefField(
                    context, "exception", "Ljava/lang/Throwable;"));
            });
    builder.VirtualMethod("getCause", "()Ljava/lang/Throwable;",
        [](IntrinsicContext& context) {
                return VmValue::Ref(GetThrowableRefField(
                    context, "exception", "Ljava/lang/Throwable;"));
            });
    auto result = std::move(builder).Build();
    return result;
}

IntrinsicClassDecl Declare_java_lang_ReflectiveOperationException() {
    auto builder = IntrinsicClassBuilder::Class("Ljava/lang/ReflectiveOperationException;", "Ljava/lang/Exception;");
    builder.Constructor("()V",
        [](IntrinsicContext &) { return VmValue::Void(); });
    builder.Constructor("(Ljava/lang/String;)V",
        [](IntrinsicContext& context) {
                const auto message = context.arguments[0].ref;
                context.vm.SetThrowableMessage(context.receiver, message);
                return VmValue::Void();
            });
    builder.UnimplementedConstructor("(Ljava/lang/Throwable;)V");
    builder.UnimplementedConstructor("(Ljava/lang/String;Ljava/lang/Throwable;)V");
    auto result = std::move(builder).Build();
    return result;
}

IntrinsicClassDecl Declare_java_lang_TypeNotPresentException() {
    auto builder = IntrinsicClassBuilder::Class("Ljava/lang/TypeNotPresentException;", "Ljava/lang/RuntimeException;");
    builder.InstanceField("typeName", "Ljava/lang/String;");
    builder.UnimplementedConstructor("(Ljava/lang/String;Ljava/lang/Throwable;)V");
    builder.VirtualMethod("typeName", "()Ljava/lang/String;",
        [](IntrinsicContext& context) {
                return VmValue::Ref(GetThrowableRefField(
                    context, "typeName", "Ljava/lang/String;"));
            });
    auto result = std::move(builder).Build();
    return result;
}

IntrinsicClassDecl Declare_java_lang_SecurityException() {
    auto builder = IntrinsicClassBuilder::Class("Ljava/lang/SecurityException;", "Ljava/lang/RuntimeException;");
    builder.Constructor("()V",
        [](IntrinsicContext &) { return VmValue::Void(); });
    builder.Constructor("(Ljava/lang/String;)V",
        [](IntrinsicContext& context) {
                const auto message = context.arguments[0].ref;
                context.vm.SetThrowableMessage(context.receiver, message);
                return VmValue::Void();
            });
    builder.UnimplementedConstructor("(Ljava/lang/String;Ljava/lang/Throwable;)V");
    builder.UnimplementedConstructor("(Ljava/lang/Throwable;)V");
    auto result = std::move(builder).Build();
    return result;
}

IntrinsicClassDecl Declare_java_lang_ThreadDeath() {
    auto builder = IntrinsicClassBuilder::Class("Ljava/lang/ThreadDeath;", "Ljava/lang/Error;");
    builder.Constructor("()V",
        [](IntrinsicContext &) { return VmValue::Void(); });
    auto result = std::move(builder).Build();
    return result;
}

IntrinsicClassDecl Declare_java_lang_InstantiationError() {
    auto builder = IntrinsicClassBuilder::Class("Ljava/lang/InstantiationError;", "Ljava/lang/IncompatibleClassChangeError;");
    builder.Constructor("()V",
        [](IntrinsicContext &) { return VmValue::Void(); });
    builder.Constructor("(Ljava/lang/String;)V",
        [](IntrinsicContext& context) {
                const auto message = context.arguments[0].ref;
                context.vm.SetThrowableMessage(context.receiver, message);
                return VmValue::Void();
            });
    builder.Constructor("(Ljava/lang/Class;)V",
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
    auto builder = IntrinsicClassBuilder::Class("Ljava/lang/InstantiationException;", "Ljava/lang/ReflectiveOperationException;");
    builder.Constructor("()V",
        [](IntrinsicContext &) { return VmValue::Void(); });
    builder.Constructor("(Ljava/lang/String;)V",
        [](IntrinsicContext& context) {
                const auto message = context.arguments[0].ref;
                context.vm.SetThrowableMessage(context.receiver, message);
                return VmValue::Void();
            });
    builder.Constructor("(Ljava/lang/Class;)V",
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
