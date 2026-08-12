// Built-in java.* core intrinsic handlers (P1 language core). Semantics of
// native halves follow AOSP vm/native/java_lang_Object.cpp /
// java_lang_String.cpp at the pinned baseline; pure-library behaviour
// follows the class-library documentation (07 §2, libcore not vendored).

#include "interpreter_internal.h"

namespace ogplay::runtime::dexvm {
namespace {

[[nodiscard]] std::string DottedName(const std::string& descriptor) {
    if (descriptor.starts_with("L") && descriptor.ends_with(";")) {
        std::string name = descriptor.substr(1, descriptor.size() - 2);
        for (auto& character : name) {
            if (character == '/') character = '.';
        }
        return name;
    }
    return descriptor;
}

[[nodiscard]] std::int32_t JavaStringHash(const std::u16string& value) {
    std::int32_t hash = 0;
    for (const auto unit : value) {
        hash = static_cast<std::int32_t>(
            static_cast<std::uint32_t>(hash) * 31U + unit);
    }
    return hash;
}

}  // namespace

void RegisterCoreBuiltinHandlers(IntrinsicRegistry& registry) {
    registry.Register("core.object.init", [](IntrinsicContext&) {
        return VmValue::Void();
    });
    registry.Register("core.object.equals", [](IntrinsicContext& context) {
        const auto other = context.arguments.empty()
                               ? VmObjectRef{}
                               : context.arguments[0].ref;
        return VmValue::Int(context.receiver == other ? 1 : 0);
    });
    registry.Register("core.object.hash_code", [](IntrinsicContext& context) {
        return VmValue::Int(
            static_cast<std::int32_t>(context.receiver.Value()));
    });
    registry.Register("core.object.to_string", [](IntrinsicContext& context) {
        auto& vm = context.vm;
        const auto java_class = vm.Model().ObjectClass(context.receiver);
        const auto descriptor =
            java_class.IsValid() ? vm.Linker().Class(java_class).descriptor
                                 : std::string("<external>");
        return VmValue::Ref(vm.NewStringUtf8(
            DottedName(descriptor) + "@" +
            std::to_string(context.receiver.Value())));
    });
    registry.Register("core.object.get_class", [](IntrinsicContext& context) {
        auto& vm = context.vm;
        const auto java_class = vm.Model().ObjectClass(context.receiver);
        if (!java_class.IsValid()) {
            throw VmJavaThrow{"Ljava/lang/IllegalStateException;",
                              "object has no VM class"};
        }
        return VmValue::Ref(vm.Model().ClassObject(java_class));
    });

    registry.Register("core.string.length", [](IntrinsicContext& context) {
        return VmValue::Int(static_cast<std::int32_t>(
            context.vm.Model().StringValue(context.receiver).size()));
    });
    registry.Register("core.string.char_at", [](IntrinsicContext& context) {
        const auto value =
            context.vm.Model().StringValue(context.receiver);
        const auto index = context.arguments[0].AsInt();
        if (index < 0 || static_cast<std::size_t>(index) >= value.size()) {
            throw VmJavaThrow{
                "Ljava/lang/StringIndexOutOfBoundsException;",
                "index " + std::to_string(index)};
        }
        return VmValue::Int(value[static_cast<std::size_t>(index)]);
    });
    registry.Register("core.string.equals", [](IntrinsicContext& context) {
        const auto other = context.arguments[0].ref;
        if (!other.IsValid()) return VmValue::Int(0);
        auto& model = context.vm.Model();
        const auto other_kind = model.Kind(other);
        if (other_kind != VmObjectKind::string &&
            other_kind != VmObjectKind::external) {
            return VmValue::Int(0);
        }
        return VmValue::Int(model.StringValue(context.receiver) ==
                                    model.StringValue(other)
                                ? 1
                                : 0);
    });
    registry.Register("core.string.hash_code", [](IntrinsicContext& context) {
        return VmValue::Int(JavaStringHash(
            context.vm.Model().StringValue(context.receiver)));
    });
    registry.Register("core.string.to_string", [](IntrinsicContext& context) {
        return VmValue::Ref(context.receiver);
    });

    registry.Register("core.class.get_name", [](IntrinsicContext& context) {
        auto& vm = context.vm;
        const auto java_class =
            vm.Model().ClassOfClassObject(context.receiver);
        return VmValue::Ref(vm.NewStringUtf8(
            DottedName(vm.Linker().Class(java_class).descriptor)));
    });

    registry.Register("core.throwable.init", [](IntrinsicContext&) {
        return VmValue::Void();
    });
    registry.Register("core.throwable.init_message",
                      [](IntrinsicContext& context) {
        const auto message = context.arguments[0].ref;
        context.vm.SetThrowableMessage(context.receiver, message);
        return VmValue::Void();
    });
    registry.Register("core.throwable.get_message",
                      [](IntrinsicContext& context) {
        return VmValue::Ref(context.vm.ThrowableMessage(context.receiver));
    });
    registry.Register("core.throwable.to_string",
                      [](IntrinsicContext& context) {
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
}

}  // namespace ogplay::runtime::dexvm
