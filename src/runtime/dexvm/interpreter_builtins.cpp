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
      if (character == '/')
        character = '.';
        }
        return name;
    }
    // Class.getName answers the Java keyword for primitive classes
    // (Float.TYPE.getName() == "float"); array descriptors stay as-is.
    switch (descriptor.size() == 1 ? descriptor[0] : '\0') {
  case 'Z':
    return "boolean";
  case 'B':
    return "byte";
  case 'C':
    return "char";
  case 'S':
    return "short";
  case 'I':
    return "int";
  case 'J':
    return "long";
  case 'F':
    return "float";
  case 'D':
    return "double";
  case 'V':
    return "void";
  default:
    break;
    }
    return descriptor;
}

[[nodiscard]] bool IsPrimitiveDescriptor(const std::string& descriptor) {
    return descriptor.size() == 1 &&
           std::string_view("ZBSCIJFD").find(descriptor[0]) !=
               std::string_view::npos;
}

[[nodiscard]] JniPrimitiveKind PrimitiveKindFor(const std::string& element) {
    switch (element[0]) {
  case 'Z':
    return JniPrimitiveKind::boolean;
  case 'B':
    return JniPrimitiveKind::byte;
  case 'C':
    return JniPrimitiveKind::character;
  case 'S':
    return JniPrimitiveKind::short_integer;
  case 'I':
    return JniPrimitiveKind::integer;
  case 'J':
    return JniPrimitiveKind::long_integer;
  case 'F':
    return JniPrimitiveKind::float_value;
  default:
    return JniPrimitiveKind::double_value;
    }
}

// Recursive builder for Array.newInstance: each level allocates the real
// typed array so subsequent check-cast and aget/aput see honest classes.
[[nodiscard]] VmObjectRef
BuildReflectArray(Interpreter &vm, const std::string &element_descriptor,
    const std::span<const std::int32_t> dims) {
    std::string descriptor(dims.size(), '[');
    descriptor += element_descriptor;
    const auto array_class = vm.Linker().ResolveDescriptor(descriptor);
    if (dims.size() == 1 && IsPrimitiveDescriptor(element_descriptor)) {
        return vm.Model().NewPrimitiveArray(
            array_class, PrimitiveKindFor(element_descriptor), dims[0]);
    }
    const auto element_class =
        vm.Linker().ResolveDescriptor(descriptor.substr(1));
    const auto array =
        vm.Model().NewObjectArray(array_class, element_class, dims[0]);
    if (dims.size() > 1) {
        for (std::int32_t index = 0; index < dims[0]; ++index) {
            vm.Model().SetObjectElement(
                array, index,
                BuildReflectArray(vm, element_descriptor, dims.subspan(1)));
        }
    }
    return array;
}

[[nodiscard]] std::int32_t JavaStringHash(const std::u16string& value) {
    std::int32_t hash = 0;
    for (const auto unit : value) {
    hash = static_cast<std::int32_t>(static_cast<std::uint32_t>(hash) * 31U +
                                     unit);
    }
    return hash;
}

}  // namespace

