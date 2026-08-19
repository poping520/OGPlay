// java.lang.Enum intrinsic. Semantics follow the pinned AOSP libcore
// libart Enum.java: constants carry a final name/ordinal slot pair written
// by the protected constructor, the query methods are final, toString stays
// overridable, and static valueOf resolves constants from the enum's own
// static fields (the class library's reflective values() cache is not in
// scope; see docs/design/dexvm/03-platform-intrinsics.md §5).

#include "catalog.h"
#include "shared.h"

#include "ogplay/runtime/dexvm/intrinsic_builder.h"

namespace ogplay::runtime::dexvm::intrinsics {
using namespace detail;

namespace {

// Enum's declared slots resolved through the receiver's own class, so
// interpreted enum subclasses and constant-body subclasses share the
// super layout (same pattern as the Throwable message field).
[[nodiscard]] const LinkedField& EnumField(Interpreter& vm,
                                           const VmObjectRef object,
                                           const std::string_view name,
                                           const std::string_view descriptor) {
    const auto java_class = vm.Model().ObjectClass(object);
    const auto field = vm.Linker().FindFieldRecursive(
        java_class, std::string(name), std::string(descriptor));
    if (!field.has_value()) {
        throw DexVmError(DexVmErrorReason::internal_invariant,
                         "Enum field is missing: " + std::string(name));
    }
    return vm.Linker().Field(*field);
}

[[nodiscard]] VmObjectRef NameOf(Interpreter& vm, const VmObjectRef object) {
    const auto& linked = EnumField(vm, object, "name", "Ljava/lang/String;");
    const auto slots = vm.Model().InstanceSlots(object);
    if (linked.slot >= slots.size() || !linked.is_ref) {
        throw DexVmError(DexVmErrorReason::internal_invariant,
                         "Enum name slot is invalid");
    }
    return VmObjectRef(slots[linked.slot].bits);
}

[[nodiscard]] std::int32_t OrdinalOf(Interpreter& vm,
                                     const VmObjectRef object) {
    const auto& linked = EnumField(vm, object, "ordinal", "I");
    const auto slots = vm.Model().InstanceSlots(object);
    if (linked.slot >= slots.size() || linked.is_ref) {
        throw DexVmError(DexVmErrorReason::internal_invariant,
                         "Enum ordinal slot is invalid");
    }
    return static_cast<std::int32_t>(slots[linked.slot].bits);
}

[[nodiscard]] bool IsEnumClass(const DexClassLinker& linker,
                               const DexClassId java_class) {
    const auto& linked = linker.Class(java_class);
    return linked.super.has_value() &&
           linker.Class(*linked.super).descriptor == "Ljava/lang/Enum;";
}

// "class fixture.Size" — the Class.toString form AOSP composes into the
// IllegalArgumentException messages.
[[nodiscard]] std::string ClassDisplay(const DexClassLinker& linker,
                                       const DexClassId java_class) {
    return "class " + DottedName(linker.Class(java_class).descriptor);
}

// Enum.compareTo(Enum): ordinal difference. A null other reaches the
// field read on the platform too and surfaces as an NPE.
[[nodiscard]] std::int32_t CompareOrdinals(Interpreter& vm,
                                           const VmObjectRef receiver,
                                           const VmObjectRef other) {
    if (!other.IsValid()) {
        throw VmJavaThrow{"Ljava/lang/NullPointerException;",
                          "ordinal of null enum constant"};
    }
    return OrdinalOf(vm, receiver) - OrdinalOf(vm, other);
}

// Enum.valueOf(Class, String) without the reflective shared-constants
// cache: run <clinit>, then match the requested name against the live
// values of the enum's own constant fields.
[[nodiscard]] VmValue ValueOfConstant(IntrinsicContext& context) {
    auto& vm = context.vm;
    const auto enum_type = context.arguments[0].ref;
    const auto name = context.arguments[1].ref;
    if (!enum_type.IsValid()) {
        throw VmJavaThrow{"Ljava/lang/NullPointerException;",
                          "enumType == null"};
    }
    if (!name.IsValid()) {
        throw VmJavaThrow{"Ljava/lang/NullPointerException;", "name == null"};
    }
    const auto java_class = vm.Model().ClassOfClassObject(enum_type);
    auto& linker = vm.Linker();
    if (!IsEnumClass(linker, java_class)) {
        throw VmJavaThrow{"Ljava/lang/IllegalArgumentException;",
                          ClassDisplay(linker, java_class) +
                              " is not an enum type"};
    }
    const auto initialized = vm.EnsureClassInitialized(java_class);
    if (initialized.exception.IsValid()) {
        throw VmJavaThrow{linker.Class(initialized.exception_class).descriptor,
                          initialized.exception_message};
    }
    const auto wanted = vm.Model().StringValue(name);
    const auto& linked = linker.Class(java_class);
    for (const auto field_id : linked.own_static_fields) {
        const auto& field = linker.Field(field_id);
        if (!field.is_static || !field.is_ref ||
            field.descriptor != linked.descriptor) {
            continue;
        }
        const auto constant =
            VmObjectRef(linked.static_storage[field.slot]);
        if (!constant.IsValid()) continue;
        const auto constant_name = NameOf(vm, constant);
        if (constant_name.IsValid() &&
            vm.Model().StringValue(constant_name) == wanted) {
            return VmValue::Ref(constant);
        }
    }
    std::string rendered;
    for (const auto unit : wanted) {
        if (unit >= 0x80U) {
            rendered += '?';
        } else {
            rendered += static_cast<char>(unit);
        }
    }
    throw VmJavaThrow{"Ljava/lang/IllegalArgumentException;",
                      rendered + " is not a constant in " +
                          DottedName(linked.descriptor)};
}

}  // namespace

IntrinsicClassDecl Declare_java_lang_Enum() {
    auto builder = IntrinsicClassBuilder::Class("Ljava/lang/Enum;", "Ljava/lang/Object;", {"Ljava/lang/Comparable;", "Ljava/io/Serializable;"});
    builder.InstanceField("name", "Ljava/lang/String;");
    builder.InstanceField("ordinal", "I");
    builder.Constructor("(Ljava/lang/String;I)V",
        [](IntrinsicContext& context) {
            const auto& name_field =
                EnumField(context.vm, context.receiver, "name",
                          "Ljava/lang/String;");
            const auto& ordinal_field =
                EnumField(context.vm, context.receiver, "ordinal", "I");
            auto slots =
                context.vm.Model().InstanceSlots(context.receiver);
            if (name_field.slot >= slots.size() ||
                ordinal_field.slot >= slots.size()) {
                throw DexVmError(DexVmErrorReason::internal_invariant,
                                 "Enum constant slots are invalid");
            }
            slots[name_field.slot] = {context.arguments[0].ref.Value(),
                                      SlotTag::ref};
            slots[ordinal_field.slot] = {
                static_cast<std::uint32_t>(context.arguments[1].AsInt()),
                SlotTag::cat1};
            return VmValue::Void();
        });
    builder.FinalMethod("name", "()Ljava/lang/String;",
        [](IntrinsicContext& context) {
            return VmValue::Ref(NameOf(context.vm, context.receiver));
        });
    builder.FinalMethod("ordinal", "()I",
        [](IntrinsicContext& context) {
            return VmValue::Int(OrdinalOf(context.vm, context.receiver));
        });
    // Not final on the platform: enum types may override toString.
    builder.VirtualMethod("toString", "()Ljava/lang/String;",
        [](IntrinsicContext& context) {
            return VmValue::Ref(NameOf(context.vm, context.receiver));
        });
    builder.FinalMethod("equals", "(Ljava/lang/Object;)Z",
        [](IntrinsicContext& context) {
            const auto other = context.arguments[0].ref;
            return VmValue::Int(context.receiver == other ? 1 : 0);
        });
    builder.FinalMethod("hashCode", "()I",
        [](IntrinsicContext& context) {
            const auto name = NameOf(context.vm, context.receiver);
            return VmValue::Int(
                OrdinalOf(context.vm, context.receiver) +
                (name.IsValid()
                     ? JavaStringHash(context.vm.Model().StringValue(name))
                     : 0));
        });
    builder.FinalMethod("clone", "()Ljava/lang/Object;",
        [](IntrinsicContext&) -> VmValue {
            throw VmJavaThrow{"Ljava/lang/CloneNotSupportedException;",
                              "Enums may not be cloned"};
        });
    builder.FinalMethod("compareTo", "(Ljava/lang/Enum;)I",
        [](IntrinsicContext& context) {
            return VmValue::Int(CompareOrdinals(
                context.vm, context.receiver, context.arguments[0].ref));
        });
    // Erased Comparable bridge. The per-enum cast to E is approximated by
    // the Enum-kind check; non-enum arguments fail like the platform's
    // checkcast bridge does.
    builder.FinalMethod("compareTo", "(Ljava/lang/Object;)I",
        [](IntrinsicContext& context) {
            const auto other = context.arguments[0].ref;
            if (other.IsValid()) {
                auto& linker = context.vm.Linker();
                auto current = context.vm.Model().ObjectClass(other);
                while (current.IsValid()) {
                    if (linker.Class(current).descriptor ==
                        "Ljava/lang/Enum;") {
                        break;
                    }
                    current = linker.Class(current).super.value_or(
                        DexClassId{});
                }
                if (!current.IsValid()) {
                    throw VmJavaThrow{"Ljava/lang/ClassCastException;",
                                      "argument is not an enum constant"};
                }
            }
            return VmValue::Int(
                CompareOrdinals(context.vm, context.receiver, other));
        });
    builder.FinalMethod("getDeclaringClass", "()Ljava/lang/Class;",
        [](IntrinsicContext& context) {
            auto& vm = context.vm;
            const auto java_class = vm.Model().ObjectClass(context.receiver);
            const auto& linked = vm.Linker().Class(java_class);
            // Constant-body subclasses answer the declaring enum type,
            // which is the superclass that directly extends Enum.
            if (!linked.super.has_value() ||
                vm.Linker().Class(*linked.super).descriptor ==
                    "Ljava/lang/Enum;") {
                return VmValue::Ref(vm.Model().ClassObject(java_class));
            }
            return VmValue::Ref(vm.Model().ClassObject(*linked.super));
        });
    builder.StaticMethod("valueOf",
                   "(Ljava/lang/Class;Ljava/lang/String;)Ljava/lang/Enum;",
                   ValueOfConstant);
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::dexvm::intrinsics