void RegisterCoreBuiltinHandlers(IntrinsicRegistry& registry) {
  registry.Register("core.object.init",
                    [](IntrinsicContext &) { return VmValue::Void(); });
    registry.Register("core.object.equals", [](IntrinsicContext& context) {
    const auto other =
        context.arguments.empty() ? VmObjectRef{} : context.arguments[0].ref;
        return VmValue::Int(context.receiver == other ? 1 : 0);
    });
    registry.Register("core.object.hash_code", [](IntrinsicContext& context) {
    return VmValue::Int(static_cast<std::int32_t>(context.receiver.Value()));
    });
    registry.Register("core.object.to_string", [](IntrinsicContext& context) {
        auto& vm = context.vm;
        const auto java_class = vm.Model().ObjectClass(context.receiver);
    const auto descriptor = java_class.IsValid()
                                ? vm.Linker().Class(java_class).descriptor
                                 : std::string("<external>");
    return VmValue::Ref(
        vm.NewStringUtf8(DottedName(descriptor) + "@" +
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
  registry.Register("core.object.notify", [](IntrinsicContext &context) {
    context.vm.NotifyMonitor(context.receiver, false);
    return VmValue::Void();
  });
  registry.Register("core.object.notify_all", [](IntrinsicContext &context) {
    context.vm.NotifyMonitor(context.receiver, true);
    return VmValue::Void();
  });

    registry.Register("core.string.length", [](IntrinsicContext& context) {
        return VmValue::Int(static_cast<std::int32_t>(
            context.vm.Model().StringValue(context.receiver).size()));
    });
    registry.Register("core.string.char_at", [](IntrinsicContext& context) {
    const auto value = context.vm.Model().StringValue(context.receiver);
        const auto index = context.arguments[0].AsInt();
        if (index < 0 || static_cast<std::size_t>(index) >= value.size()) {
      throw VmJavaThrow{"Ljava/lang/StringIndexOutOfBoundsException;",
                "index " + std::to_string(index)};
        }
        return VmValue::Int(value[static_cast<std::size_t>(index)]);
    });
    registry.Register("core.string.equals", [](IntrinsicContext& context) {
        const auto other = context.arguments[0].ref;
    if (!other.IsValid())
      return VmValue::Int(0);
        auto& model = context.vm.Model();
        const auto other_kind = model.Kind(other);
        if (other_kind != VmObjectKind::string &&
            other_kind != VmObjectKind::external) {
            return VmValue::Int(0);
        }
    return VmValue::Int(
        model.StringValue(context.receiver) == model.StringValue(other) ? 1
                                : 0);
    });
    registry.Register("core.string.hash_code", [](IntrinsicContext& context) {
    return VmValue::Int(
        JavaStringHash(context.vm.Model().StringValue(context.receiver)));
    });
    registry.Register("core.string.to_string", [](IntrinsicContext& context) {
        return VmValue::Ref(context.receiver);
    });

    registry.Register("core.class.get_name", [](IntrinsicContext& context) {
        auto& vm = context.vm;
    const auto java_class = vm.Model().ClassOfClassObject(context.receiver);
    return VmValue::Ref(
        vm.NewStringUtf8(DottedName(vm.Linker().Class(java_class).descriptor)));
  });
  registry.Register(
      "core.class.get_declared_methods", [](IntrinsicContext &context) {
        auto &vm = context.vm;
        const auto represented =
            vm.Model().ClassOfClassObject(context.receiver);
        std::vector<VmMethodId> declared;
        for (const auto method_id : vm.Linker().MethodsOf(represented)) {
          const auto &method = vm.Linker().Method(method_id);
          if (method.name != "<init>" && method.name != "<clinit>") {
            declared.push_back(method_id);
          }
        }
        const auto method_class =
            vm.Linker().ResolveDescriptor("Ljava/lang/reflect/Method;");
        const auto array_class =
            vm.Linker().ResolveDescriptor("[Ljava/lang/reflect/Method;");
        const auto array = vm.Model().NewObjectArray(
            array_class, method_class, static_cast<JniSize>(declared.size()));
        for (std::size_t index = 0; index < declared.size(); ++index) {
          const auto reflected =
              vm.NewIntrinsicInstance("Ljava/lang/reflect/Method;");
          const auto slots = vm.Model().InstanceSlots(reflected);
          slots[0] = {declared[index].Value(), SlotTag::cat1};
          slots[1] = {vm.NewStringUtf8(vm.Linker().Method(declared[index]).name)
                          .Value(),
                      SlotTag::ref};
          vm.Model().SetObjectElement(array, static_cast<JniSize>(index),
                                      reflected);
        }
        return VmValue::Ref(array);
      });
  registry.Register(
      "core.reflect.method_get_name", [](IntrinsicContext &context) {
        const auto slots = context.vm.Model().InstanceSlots(context.receiver);
        return VmValue::Ref(VmObjectRef(slots[1].bits));
      });
  registry.Register(
      "core.reflect.method_invoke", [](IntrinsicContext &context) {
        auto &vm = context.vm;
        const auto slots = vm.Model().InstanceSlots(context.receiver);
        const auto method_id = VmMethodId(slots[0].bits);
        const auto &method = vm.Linker().Method(method_id);
        const auto arguments = context.arguments[1].ref;
        if (!arguments.IsValid() || vm.Model().ArrayLength(arguments) != 0) {
          throw VmJavaThrow{
              "Ljava/lang/UnsupportedOperationException;",
              "reflective Method.invoke supports zero arguments only"};
        }
        if (method.return_shorty != 'I' && method.return_shorty != 'Z' &&
            method.return_shorty != 'B' && method.return_shorty != 'C' &&
            method.return_shorty != 'S') {
          throw VmJavaThrow{
              "Ljava/lang/UnsupportedOperationException;",
              "reflective Method.invoke supports int-like returns only"};
        }
        std::vector<VmValue> call_arguments;
        if (!method.is_static) {
          if (!context.arguments[0].ref.IsValid()) {
            throw VmJavaThrow{"Ljava/lang/NullPointerException;",
                              "reflective receiver is null"};
          }
          call_arguments.push_back(VmValue::Ref(context.arguments[0].ref));
        }
        const auto outcome = vm.Call(method_id, call_arguments);
        if (outcome.exception.IsValid()) {
          throw VmJavaThrow{"Ljava/lang/RuntimeException;",
                            "reflected method raised: " +
                                outcome.exception_message};
        }
        const auto boxed = vm.NewIntrinsicInstance("Ljava/lang/Integer;");
        vm.Model().InstanceSlots(boxed)[0] = {
            static_cast<std::uint32_t>(outcome.value.AsInt()), SlotTag::cat1};
        return VmValue::Ref(boxed);
    });

  registry.Register("core.weak_reference.init", [](IntrinsicContext &context) {
        const auto slots = context.vm.Model().InstanceSlots(context.receiver);
        slots[0] = {context.arguments[0].ref.Value(), SlotTag::ref};
        return VmValue::Void();
    });
  registry.Register("core.weak_reference.get", [](IntrinsicContext &context) {
        const auto slots = context.vm.Model().InstanceSlots(context.receiver);
        return VmValue::Ref(VmObjectRef(slots[0].bits));
    });

  registry.Register(
      "core.reflect.array_new_instance", [](IntrinsicContext &context) {
        auto& vm = context.vm;
        auto& model = vm.Model();
        const auto class_object = context.arguments[0].ref;
        const auto dims_ref = context.arguments[1].ref;
        if (!class_object.IsValid() || !dims_ref.IsValid()) {
            throw VmJavaThrow{"Ljava/lang/NullPointerException;",
                              "Array.newInstance argument is null"};
        }
        const auto element_class = model.ClassOfClassObject(class_object);
        const auto dim_count = model.ArrayLength(dims_ref);
        if (dim_count <= 0 || dim_count > 255) {
            throw VmJavaThrow{"Ljava/lang/IllegalArgumentException;",
                              "invalid dimension count"};
        }
        std::vector<std::int32_t> dims;
        dims.reserve(static_cast<std::size_t>(dim_count));
        for (JniSize index = 0; index < dim_count; ++index) {
            const auto length = static_cast<std::int32_t>(
                model.GetPrimitiveElement(dims_ref, index));
            if (length < 0) {
                throw VmJavaThrow{"Ljava/lang/NegativeArraySizeException;",
                                  std::to_string(length)};
            }
            dims.push_back(length);
        }
        const auto element_descriptor =
            vm.Linker().Class(element_class).descriptor;
        if (element_descriptor == "V") {
            throw VmJavaThrow{"Ljava/lang/IllegalArgumentException;",
                              "void array"};
        }
        return VmValue::Ref(BuildReflectArray(vm, element_descriptor, dims));
    });

  registry.Register("core.throwable.init",
                    [](IntrinsicContext &) { return VmValue::Void(); });
    registry.Register("core.throwable.init_message",
                      [](IntrinsicContext& context) {
        const auto message = context.arguments[0].ref;
        context.vm.SetThrowableMessage(context.receiver, message);
        return VmValue::Void();
    });
  registry.Register(
      "core.throwable.get_message", [](IntrinsicContext &context) {
        return VmValue::Ref(context.vm.ThrowableMessage(context.receiver));
    });
  registry.Register("core.throwable.to_string", [](IntrinsicContext &context) {
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
