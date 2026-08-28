// DVM-80: API-family translation unit. Physical consolidation only.

// ---- migrated from java_lang_Enum.cpp ----
// java.lang.Enum intrinsic. Semantics follow the pinned AOSP libcore
// libart Enum.java: constants carry a final name/ordinal slot pair written
// by the protected constructor, the query methods are final, toString stays
// overridable, and static valueOf resolves constants from the enum's own
// static fields (the class library's reflective values() cache is not in
// scope; see docs/design/dexvm/03-platform-intrinsics.md §5).

#include "catalog.h"
#include "shared.h"

#include "ogplay/runtime/dexvm/intrinsic_builder.h"

namespace ogplay::runtime::dexvm::intrinsics::dvm80_java_lang_Enum {
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

namespace ogplay::runtime::dexvm::intrinsics::
    dvm80_java_lang_primitive_wrappers {
void AppendJavaLangPrimitiveWrappers(
    std::vector<IntrinsicClassDecl>& catalog);
}

namespace ogplay::runtime::dexvm::intrinsics {

void AppendJavaLangPrimitiveWrappers(
    std::vector<IntrinsicClassDecl>& catalog) {
    dvm80_java_lang_primitive_wrappers::
        AppendJavaLangPrimitiveWrappers(catalog);
}

}  // namespace ogplay::runtime::dexvm::intrinsics

namespace ogplay::runtime::dexvm::intrinsics {
IntrinsicClassDecl Declare_java_lang_Enum() {
    return dvm80_java_lang_Enum::Declare_java_lang_Enum();
}
}  // namespace ogplay::runtime::dexvm::intrinsics

// ---- migrated from java_lang_interfaces.cpp ----
#include "catalog.h"
#include "shared.h"

#include "ogplay/runtime/dexvm/intrinsic_builder.h"

namespace ogplay::runtime::dexvm::intrinsics::dvm80_java_lang_interfaces {
    using namespace detail;

    namespace {
        // Top-level `java.lang` interfaces from pinned libcore
        // luni/src/main/java/java/lang (android-4.4.4_r2.0.1). Nested types such as
        // Thread.UncaughtExceptionHandler and java.lang.annotation.* stay out.

        IntrinsicClassDecl Declare_java_lang_Appendable() {
            auto builder = IntrinsicClassBuilder::Interface("Ljava/lang/Appendable;");
            builder.UnimplementedVirtual("append", "(C)Ljava/lang/Appendable;");
            builder.UnimplementedVirtual("append", "(Ljava/lang/CharSequence;)Ljava/lang/Appendable;");
            builder.UnimplementedVirtual("append", "(Ljava/lang/CharSequence;II)Ljava/lang/Appendable;");
            return std::move(builder).Build();
        }

        IntrinsicClassDecl Declare_java_lang_AutoCloseable() {
            auto builder = IntrinsicClassBuilder::Interface("Ljava/lang/AutoCloseable;");
            builder.UnimplementedVirtual("close", "()V");
            return std::move(builder).Build();
        }

        IntrinsicClassDecl Declare_java_lang_CharSequence() {
            auto builder = IntrinsicClassBuilder::Interface("Ljava/lang/CharSequence;");
            builder.FinalMethod("length", "()I", [](IntrinsicContext& context) {
                return VmValue::Int(static_cast<std::int32_t>(
                        context.vm.Model().StringValue(context.receiver).size())
                );
            });
            builder.UnimplementedVirtual("charAt", "(I)C");
            builder.UnimplementedVirtual("subSequence", "(II)Ljava/lang/CharSequence;");
            builder.UnimplementedVirtual("toString", "()Ljava/lang/String;");
            return std::move(builder).Build();
        }

        IntrinsicClassDecl Declare_java_lang_Cloneable() {
            auto builder = IntrinsicClassBuilder::Interface("Ljava/lang/Cloneable;");
            return std::move(builder).Build();
        }

        IntrinsicClassDecl Declare_java_lang_Comparable() {
            auto builder = IntrinsicClassBuilder::Interface("Ljava/lang/Comparable;");
            builder.UnimplementedVirtual("compareTo", "(Ljava/lang/Object;)I");
            return std::move(builder).Build();
        }

        IntrinsicClassDecl Declare_java_lang_Iterable() {
            auto builder = IntrinsicClassBuilder::Interface("Ljava/lang/Iterable;");
            builder.UnimplementedVirtual("iterator", "()Ljava/util/Iterator;");
            return std::move(builder).Build();
        }

        IntrinsicClassDecl Declare_java_lang_Readable() {
            auto builder = IntrinsicClassBuilder::Interface("Ljava/lang/Readable;");
            builder.UnimplementedVirtual("read", "(Ljava/nio/CharBuffer;)I");
            return std::move(builder).Build();
        }

        IntrinsicClassDecl Declare_java_lang_Runnable() {
            auto builder = IntrinsicClassBuilder::Interface("Ljava/lang/Runnable;");
            builder.UnimplementedVirtual("run", "()V");
            return std::move(builder).Build();
        }
    } // namespace

    void AppendJavaLangInterfaces(std::vector<IntrinsicClassDecl>& catalog) {
        catalog.push_back(Declare_java_lang_Appendable());
        catalog.push_back(Declare_java_lang_AutoCloseable());
        catalog.push_back(Declare_java_lang_CharSequence());
        catalog.push_back(Declare_java_lang_Cloneable());
        catalog.push_back(Declare_java_lang_Comparable());
        catalog.push_back(Declare_java_lang_Iterable());
        catalog.push_back(Declare_java_lang_Readable());
        catalog.push_back(Declare_java_lang_Runnable());
    }
} // namespace ogplay::runtime::dexvm::intrinsics

namespace ogplay::runtime::dexvm::intrinsics {
void AppendJavaLangInterfaces(std::vector<IntrinsicClassDecl>& catalog) {
    dvm80_java_lang_interfaces::AppendJavaLangInterfaces(catalog);
}
}  // namespace ogplay::runtime::dexvm::intrinsics

// ---- migrated from java_lang_Math.cpp ----
#include "catalog.h"
#include "shared.h"

#include "ogplay/runtime/dexvm/intrinsic_builder.h"

namespace ogplay::runtime::dexvm::intrinsics::dvm80_java_lang_Math {
using namespace detail;

IntrinsicClassDecl Declare_java_lang_Math() {
    auto builder = IntrinsicClassBuilder::Class("Ljava/lang/Math;", "Ljava/lang/Object;");
    builder.StaticMethod("abs", "(I)I",
        [](IntrinsicContext& context) {
                const auto value = context.arguments[0].AsInt();
                return VmValue::Int(value < 0 ? -value : value);
            });
    builder.StaticMethod("abs", "(J)J",
        [](IntrinsicContext& context) {
                const auto value = context.arguments[0].AsLong();
                return VmValue::Long(value < 0 ? -value : value);
            });
    builder.StaticMethod("abs", "(F)F",
        [](IntrinsicContext& context) {
                return VmValue::Float(std::fabs(context.arguments[0].AsFloat()));
            });
    builder.StaticMethod("abs", "(D)D",
        [](IntrinsicContext& context) {
                return VmValue::Double(std::fabs(context.arguments[0].AsDouble()));
            });
    builder.StaticMethod("max", "(II)I",
        [](IntrinsicContext& context) {
            return VmValue::Int(
                std::max(context.arguments[0].AsInt(), context.arguments[1].AsInt()));
            });
    builder.StaticMethod("min", "(II)I",
        [](IntrinsicContext& context) {
            return VmValue::Int(
                std::min(context.arguments[0].AsInt(), context.arguments[1].AsInt()));
            });
    builder.StaticMethod("max", "(FF)F",
        [](IntrinsicContext& context) {
                return VmValue::Float(std::fmax(context.arguments[0].AsFloat(),
                                                context.arguments[1].AsFloat()));
            });
    builder.StaticMethod("min", "(FF)F",
        [](IntrinsicContext& context) {
                return VmValue::Float(std::fmin(context.arguments[0].AsFloat(),
                                                context.arguments[1].AsFloat()));
            });
    builder.StaticMethod("sqrt", "(D)D",
        [](IntrinsicContext& context) {
                return VmValue::Double(std::sqrt(context.arguments[0].AsDouble()));
            });
    builder.StaticMethod("sin", "(D)D",
        [](IntrinsicContext& context) {
                return VmValue::Double(std::sin(context.arguments[0].AsDouble()));
            });
    builder.StaticMethod("cos", "(D)D",
        [](IntrinsicContext& context) {
                return VmValue::Double(std::cos(context.arguments[0].AsDouble()));
            });
    builder.StaticMethod("atan2", "(DD)D",
        [](IntrinsicContext& context) {
                return VmValue::Double(std::atan2(context.arguments[0].AsDouble(),
                                                  context.arguments[1].AsDouble()));
            });
    builder.StaticMethod("pow", "(DD)D",
        [](IntrinsicContext& context) {
                return VmValue::Double(std::pow(context.arguments[0].AsDouble(),
                                                context.arguments[1].AsDouble()));
            });
    builder.StaticMethod("floor", "(D)D",
        [](IntrinsicContext& context) {
                return VmValue::Double(std::floor(context.arguments[0].AsDouble()));
            });
    builder.StaticMethod("ceil", "(D)D",
        [](IntrinsicContext& context) {
                return VmValue::Double(std::ceil(context.arguments[0].AsDouble()));
            });
    auto result = std::move(builder).Build();
    return result;
}

}  // namespace ogplay::runtime::dexvm::intrinsics

namespace ogplay::runtime::dexvm::intrinsics {
IntrinsicClassDecl Declare_java_lang_Math() {
    return dvm80_java_lang_Math::Declare_java_lang_Math();
}
}  // namespace ogplay::runtime::dexvm::intrinsics

// ---- migrated from java_lang_Object.cpp ----
#include "catalog.h"
#include "shared.h"

#include <charconv>
#include <limits>
#include <vector>

#include "ogplay/runtime/dexvm/intrinsic_builder.h"

namespace ogplay::runtime::dexvm::intrinsics::dvm80_java_lang_Object {
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
                vm.SetPendingException(hash_outcome.exception);
                return VmValue::Ref(VmObjectRef{});
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

namespace ogplay::runtime::dexvm::intrinsics {
IntrinsicClassDecl Declare_java_lang_Object() {
    return dvm80_java_lang_Object::Declare_java_lang_Object();
}
}  // namespace ogplay::runtime::dexvm::intrinsics

// ---- migrated from java_lang_primitive_wrappers.cpp ----
#include "catalog.h"
#include "shared.h"

#include <array>
#include <bit>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <system_error>

#include "ogplay/hal/from_chars.h"
#include "ogplay/runtime/dexvm/intrinsic_builder.h"

namespace ogplay::runtime::dexvm::intrinsics::dvm80_java_lang_primitive_wrappers {
using namespace detail;
namespace {

constexpr std::string_view kDigits = "0123456789abcdefghijklmnopqrstuvwxyz";

[[noreturn]] void NumberFormat(std::string_view text) {
    throw VmJavaThrow{"Ljava/lang/NumberFormatException;",
                      "invalid number: " + std::string(text)};
}

[[nodiscard]] std::string GuestText(IntrinsicContext& context,
                                    VmObjectRef reference) {
    if (!reference.IsValid()) NumberFormat("null");
    return Narrow(context.vm.Model().StringValue(reference));
}

[[nodiscard]] std::string GuestFloatingText(IntrinsicContext& context,
                                            VmObjectRef reference) {
    if (!reference.IsValid()) {
        throw VmJavaThrow{"Ljava/lang/NullPointerException;",
                          "null floating string"};
    }
    return Narrow(context.vm.Model().StringValue(reference));
}

[[nodiscard]] int DigitValue(char unit) {
    if (unit >= '0' && unit <= '9') return unit - '0';
    if (unit >= 'a' && unit <= 'z') return unit - 'a' + 10;
    if (unit >= 'A' && unit <= 'Z') return unit - 'A' + 10;
    return -1;
}

[[nodiscard]] std::int64_t ParseSignedRadix(std::string_view text, int radix,
                                            std::int64_t minimum,
                                            std::int64_t maximum) {
    if (radix < 2 || radix > 36 || text.empty()) NumberFormat(text);
    std::size_t index = 0;
    bool negative = false;
    if (text[index] == '-' || text[index] == '+') {
        negative = text[index] == '-';
        if (++index == text.size()) NumberFormat(text);
    }
    const auto limit = negative ? minimum : -maximum;
    const auto multiply_limit = limit / radix;
    std::int64_t result = 0;
    for (; index < text.size(); ++index) {
        const auto digit = DigitValue(text[index]);
        if (digit < 0 || digit >= radix || result < multiply_limit) {
            NumberFormat(text);
        }
        result *= radix;
        if (result < limit + digit) NumberFormat(text);
        result -= digit;
    }
    return negative ? result : -result;
}

[[nodiscard]] std::int64_t DecodeIntegral(std::string_view text,
                                          std::int64_t minimum,
                                          std::int64_t maximum) {
    if (text.empty()) NumberFormat(text);
    bool negative = false;
    std::size_t index = 0;
    if (text[index] == '-' || text[index] == '+') {
        negative = text[index] == '-';
        if (++index == text.size()) NumberFormat(text);
    }
    int radix = 10;
    if (text[index] == '0') {
        if (index + 1 == text.size()) return 0;
        if (text[index + 1] == 'x' || text[index + 1] == 'X') {
            radix = 16;
            index += 2;
        } else {
            radix = 8;
            ++index;
        }
    } else if (text[index] == '#') {
        radix = 16;
        ++index;
    }
    if (index == text.size() || text[index] == '-' || text[index] == '+') {
        NumberFormat(text);
    }
    std::string normalized;
    if (negative) normalized.push_back('-');
    normalized.append(text.substr(index));
    return ParseSignedRadix(normalized, radix, minimum, maximum);
}

[[nodiscard]] std::string FormatUnsigned(std::uint64_t value, int radix) {
    std::array<char, 65> buffer{};
    auto cursor = buffer.end();
    do {
        *--cursor = kDigits[value % static_cast<unsigned>(radix)];
        value /= static_cast<unsigned>(radix);
    } while (value != 0);
    return {cursor, buffer.end()};
}

[[nodiscard]] std::string FormatSigned(std::int64_t value, int radix) {
    if (radix < 2 || radix > 36) radix = 10;
    const bool negative = value < 0;
    const auto magnitude = negative
        ? std::uint64_t{0} - static_cast<std::uint64_t>(value)
        : static_cast<std::uint64_t>(value);
    auto result = FormatUnsigned(magnitude, radix);
    if (negative) result.insert(result.begin(), '-');
    return result;
}

template <typename U>
[[nodiscard]] U ByteSwap(U value) {
    if constexpr (sizeof(U) == 2) {
        return static_cast<U>((value << 8U) | (value >> 8U));
    } else if constexpr (sizeof(U) == 4) {
        return static_cast<U>((value << 24U) |
            ((value << 8U) & 0x00ff0000U) |
            ((value >> 8U) & 0x0000ff00U) | (value >> 24U));
    } else {
        return static_cast<U>((value << 56U) |
            ((value << 40U) & 0x00ff000000000000ULL) |
            ((value << 24U) & 0x0000ff0000000000ULL) |
            ((value << 8U) & 0x000000ff00000000ULL) |
            ((value >> 8U) & 0x00000000ff000000ULL) |
            ((value >> 24U) & 0x0000000000ff0000ULL) |
            ((value >> 40U) & 0x000000000000ff00ULL) | (value >> 56U));
    }
}

[[nodiscard]] std::uint64_t ReadBoxed(IntrinsicContext& context,
                                      VmObjectRef object, bool wide) {
    if (!object.IsValid()) {
        throw VmJavaThrow{"Ljava/lang/NullPointerException;", "null wrapper"};
    }
    const auto slots = context.vm.Model().InstanceSlots(object);
    if (slots.size() < (wide ? 2U : 1U)) {
        throw VmJavaThrow{"Ljava/lang/IllegalArgumentException;",
                          "object is not a compatible wrapper"};
    }
    auto bits = static_cast<std::uint64_t>(slots[0].bits);
    if (wide) bits |= static_cast<std::uint64_t>(slots[1].bits) << 32U;
    return bits;
}

[[nodiscard]] bool IsExactClass(IntrinsicContext& context, VmObjectRef object,
                                std::string_view descriptor) {
    return object.IsValid() &&
           context.vm.Linker().Class(context.vm.Model().ObjectClass(object))
                   .descriptor == descriptor;
}

[[nodiscard]] VmObjectRef StaticReference(IntrinsicContext& context,
                                          std::string_view owner,
                                          std::string_view name,
                                          std::string_view descriptor) {
    const auto java_class = context.vm.Linker().FindClass(owner);
    if (!java_class.has_value()) return VmObjectRef{0};
    const auto field = context.vm.Linker().FindFieldRecursive(
        *java_class, std::string(name), std::string(descriptor));
    if (!field.has_value()) return VmObjectRef{0};
    const auto& linked = context.vm.Linker().Field(*field);
    return VmObjectRef{context.vm.Linker().Class(linked.owner)
                           .static_storage[linked.slot]};
}

void InitializeType(IntrinsicContext& context, std::string_view owner,
                    std::string_view primitive) {
    context.vm.SetIntrinsicStaticRef(
        owner, "TYPE", "Ljava/lang/Class;",
        context.vm.Model().ClassObject(
            context.vm.Linker().ResolveDescriptor(primitive)));
}

[[nodiscard]] VmValue Bool(bool value) {
    return VmValue::Int(value ? 1 : 0);
}

template <typename Float, typename Int>
[[nodiscard]] Int FloatToJavaInteger(Float value) {
    if (std::isnan(value)) return 0;
    if (value >= static_cast<Float>(std::numeric_limits<Int>::max())) {
        return std::numeric_limits<Int>::max();
    }
    if (value <= static_cast<Float>(std::numeric_limits<Int>::min())) {
        return std::numeric_limits<Int>::min();
    }
    return static_cast<Int>(value);
}

template <typename Float>
[[nodiscard]] std::string FormatFloating(Float value) {
    if (std::isnan(value)) return "NaN";
    if (std::isinf(value)) return std::signbit(value) ? "-Infinity" : "Infinity";
    if (value == 0) return std::signbit(value) ? "-0.0" : "0.0";

    // API 19 RealToString deliberately prints extra precision for the
    // smallest float subnormals, and special-cases Double.MIN_VALUE.
    if constexpr (sizeof(Float) == sizeof(float)) {
        const auto bits = std::bit_cast<std::uint32_t>(static_cast<float>(value));
        const auto magnitude = bits & 0x7fffffffU;
        if (magnitude >= 1U && magnitude <= 7U) {
            static constexpr std::array<std::string_view, 7> kSmallSubnormals{
                "1.4E-45", "2.8E-45", "4.2E-45", "5.6E-45",
                "7.0E-45", "8.4E-45", "9.8E-45",
            };
            std::string text(kSmallSubnormals[magnitude - 1U]);
            if ((bits & 0x80000000U) != 0) text.insert(text.begin(), '-');
            return text;
        }
    } else {
        const auto bits =
            std::bit_cast<std::uint64_t>(static_cast<double>(value));
        if ((bits & 0x7fffffffffffffffULL) == 1ULL) {
            return (bits & 0x8000000000000000ULL) != 0
                       ? "-4.9E-324"
                       : "4.9E-324";
        }
    }

    std::array<char, 128> buffer{};
    const auto absolute = std::fabs(value);
    const auto format = absolute >= static_cast<Float>(1e-3) &&
                                absolute < static_cast<Float>(1e7)
                            ? std::chars_format::fixed
                            : std::chars_format::scientific;
    const auto converted = std::to_chars(buffer.data(), buffer.data() + buffer.size(),
                                         value, format);
    if (converted.ec != std::errc{}) NumberFormat("floating formatting");
    std::string text(buffer.data(), converted.ptr);
    auto exponent = text.find_first_of("eE");
    if (exponent == std::string::npos) {
        if (text.find('.') == std::string::npos) text += ".0";
        return text;
    }
    text[exponent] = 'E';
    if (text.find('.', 0) == std::string::npos || text.find('.') > exponent) {
        text.insert(exponent, ".0");
        exponent += 2;
    }
    std::size_t sign = exponent + 1;
    if (sign < text.size() && text[sign] == '+') text.erase(sign, 1);
    if (sign < text.size() && text[sign] == '-') ++sign;
    while (sign + 1 < text.size() && text[sign] == '0') text.erase(sign, 1);
    return text;
}

[[nodiscard]] bool FloatingMagnitudeBelowOne(std::string_view text,
                                             bool hex) {
    const auto start = text.starts_with('-') ? 1U : 0U;
    const auto marker = text.find_first_of(hex ? "pP" : "eE", start);
    const auto significand_end =
        marker == std::string_view::npos ? text.size() : marker;

    std::int64_t explicit_exponent = 0;
    if (marker != std::string_view::npos) {
        auto cursor = marker + 1U;
        bool negative = false;
        if (cursor < text.size() &&
            (text[cursor] == '+' || text[cursor] == '-')) {
            negative = text[cursor] == '-';
            ++cursor;
        }
        if (cursor == text.size()) NumberFormat(text);

        std::int64_t magnitude = 0;
        for (; cursor < text.size(); ++cursor) {
            if (text[cursor] < '0' || text[cursor] > '9') NumberFormat(text);
            if (magnitude < 1'000'000'000LL) {
                const auto next =
                    magnitude * 10 + (text[cursor] - '0');
                magnitude =
                    next > 1'000'000'000LL ? 1'000'000'000LL : next;
            }
        }
        explicit_exponent = negative ? -magnitude : magnitude;
    }

    bool seen_point = false;
    std::int64_t digits_before_point = 0;
    std::int64_t digit_index = 0;
    std::int64_t first_nonzero = -1;
    int leading_digit = 0;
    for (auto cursor = start; cursor < significand_end; ++cursor) {
        const auto unit = text[cursor];
        if (unit == '.') {
            if (seen_point) NumberFormat(text);
            seen_point = true;
            continue;
        }
        const auto digit = DigitValue(unit);
        if (digit < 0 || digit >= (hex ? 16 : 10)) NumberFormat(text);
        if (!seen_point) ++digits_before_point;
        if (first_nonzero < 0 && digit != 0) {
            first_nonzero = digit_index;
            leading_digit = digit;
        }
        ++digit_index;
    }
    if (digit_index == 0) NumberFormat(text);
    if (first_nonzero < 0) return true;

    if (!hex) {
        return explicit_exponent + digits_before_point - first_nonzero - 1 < 0;
    }

    const auto leading_bit =
        leading_digit >= 8 ? 3 : leading_digit >= 4 ? 2 :
        leading_digit >= 2 ? 1 : 0;
    return explicit_exponent +
               4 * (digits_before_point - first_nonzero - 1) +
               leading_bit <
           0;
}

template <typename Float>
[[nodiscard]] Float ParseFloating(std::string text) {
    const auto whitespace = [](unsigned char unit) { return unit <= 0x20; };
    while (!text.empty() && whitespace(text.front())) text.erase(text.begin());
    while (!text.empty() && whitespace(text.back())) text.pop_back();
    if (text == "NaN" || text == "+NaN" || text == "-NaN") {
        return std::numeric_limits<Float>::quiet_NaN();
    }
    if (text == "Infinity" || text == "+Infinity") {
        return std::numeric_limits<Float>::infinity();
    }
    if (text == "-Infinity") return -std::numeric_limits<Float>::infinity();
    if (!text.empty() && (text.back() == 'f' || text.back() == 'F' ||
                          text.back() == 'd' || text.back() == 'D')) {
        text.pop_back();
    }
    if (text.empty()) NumberFormat(text);
    if (text.front() == '+') text.erase(text.begin());
    if (text.empty()) NumberFormat(text);
    Float result{};
    const char* begin = text.data();
    auto format = std::chars_format::general;
    bool hex = false;
    if (text.size() > 2 && (text.starts_with("0x") || text.starts_with("0X") ||
                            text.starts_with("-0x") || text.starts_with("-0X"))) {
        const std::size_t prefix = text[0] == '-' ? 1U : 0U;
        if (text.find_first_of("pP", prefix + 2U) == std::string::npos) {
            NumberFormat(text);
        }
        text.erase(prefix, 2);
        begin = text.data();
        format = std::chars_format::hex;
        hex = true;
    }
    const auto parsed = ogplay::hal::FromChars(
        begin, text.data() + text.size(), result, format);
    if (parsed.ec == std::errc::result_out_of_range) {
        const bool underflow = FloatingMagnitudeBelowOne(text, hex);
        const auto sign = text.starts_with('-') ? Float{-1} : Float{1};
        return underflow ? std::copysign(Float{0}, sign)
                         : std::copysign(std::numeric_limits<Float>::infinity(),
                                         sign);
    }
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size()) {
        NumberFormat(text);
    }
    return result;
}

template <typename Float>
[[nodiscard]] std::string FormatHexFloating(Float value) {
    if (std::isnan(value)) return "NaN";
    if (std::isinf(value)) return std::signbit(value) ? "-Infinity" : "Infinity";

    if constexpr (sizeof(Float) == sizeof(float)) {
        const auto bits = std::bit_cast<std::uint32_t>(static_cast<float>(value));
        const bool negative = (bits & 0x80000000U) != 0;
        const auto exponent = (bits & 0x7f800000U) >> 23U;
        auto significand = (bits & 0x007fffffU) << 1U;
        if (exponent == 0 && significand == 0) {
            return negative ? "-0x0.0p0" : "0x0.0p0";
        }

        std::string text = negative ? "-0x" : "0x";
        const bool subnormal = exponent == 0;
        text += subnormal ? "0." : "1.";

        std::size_t fraction_digits = 6;
        while (significand != 0 && (significand & 0xfU) == 0) {
            significand >>= 4U;
            --fraction_digits;
        }
        const auto fraction = FormatUnsigned(significand, 16);
        if (significand != 0 && fraction_digits > fraction.size()) {
            text.append(fraction_digits - fraction.size(), '0');
        }
        text += fraction;

        if (subnormal) {
            text += "p-126";
        } else {
            text += 'p';
            text += FormatSigned(static_cast<std::int64_t>(exponent) - 127, 10);
        }
        return text;
    } else {
        const auto bits =
            std::bit_cast<std::uint64_t>(static_cast<double>(value));
        const bool negative = (bits & 0x8000000000000000ULL) != 0;
        const auto exponent = (bits & 0x7ff0000000000000ULL) >> 52U;
        auto significand = bits & 0x000fffffffffffffULL;
        if (exponent == 0 && significand == 0) {
            return negative ? "-0x0.0p0" : "0x0.0p0";
        }

        std::string text = negative ? "-0x" : "0x";
        const bool subnormal = exponent == 0;
        text += subnormal ? "0." : "1.";

        std::size_t fraction_digits = 13;
        while (significand != 0 && (significand & 0xfULL) == 0) {
            significand >>= 4U;
            --fraction_digits;
        }
        const auto fraction = FormatUnsigned(significand, 16);
        if (significand != 0 && fraction_digits > fraction.size()) {
            text.append(fraction_digits - fraction.size(), '0');
        }
        text += fraction;

        if (subnormal) {
            text += "p-1022";
        } else {
            text += 'p';
            text += FormatSigned(static_cast<std::int64_t>(exponent) - 1023, 10);
        }
        return text;
    }
}

[[nodiscard]] std::uint32_t CanonicalFloatBits(float value) {
    return std::isnan(value) ? 0x7fc00000U : std::bit_cast<std::uint32_t>(value);
}

[[nodiscard]] std::uint64_t CanonicalDoubleBits(double value) {
    return std::isnan(value) ? 0x7ff8000000000000ULL
                             : std::bit_cast<std::uint64_t>(value);
}

[[nodiscard]] int JavaFloatCompare(float left, float right) {
    if (left < right) return -1;
    if (left > right) return 1;
    const auto l = static_cast<std::int32_t>(CanonicalFloatBits(left));
    const auto r = static_cast<std::int32_t>(CanonicalFloatBits(right));
    return l == r ? 0 : (l < r ? -1 : 1);
}

[[nodiscard]] int JavaDoubleCompare(double left, double right) {
    if (left < right) return -1;
    if (left > right) return 1;
    const auto l = static_cast<std::int64_t>(CanonicalDoubleBits(left));
    const auto r = static_cast<std::int64_t>(CanonicalDoubleBits(right));
    return l == r ? 0 : (l < r ? -1 : 1);
}

[[nodiscard]] bool CharacterIsWhitespace(std::int32_t code_point) {
    if ((code_point >= 0x1c && code_point <= 0x20) ||
        (code_point >= 0x09 && code_point <= 0x0d)) return true;
    if (code_point == 0x1680 || code_point == 0x180e) return true;
    if (code_point == 0x2007 || code_point == 0x202f) return false;
    return (code_point >= 0x2000 && code_point <= 0x200a) ||
           code_point == 0x2028 || code_point == 0x2029 ||
           code_point == 0x205f || code_point == 0x3000;
}

[[nodiscard]] bool CharacterIsSpace(std::int32_t code_point) {
    return code_point == 0x20 || code_point == 0xa0 ||
           code_point == 0x1680 || code_point == 0x180e ||
           (code_point >= 0x2000 && code_point <= 0x200a) ||
           code_point == 0x2028 || code_point == 0x2029 ||
           code_point == 0x202f || code_point == 0x205f ||
           code_point == 0x3000;
}

void AddTypeField(IntrinsicClassBuilder& builder) {
    builder.StaticField("TYPE", "Ljava/lang/Class;");
}

void AddNumberConversions(IntrinsicClassBuilder& builder, bool wide,
                          char kind) {
    const auto numeric = [wide, kind](IntrinsicContext& context) {
        const auto bits = ReadBoxed(context, context.receiver, wide);
        if (kind == 'F') return static_cast<long double>(
            std::bit_cast<float>(static_cast<std::uint32_t>(bits)));
        if (kind == 'D') return static_cast<long double>(std::bit_cast<double>(bits));
        if (wide) return static_cast<long double>(static_cast<std::int64_t>(bits));
        return static_cast<long double>(static_cast<std::int32_t>(bits));
    };
    builder.FinalMethod("byteValue", "()B", [numeric, kind](IntrinsicContext& c) {
        const auto value = numeric(c);
        const auto integer = (kind == 'F' || kind == 'D')
            ? FloatToJavaInteger<long double, std::int32_t>(value)
            : static_cast<std::int32_t>(value);
        return VmValue::Int(static_cast<std::int8_t>(integer)); });
    builder.FinalMethod("shortValue", "()S", [numeric, kind](IntrinsicContext& c) {
        const auto value = numeric(c);
        const auto integer = (kind == 'F' || kind == 'D')
            ? FloatToJavaInteger<long double, std::int32_t>(value)
            : static_cast<std::int32_t>(value);
        return VmValue::Int(static_cast<std::int16_t>(integer)); });
    builder.FinalMethod("intValue", "()I", [numeric, kind](IntrinsicContext& c) {
        const auto v = numeric(c);
        return VmValue::Int((kind == 'F' || kind == 'D')
            ? FloatToJavaInteger<long double, std::int32_t>(v)
            : static_cast<std::int32_t>(v)); });
    builder.FinalMethod("longValue", "()J", [numeric, kind](IntrinsicContext& c) {
        const auto v = numeric(c);
        return VmValue::Long((kind == 'F' || kind == 'D')
            ? FloatToJavaInteger<long double, std::int64_t>(v)
            : static_cast<std::int64_t>(v)); });
    builder.FinalMethod("floatValue", "()F", [numeric](IntrinsicContext& c) {
        return VmValue::Float(static_cast<float>(numeric(c))); });
    builder.FinalMethod("doubleValue", "()D", [numeric](IntrinsicContext& c) {
        return VmValue::Double(static_cast<double>(numeric(c))); });
}

IntrinsicClassDecl Declare_java_lang_Number() {
    auto builder = IntrinsicClassBuilder::Class("Ljava/lang/Number;", "Ljava/lang/Object;", {"Ljava/io/Serializable;"});
    builder.Constructor("()V", [](IntrinsicContext&) { return VmValue::Void(); });
    const auto abstract = [](IntrinsicContext&) -> VmValue {
        throw VmJavaThrow{"Ljava/lang/AbstractMethodError;",
                          "abstract Number conversion"};
    };
    builder.VirtualMethod("byteValue", "()B", abstract);
    builder.VirtualMethod("shortValue", "()S", abstract);
    builder.VirtualMethod("intValue", "()I", abstract);
    builder.VirtualMethod("longValue", "()J", abstract);
    builder.VirtualMethod("floatValue", "()F", abstract);
    builder.VirtualMethod("doubleValue", "()D", abstract);
    return std::move(builder).Build();
}

template <typename T>
void AddSmallIntegralCommon(IntrinsicClassBuilder& builder,
                            const char* descriptor, const char* primitive,
                            const char* parse_name, const char* value_of_desc,
                            std::int64_t minimum, std::int64_t maximum) {
    const auto set = [](IntrinsicContext& context, std::int64_t value) {
        SetBoxedBits(context, context.receiver,
                     static_cast<std::uint32_t>(static_cast<std::int32_t>(value)),
                     false);
        return VmValue::Void();
    };
    builder.Constructor(std::string("(") + primitive + ")V",
                    [set](IntrinsicContext& c) { return set(c, c.arguments[0].AsInt()); });
    builder.Constructor("(Ljava/lang/String;)V",
                    [set, minimum, maximum](IntrinsicContext& c) {
        return set(c, ParseSignedRadix(GuestText(c, c.arguments[0].ref), 10,
                                       minimum, maximum)); });
    AddNumberConversions(builder, false, 'I');
    builder.StaticMethod(parse_name, std::string("(Ljava/lang/String;)") + primitive,
                   [minimum, maximum](IntrinsicContext& c) {
        return VmValue::Int(static_cast<std::int32_t>(ParseSignedRadix(
            GuestText(c, c.arguments[0].ref), 10, minimum, maximum))); });
    builder.StaticMethod(parse_name, std::string("(Ljava/lang/String;I)") + primitive,
                   [minimum, maximum](IntrinsicContext& c) {
        return VmValue::Int(static_cast<std::int32_t>(ParseSignedRadix(
            GuestText(c, c.arguments[0].ref), c.arguments[1].AsInt(), minimum,
            maximum))); });
    builder.StaticMethod("decode", std::string("(Ljava/lang/String;)") + descriptor,
                   [descriptor, minimum, maximum](IntrinsicContext& c) {
        const auto value = DecodeIntegral(GuestText(c, c.arguments[0].ref),
                                          minimum, maximum);
        return MakeBoxed(c, descriptor, static_cast<std::uint32_t>(value), false);
    });
    builder.StaticMethod("valueOf", value_of_desc,
                   [descriptor](IntrinsicContext& c) {
        return MakeBoxed(c, descriptor,
                         static_cast<std::uint32_t>(c.arguments[0].AsInt()), false);
    });
    builder.StaticMethod("valueOf", std::string("(Ljava/lang/String;)") + descriptor,
                   [descriptor, minimum, maximum](IntrinsicContext& c) {
        const auto value = ParseSignedRadix(GuestText(c, c.arguments[0].ref), 10,
                                            minimum, maximum);
        return MakeBoxed(c, descriptor, static_cast<std::uint32_t>(value), false);
    });
    builder.StaticMethod("valueOf", std::string("(Ljava/lang/String;I)") + descriptor,
                   [descriptor, minimum, maximum](IntrinsicContext& c) {
        const auto value = ParseSignedRadix(GuestText(c, c.arguments[0].ref),
            c.arguments[1].AsInt(), minimum, maximum);
        return MakeBoxed(c, descriptor, static_cast<std::uint32_t>(value), false);
    });
    builder.FinalMethod("compareTo", std::string("(") + descriptor + ")I",
                    [](IntrinsicContext& c) {
        const auto left = static_cast<T>(ReadBoxed(c, c.receiver, false));
        const auto right = static_cast<T>(ReadBoxed(c, c.arguments[0].ref, false));
        return VmValue::Int(left < right ? -1 : (left > right ? 1 : 0)); });
    builder.StaticMethod("compare", std::string("(") + primitive + primitive + ")I",
                   [](IntrinsicContext& c) {
        const auto left = static_cast<T>(c.arguments[0].AsInt());
        const auto right = static_cast<T>(c.arguments[1].AsInt());
        return VmValue::Int(left < right ? -1 : (left > right ? 1 : 0)); });
    builder.FinalMethod("equals", "(Ljava/lang/Object;)Z",
                    [descriptor](IntrinsicContext& c) {
        return Bool(IsExactClass(c, c.arguments[0].ref, descriptor) &&
                    static_cast<T>(ReadBoxed(c, c.receiver, false)) ==
                    static_cast<T>(ReadBoxed(c, c.arguments[0].ref, false))); });
    builder.FinalMethod("hashCode", "()I", [](IntrinsicContext& c) {
        return VmValue::Int(static_cast<T>(ReadBoxed(c, c.receiver, false))); });
    builder.FinalMethod("toString", "()Ljava/lang/String;", [](IntrinsicContext& c) {
        return Make(c, Widen(FormatSigned(
            static_cast<T>(ReadBoxed(c, c.receiver, false)), 10))); });
    builder.StaticMethod("toString", std::string("(") + primitive + ")Ljava/lang/String;",
                   [](IntrinsicContext& c) {
        return Make(c, Widen(FormatSigned(static_cast<T>(c.arguments[0].AsInt()), 10))); });
}

IntrinsicClassDecl Declare_java_lang_Byte() {
    auto builder = IntrinsicClassBuilder::Class("Ljava/lang/Byte;", "Ljava/lang/Number;", {"Ljava/lang/Comparable;"});
    builder.InstanceField("value", "B");
    builder.ConstantInt("MAX_VALUE", "B", 127).ConstantInt("MIN_VALUE", "B", -128)
           .ConstantInt("SIZE", "I", 8);
    AddTypeField(builder);
    AddSmallIntegralCommon<std::int8_t>(builder, "Ljava/lang/Byte;", "B",
        "parseByte", "(B)Ljava/lang/Byte;", -128, 127);
    builder.StaticMethod("toHexString", "(BZ)Ljava/lang/String;", [](IntrinsicContext& c) {
        auto text = FormatUnsigned(static_cast<std::uint8_t>(c.arguments[0].AsInt()), 16);
        if (text.size() < 2) text.insert(text.begin(), '0');
        if (c.arguments[1].AsInt() != 0) {
            for (auto& unit : text) if (unit >= 'a') unit -= 32;
        }
        return Make(c, Widen(text));
    });
    builder.ClassInitializer([](IntrinsicContext& c) { InitializeType(c, "Ljava/lang/Byte;", "B"); return VmValue::Void(); });
    return std::move(builder).Build();
}

IntrinsicClassDecl Declare_java_lang_Short() {
    auto builder = IntrinsicClassBuilder::Class("Ljava/lang/Short;", "Ljava/lang/Number;", {"Ljava/lang/Comparable;"});
    builder.InstanceField("value", "S");
    builder.ConstantInt("MAX_VALUE", "S", 32767).ConstantInt("MIN_VALUE", "S", -32768)
           .ConstantInt("SIZE", "I", 16);
    AddTypeField(builder);
    AddSmallIntegralCommon<std::int16_t>(builder, "Ljava/lang/Short;", "S",
        "parseShort", "(S)Ljava/lang/Short;", -32768, 32767);
    builder.StaticMethod("reverseBytes", "(S)S", [](IntrinsicContext& c) {
        const auto v = static_cast<std::uint16_t>(c.arguments[0].AsInt());
        return VmValue::Int(static_cast<std::int16_t>(ByteSwap(v))); });
    builder.ClassInitializer([](IntrinsicContext& c) { InitializeType(c, "Ljava/lang/Short;", "S"); return VmValue::Void(); });
    return std::move(builder).Build();
}

template <typename T, typename U>
void AddWideIntegralCommon(IntrinsicClassBuilder& builder,
                           const char* descriptor, const char* primitive,
                           const char* parse_name, const char* property_name,
                           int width) {
    constexpr bool wide = sizeof(T) == 8;
    const auto argument = [](const VmValue& value) -> T {
        if constexpr (wide) {
            return static_cast<T>(value.AsLong());
        } else {
            return static_cast<T>(value.AsInt());
        }
    };
    const auto answer = [](T value) -> VmValue {
        if constexpr (wide) {
            return VmValue::Long(value);
        } else {
            return VmValue::Int(value);
        }
    };
    const auto minimum = std::numeric_limits<T>::min();
    const auto maximum = std::numeric_limits<T>::max();
    builder.Constructor(std::string("(") + primitive + ")V",
                    [argument](IntrinsicContext& c) {
        const auto value = argument(c.arguments[0]);
        SetBoxedBits(c, c.receiver, static_cast<U>(value), wide);
        return VmValue::Void(); });
    builder.Constructor("(Ljava/lang/String;)V",
                    [minimum, maximum](IntrinsicContext& c) {
        const auto value = static_cast<T>(ParseSignedRadix(
            GuestText(c, c.arguments[0].ref), 10, minimum, maximum));
        SetBoxedBits(c, c.receiver, static_cast<U>(value), wide);
        return VmValue::Void(); });
    AddNumberConversions(builder, wide, wide ? 'J' : 'I');
    builder.StaticMethod(parse_name, std::string("(Ljava/lang/String;)") + primitive,
                   [answer, minimum, maximum](IntrinsicContext& c) {
        return answer(static_cast<T>(ParseSignedRadix(
            GuestText(c, c.arguments[0].ref), 10, minimum, maximum))); });
    builder.StaticMethod(parse_name, std::string("(Ljava/lang/String;I)") + primitive,
                   [answer, minimum, maximum](IntrinsicContext& c) {
        return answer(static_cast<T>(ParseSignedRadix(
            GuestText(c, c.arguments[0].ref), c.arguments[1].AsInt(), minimum,
            maximum))); });
    builder.StaticMethod("decode", std::string("(Ljava/lang/String;)") + descriptor,
                   [descriptor, minimum, maximum](IntrinsicContext& c) {
        const auto value = static_cast<T>(DecodeIntegral(
            GuestText(c, c.arguments[0].ref), minimum, maximum));
        return MakeBoxed(c, descriptor, static_cast<U>(value), wide); });
    builder.StaticMethod("valueOf", std::string("(") + primitive + ")" + descriptor,
                   [descriptor, argument](IntrinsicContext& c) {
        return MakeBoxed(c, descriptor, static_cast<U>(argument(c.arguments[0])), wide); });
    builder.StaticMethod("valueOf", std::string("(Ljava/lang/String;)") + descriptor,
                   [descriptor, minimum, maximum](IntrinsicContext& c) {
        const auto value = static_cast<T>(ParseSignedRadix(
            GuestText(c, c.arguments[0].ref), 10, minimum, maximum));
        return MakeBoxed(c, descriptor, static_cast<U>(value), wide); });
    builder.StaticMethod("valueOf", std::string("(Ljava/lang/String;I)") + descriptor,
                   [descriptor, minimum, maximum](IntrinsicContext& c) {
        const auto value = static_cast<T>(ParseSignedRadix(
            GuestText(c, c.arguments[0].ref), c.arguments[1].AsInt(), minimum,
            maximum));
        return MakeBoxed(c, descriptor, static_cast<U>(value), wide); });
    builder.FinalMethod("compareTo", std::string("(") + descriptor + ")I",
                    [](IntrinsicContext& c) {
        const auto left = static_cast<T>(ReadBoxed(c, c.receiver, wide));
        const auto right = static_cast<T>(ReadBoxed(c, c.arguments[0].ref, wide));
        return VmValue::Int(left < right ? -1 : (left > right ? 1 : 0)); });
    builder.StaticMethod("compare", std::string("(") + primitive + primitive + ")I",
                   [argument](IntrinsicContext& c) {
        const auto left = argument(c.arguments[0]);
        const auto right = argument(c.arguments[1]);
        return VmValue::Int(left < right ? -1 : (left > right ? 1 : 0)); });
    builder.FinalMethod("equals", "(Ljava/lang/Object;)Z", [descriptor](IntrinsicContext& c) {
        return Bool(IsExactClass(c, c.arguments[0].ref, descriptor) &&
                    ReadBoxed(c, c.receiver, wide) == ReadBoxed(c, c.arguments[0].ref, wide)); });
    builder.FinalMethod("hashCode", "()I", [](IntrinsicContext& c) {
        const auto bits = ReadBoxed(c, c.receiver, wide);
        return VmValue::Int(wide ? static_cast<std::int32_t>(bits ^ (bits >> 32U))
                                 : static_cast<std::int32_t>(bits)); });
    builder.FinalMethod("toString", "()Ljava/lang/String;", [](IntrinsicContext& c) {
        return Make(c, Widen(FormatSigned(static_cast<T>(ReadBoxed(c, c.receiver, wide)), 10))); });
    builder.StaticMethod("toString", std::string("(") + primitive + ")Ljava/lang/String;",
                   [argument](IntrinsicContext& c) {
        return Make(c, Widen(FormatSigned(argument(c.arguments[0]), 10))); });
    builder.StaticMethod("toString", std::string("(") + primitive + "I)Ljava/lang/String;",
                   [argument](IntrinsicContext& c) {
        return Make(c, Widen(FormatSigned(argument(c.arguments[0]), c.arguments[1].AsInt()))); });
    for (const auto& [name, radix] : std::array<std::pair<const char*, int>, 3>{
             {{"toBinaryString", 2}, {"toHexString", 16}, {"toOctalString", 8}}}) {
        builder.StaticMethod(name, std::string("(") + primitive + ")Ljava/lang/String;",
                       [argument, radix](IntrinsicContext& c) {
            return Make(c, Widen(FormatUnsigned(static_cast<U>(argument(c.arguments[0])), radix))); });
    }
    builder.StaticMethod(property_name, std::string("(Ljava/lang/String;)") + descriptor,
                   [descriptor, minimum, maximum](IntrinsicContext& c) {
        if (!c.arguments[0].ref.IsValid()) return VmValue::Ref(VmObjectRef{0});
        const auto key = GuestText(c, c.arguments[0].ref);
        if (key.empty()) return VmValue::Ref(VmObjectRef{0});
        const auto property = c.vm.GetSystemProperty(key);
        if (!property.has_value()) return VmValue::Ref(VmObjectRef{});
        try {
            const auto value = DecodeIntegral(*property, minimum, maximum);
            return MakeBoxed(c, descriptor, static_cast<U>(static_cast<T>(value)), wide);
        } catch (const VmJavaThrow&) { return VmValue::Ref(VmObjectRef{}); }
    });
    builder.StaticMethod(property_name, std::string("(Ljava/lang/String;") + primitive + ")" + descriptor,
                   [descriptor, argument, minimum, maximum](IntrinsicContext& c) {
        if (!c.arguments[0].ref.IsValid()) {
            return MakeBoxed(c, descriptor,
                static_cast<U>(argument(c.arguments[1])), wide);
        }
        const auto key = GuestText(c, c.arguments[0].ref);
        if (key.empty()) return MakeBoxed(c, descriptor,
            static_cast<U>(argument(c.arguments[1])), wide);
        const auto property = c.vm.GetSystemProperty(key);
        T value = argument(c.arguments[1]);
        if (property.has_value()) {
            try { value = static_cast<T>(DecodeIntegral(*property, minimum, maximum)); }
            catch (const VmJavaThrow&) {}
        }
        return MakeBoxed(c, descriptor, static_cast<U>(value), wide);
    });
    builder.StaticMethod(property_name, std::string("(Ljava/lang/String;") + descriptor + ")" + descriptor,
                   [descriptor, minimum, maximum](IntrinsicContext& c) {
        if (!c.arguments[0].ref.IsValid()) return VmValue::Ref(c.arguments[1].ref);
        const auto key = GuestText(c, c.arguments[0].ref);
        if (key.empty()) return VmValue::Ref(c.arguments[1].ref);
        const auto property = c.vm.GetSystemProperty(key);
        if (property.has_value()) {
            try {
                const auto value = static_cast<T>(DecodeIntegral(*property, minimum, maximum));
                return MakeBoxed(c, descriptor, static_cast<U>(value), wide);
            } catch (const VmJavaThrow&) {}
        }
        return VmValue::Ref(c.arguments[1].ref);
    });
    builder.StaticMethod("highestOneBit", std::string("(") + primitive + ")" + primitive,
                   [argument, answer, width](IntrinsicContext& c) {
        const U value = static_cast<U>(argument(c.arguments[0]));
        return answer(static_cast<T>(value == 0 ? 0 : U{1} << (width - 1 - std::countl_zero(value)))); });
    builder.StaticMethod("lowestOneBit", std::string("(") + primitive + ")" + primitive,
                   [argument, answer](IntrinsicContext& c) {
        const U value = static_cast<U>(argument(c.arguments[0]));
        return answer(static_cast<T>(value & (U{0} - value))); });
    builder.StaticMethod("numberOfLeadingZeros", std::string("(") + primitive + ")I",
                   [argument](IntrinsicContext& c) { return VmValue::Int(std::countl_zero(static_cast<U>(argument(c.arguments[0])))); });
    builder.StaticMethod("numberOfTrailingZeros", std::string("(") + primitive + ")I",
                   [argument](IntrinsicContext& c) { return VmValue::Int(std::countr_zero(static_cast<U>(argument(c.arguments[0])))); });
    builder.StaticMethod("bitCount", std::string("(") + primitive + ")I",
                   [argument](IntrinsicContext& c) { return VmValue::Int(std::popcount(static_cast<U>(argument(c.arguments[0])))); });
    builder.StaticMethod("rotateLeft", std::string("(") + primitive + "I)" + primitive,
                   [argument, answer](IntrinsicContext& c) { return answer(static_cast<T>(std::rotl(static_cast<U>(argument(c.arguments[0])), c.arguments[1].AsInt()))); });
    builder.StaticMethod("rotateRight", std::string("(") + primitive + "I)" + primitive,
                   [argument, answer](IntrinsicContext& c) { return answer(static_cast<T>(std::rotr(static_cast<U>(argument(c.arguments[0])), c.arguments[1].AsInt()))); });
    builder.StaticMethod("reverseBytes", std::string("(") + primitive + ")" + primitive,
                   [argument, answer](IntrinsicContext& c) { return answer(static_cast<T>(ByteSwap(static_cast<U>(argument(c.arguments[0]))))); });
    builder.StaticMethod("reverse", std::string("(") + primitive + ")" + primitive,
                   [argument, answer, width](IntrinsicContext& c) {
        U value = static_cast<U>(argument(c.arguments[0])); U reversed = 0;
        for (int i = 0; i < width; ++i) { reversed = static_cast<U>((reversed << 1U) | (value & 1U)); value >>= 1U; }
        return answer(static_cast<T>(reversed)); });
    builder.StaticMethod("signum", std::string("(") + primitive + ")I",
                   [argument](IntrinsicContext& c) { const auto v = argument(c.arguments[0]); return VmValue::Int(v < 0 ? -1 : (v > 0 ? 1 : 0)); });
}

IntrinsicClassDecl Declare_java_lang_Integer() {
    auto builder = IntrinsicClassBuilder::Class("Ljava/lang/Integer;", "Ljava/lang/Number;", {"Ljava/lang/Comparable;"});
    builder.InstanceField("value", "I").ConstantInt("MAX_VALUE", "I", 0x7fffffff)
           .ConstantInt("MIN_VALUE", "I", std::numeric_limits<std::int32_t>::min())
           .ConstantInt("SIZE", "I", 32);
    AddTypeField(builder);
    AddWideIntegralCommon<std::int32_t, std::uint32_t>(builder, "Ljava/lang/Integer;", "I", "parseInt", "getInteger", 32);
    builder.ClassInitializer([](IntrinsicContext& c) { InitializeType(c, "Ljava/lang/Integer;", "I"); return VmValue::Void(); });
    return std::move(builder).Build();
}

IntrinsicClassDecl Declare_java_lang_Long() {
    auto builder = IntrinsicClassBuilder::Class("Ljava/lang/Long;", "Ljava/lang/Number;", {"Ljava/lang/Comparable;"});
    builder.InstanceField("value", "J").ConstantInt("MAX_VALUE", "J", std::numeric_limits<std::int64_t>::max())
           .ConstantInt("MIN_VALUE", "J", std::numeric_limits<std::int64_t>::min())
           .ConstantInt("SIZE", "I", 64);
    AddTypeField(builder);
    AddWideIntegralCommon<std::int64_t, std::uint64_t>(builder, "Ljava/lang/Long;", "J", "parseLong", "getLong", 64);
    builder.ClassInitializer([](IntrinsicContext& c) { InitializeType(c, "Ljava/lang/Long;", "J"); return VmValue::Void(); });
    return std::move(builder).Build();
}

IntrinsicClassDecl Declare_java_lang_Boolean() {
    auto builder = IntrinsicClassBuilder::Class("Ljava/lang/Boolean;", "Ljava/lang/Object;", {"Ljava/io/Serializable;", "Ljava/lang/Comparable;"});
    builder.InstanceField("value", "Z")
           .StaticField("TRUE", "Ljava/lang/Boolean;")
           .StaticField("FALSE", "Ljava/lang/Boolean;");
    AddTypeField(builder);
    const auto parsed = [](IntrinsicContext& c, VmObjectRef text) {
        const auto value = Value(c, text);
        if (value.size() != 4) return false;
        return AsciiLower(value[0]) == u't' && AsciiLower(value[1]) == u'r' &&
               AsciiLower(value[2]) == u'u' && AsciiLower(value[3]) == u'e';
    };
    builder.Constructor("(Z)V", [](IntrinsicContext& c) { SetBoxedBits(c, c.receiver, c.arguments[0].AsInt() != 0, false); return VmValue::Void(); });
    builder.Constructor("(Ljava/lang/String;)V", [parsed](IntrinsicContext& c) { SetBoxedBits(c, c.receiver, c.arguments[0].ref.IsValid() && parsed(c, c.arguments[0].ref), false); return VmValue::Void(); });
    builder.FinalMethod("booleanValue", "()Z", [](IntrinsicContext& c) { return Bool(ReadBoxed(c, c.receiver, false) != 0); });
    builder.StaticMethod("parseBoolean", "(Ljava/lang/String;)Z", [parsed](IntrinsicContext& c) { return Bool(c.arguments[0].ref.IsValid() && parsed(c, c.arguments[0].ref)); });
    const auto canonical = [](IntrinsicContext& c, bool value) { return VmValue::Ref(StaticReference(c, "Ljava/lang/Boolean;", value ? "TRUE" : "FALSE", "Ljava/lang/Boolean;")); };
    builder.StaticMethod("valueOf", "(Z)Ljava/lang/Boolean;", [canonical](IntrinsicContext& c) { return canonical(c, c.arguments[0].AsInt() != 0); });
    builder.StaticMethod("valueOf", "(Ljava/lang/String;)Ljava/lang/Boolean;", [canonical, parsed](IntrinsicContext& c) { return canonical(c, c.arguments[0].ref.IsValid() && parsed(c, c.arguments[0].ref)); });
    builder.StaticMethod("compare", "(ZZ)I", [](IntrinsicContext& c) { return VmValue::Int((c.arguments[0].AsInt() != 0) - (c.arguments[1].AsInt() != 0)); });
    builder.FinalMethod("compareTo", "(Ljava/lang/Boolean;)I", [](IntrinsicContext& c) { return VmValue::Int((ReadBoxed(c, c.receiver, false) != 0) - (ReadBoxed(c, c.arguments[0].ref, false) != 0)); });
    builder.FinalMethod("equals", "(Ljava/lang/Object;)Z", [](IntrinsicContext& c) { return Bool(IsExactClass(c, c.arguments[0].ref, "Ljava/lang/Boolean;") && ReadBoxed(c, c.receiver, false) == ReadBoxed(c, c.arguments[0].ref, false)); });
    builder.FinalMethod("hashCode", "()I", [](IntrinsicContext& c) { return VmValue::Int(ReadBoxed(c, c.receiver, false) != 0 ? 1231 : 1237); });
    builder.FinalMethod("toString", "()Ljava/lang/String;", [](IntrinsicContext& c) { return Make(c, ReadBoxed(c, c.receiver, false) != 0 ? u"true" : u"false"); });
    builder.StaticMethod("toString", "(Z)Ljava/lang/String;", [](IntrinsicContext& c) { return Make(c, c.arguments[0].AsInt() != 0 ? u"true" : u"false"); });
    builder.StaticMethod("getBoolean", "(Ljava/lang/String;)Z", [](IntrinsicContext& c) {
        if (!c.arguments[0].ref.IsValid()) return Bool(false);
        const auto property = c.vm.GetSystemProperty(GuestText(c, c.arguments[0].ref));
        if (!property.has_value()) return Bool(false);
        std::string value = *property; for (auto& unit : value) if (unit >= 'A' && unit <= 'Z') unit += 32;
        return Bool(value == "true"); });
    builder.ClassInitializer([](IntrinsicContext& c) {
        InitializeType(c, "Ljava/lang/Boolean;", "Z");
        c.vm.SetIntrinsicStaticRef("Ljava/lang/Boolean;", "TRUE", "Ljava/lang/Boolean;", MakeBoxed(c, "Ljava/lang/Boolean;", 1, false).ref);
        c.vm.SetIntrinsicStaticRef("Ljava/lang/Boolean;", "FALSE", "Ljava/lang/Boolean;", MakeBoxed(c, "Ljava/lang/Boolean;", 0, false).ref);
        return VmValue::Void(); });
    return std::move(builder).Build();
}

template <typename Float, typename Bits>
void AddFloatingCommon(IntrinsicClassBuilder& builder, const char* descriptor,
                       const char* primitive, const char* parse_name,
                       const char* bits_to_name, const char* raw_bits_name,
                       const char* from_bits_name) {
    constexpr bool wide = sizeof(Float) == 8;
    const auto argument = [](const VmValue& value) -> Float { if constexpr (wide) return static_cast<Float>(value.AsDouble()); else return static_cast<Float>(value.AsFloat()); };
    const auto answer = [](Float value) -> VmValue { if constexpr (wide) return VmValue::Double(value); else return VmValue::Float(value); };
    const auto bits = [](Float value) -> Bits { return std::bit_cast<Bits>(value); };
    builder.Constructor(std::string("(") + primitive + ")V", [argument, bits](IntrinsicContext& c) { SetBoxedBits(c, c.receiver, bits(argument(c.arguments[0])), wide); return VmValue::Void(); });
    if constexpr (!wide) builder.Constructor("(D)V", [](IntrinsicContext& c) { SetBoxedBits(c, c.receiver, std::bit_cast<std::uint32_t>(static_cast<float>(c.arguments[0].AsDouble())), false); return VmValue::Void(); });
    builder.Constructor("(Ljava/lang/String;)V", [](IntrinsicContext& c) { const auto v = ParseFloating<Float>(GuestFloatingText(c, c.arguments[0].ref)); SetBoxedBits(c, c.receiver, std::bit_cast<Bits>(v), wide); return VmValue::Void(); });
    AddNumberConversions(builder, wide, wide ? 'D' : 'F');
    builder.StaticMethod(parse_name, std::string("(Ljava/lang/String;)") + primitive, [answer](IntrinsicContext& c) { return answer(ParseFloating<Float>(GuestFloatingText(c, c.arguments[0].ref))); });
    builder.StaticMethod("valueOf", std::string("(") + primitive + ")" + descriptor, [descriptor, argument, bits](IntrinsicContext& c) { return MakeBoxed(c, descriptor, bits(argument(c.arguments[0])), wide); });
    builder.StaticMethod("valueOf", std::string("(Ljava/lang/String;)") + descriptor, [descriptor](IntrinsicContext& c) { const auto v = ParseFloating<Float>(GuestFloatingText(c, c.arguments[0].ref)); return MakeBoxed(c, descriptor, std::bit_cast<Bits>(v), wide); });
    builder.StaticMethod("isNaN", std::string("(") + primitive + ")Z", [argument](IntrinsicContext& c) { return Bool(std::isnan(argument(c.arguments[0]))); });
    builder.FinalMethod("isNaN", "()Z", [](IntrinsicContext& c) { return Bool(std::isnan(std::bit_cast<Float>(static_cast<Bits>(ReadBoxed(c, c.receiver, wide))))); });
    builder.StaticMethod("isInfinite", std::string("(") + primitive + ")Z", [argument](IntrinsicContext& c) { return Bool(std::isinf(argument(c.arguments[0]))); });
    builder.FinalMethod("isInfinite", "()Z", [](IntrinsicContext& c) { return Bool(std::isinf(std::bit_cast<Float>(static_cast<Bits>(ReadBoxed(c, c.receiver, wide))))); });
    builder.StaticMethod("compare", std::string("(") + primitive + primitive + ")I", [argument](IntrinsicContext& c) { if constexpr (wide) return VmValue::Int(JavaDoubleCompare(argument(c.arguments[0]), argument(c.arguments[1]))); else return VmValue::Int(JavaFloatCompare(argument(c.arguments[0]), argument(c.arguments[1]))); });
    builder.FinalMethod("compareTo", std::string("(") + descriptor + ")I", [](IntrinsicContext& c) { const auto left = std::bit_cast<Float>(static_cast<Bits>(ReadBoxed(c, c.receiver, wide))); const auto right = std::bit_cast<Float>(static_cast<Bits>(ReadBoxed(c, c.arguments[0].ref, wide))); if constexpr (wide) return VmValue::Int(JavaDoubleCompare(left, right)); else return VmValue::Int(JavaFloatCompare(left, right)); });
    builder.FinalMethod("equals", "(Ljava/lang/Object;)Z", [descriptor](IntrinsicContext& c) { if (!IsExactClass(c, c.arguments[0].ref, descriptor)) return Bool(false); const auto left = std::bit_cast<Float>(static_cast<Bits>(ReadBoxed(c, c.receiver, wide))); const auto right = std::bit_cast<Float>(static_cast<Bits>(ReadBoxed(c, c.arguments[0].ref, wide))); if constexpr (wide) return Bool(CanonicalDoubleBits(left) == CanonicalDoubleBits(right)); else return Bool(CanonicalFloatBits(left) == CanonicalFloatBits(right)); });
    builder.FinalMethod("hashCode", "()I", [](IntrinsicContext& c) { const auto value = std::bit_cast<Float>(static_cast<Bits>(ReadBoxed(c, c.receiver, wide))); if constexpr (wide) { const auto canonical = CanonicalDoubleBits(value); return VmValue::Int(static_cast<std::int32_t>(canonical ^ (canonical >> 32U))); } else return VmValue::Int(static_cast<std::int32_t>(CanonicalFloatBits(value))); });
    builder.FinalMethod("toString", "()Ljava/lang/String;", [](IntrinsicContext& c) { return Make(c, Widen(FormatFloating(std::bit_cast<Float>(static_cast<Bits>(ReadBoxed(c, c.receiver, wide)))))); });
    builder.StaticMethod("toString", std::string("(") + primitive + ")Ljava/lang/String;", [argument](IntrinsicContext& c) { return Make(c, Widen(FormatFloating(argument(c.arguments[0])))); });
    builder.StaticMethod("toHexString", std::string("(") + primitive + ")Ljava/lang/String;", [argument](IntrinsicContext& c) { return Make(c, Widen(FormatHexFloating(argument(c.arguments[0])))); });
    builder.StaticMethod(bits_to_name, std::string("(") + primitive + ")" + (wide ? "J" : "I"), [argument](IntrinsicContext& c) { if constexpr (wide) return VmValue::Long(static_cast<std::int64_t>(CanonicalDoubleBits(argument(c.arguments[0])))); else return VmValue::Int(static_cast<std::int32_t>(CanonicalFloatBits(argument(c.arguments[0])))); });
    builder.StaticMethod(raw_bits_name, std::string("(") + primitive + ")" + (wide ? "J" : "I"), [argument, bits](IntrinsicContext& c) { if constexpr (wide) return VmValue::Long(static_cast<std::int64_t>(bits(argument(c.arguments[0])))); else return VmValue::Int(static_cast<std::int32_t>(bits(argument(c.arguments[0])))); });
    builder.StaticMethod(from_bits_name, std::string("(") + (wide ? "J" : "I") + ")" + primitive, [answer](IntrinsicContext& c) { Bits raw; if constexpr (wide) raw = static_cast<Bits>(c.arguments[0].AsLong()); else raw = static_cast<Bits>(c.arguments[0].AsInt()); return answer(std::bit_cast<Float>(raw)); });
}

IntrinsicClassDecl Declare_java_lang_Float() {
    auto builder = IntrinsicClassBuilder::Class("Ljava/lang/Float;", "Ljava/lang/Number;", {"Ljava/lang/Comparable;"});
    builder.InstanceField("value", "F");
    AddTypeField(builder);
    builder.ConstantInt("MAX_VALUE", "F", std::bit_cast<std::int32_t>(std::numeric_limits<float>::max()))
           .ConstantInt("MIN_VALUE", "F", std::bit_cast<std::int32_t>(std::numeric_limits<float>::denorm_min()))
           .ConstantInt("NaN", "F", 0x7fc00000).ConstantInt("POSITIVE_INFINITY", "F", 0x7f800000)
           .ConstantInt("NEGATIVE_INFINITY", "F", static_cast<std::int32_t>(0xff800000U))
           .ConstantInt("MIN_NORMAL", "F", std::bit_cast<std::int32_t>(std::numeric_limits<float>::min()))
           .ConstantInt("MAX_EXPONENT", "I", 127).ConstantInt("MIN_EXPONENT", "I", -126)
           .ConstantInt("SIZE", "I", 32);
    AddFloatingCommon<float, std::uint32_t>(builder, "Ljava/lang/Float;", "F", "parseFloat", "floatToIntBits", "floatToRawIntBits", "intBitsToFloat");
    builder.ClassInitializer([](IntrinsicContext& c) { InitializeType(c, "Ljava/lang/Float;", "F"); return VmValue::Void(); });
    return std::move(builder).Build();
}

IntrinsicClassDecl Declare_java_lang_Double() {
    auto builder = IntrinsicClassBuilder::Class("Ljava/lang/Double;", "Ljava/lang/Number;", {"Ljava/lang/Comparable;"});
    builder.InstanceField("value", "D");
    AddTypeField(builder);
    builder.ConstantInt("MAX_VALUE", "D", std::bit_cast<std::int64_t>(std::numeric_limits<double>::max()))
           .ConstantInt("MIN_VALUE", "D", std::bit_cast<std::int64_t>(std::numeric_limits<double>::denorm_min()))
           .ConstantInt("NaN", "D", std::bit_cast<std::int64_t>(std::numeric_limits<double>::quiet_NaN()))
           .ConstantInt("POSITIVE_INFINITY", "D", std::bit_cast<std::int64_t>(std::numeric_limits<double>::infinity()))
           .ConstantInt("NEGATIVE_INFINITY", "D", std::bit_cast<std::int64_t>(-std::numeric_limits<double>::infinity()))
           .ConstantInt("MIN_NORMAL", "D", std::bit_cast<std::int64_t>(std::numeric_limits<double>::min()))
           .ConstantInt("MAX_EXPONENT", "I", 1023).ConstantInt("MIN_EXPONENT", "I", -1022)
           .ConstantInt("SIZE", "I", 64);
    AddFloatingCommon<double, std::uint64_t>(builder, "Ljava/lang/Double;", "D", "parseDouble", "doubleToLongBits", "doubleToRawLongBits", "longBitsToDouble");
    builder.ClassInitializer([](IntrinsicContext& c) { InitializeType(c, "Ljava/lang/Double;", "D"); return VmValue::Void(); });
    return std::move(builder).Build();
}

IntrinsicClassDecl Declare_java_lang_Character() {
    auto builder = IntrinsicClassBuilder::Class("Ljava/lang/Character;", "Ljava/lang/Object;", {"Ljava/io/Serializable;", "Ljava/lang/Comparable;"});
    builder.InstanceField("value", "C");
    AddTypeField(builder);
    for (const auto& [name, descriptor, value] : std::array<std::tuple<const char*, const char*, int>, 58>{{
        {"MIN_VALUE","C",0},{"MAX_VALUE","C",0xffff},{"MIN_RADIX","I",2},{"MAX_RADIX","I",36},
        {"UNASSIGNED","B",0},{"UPPERCASE_LETTER","B",1},{"LOWERCASE_LETTER","B",2},{"TITLECASE_LETTER","B",3},{"MODIFIER_LETTER","B",4},{"OTHER_LETTER","B",5},{"NON_SPACING_MARK","B",6},{"ENCLOSING_MARK","B",7},{"COMBINING_SPACING_MARK","B",8},{"DECIMAL_DIGIT_NUMBER","B",9},{"LETTER_NUMBER","B",10},{"OTHER_NUMBER","B",11},{"SPACE_SEPARATOR","B",12},{"LINE_SEPARATOR","B",13},{"PARAGRAPH_SEPARATOR","B",14},{"CONTROL","B",15},{"FORMAT","B",16},{"PRIVATE_USE","B",18},{"SURROGATE","B",19},{"DASH_PUNCTUATION","B",20},{"START_PUNCTUATION","B",21},{"END_PUNCTUATION","B",22},{"CONNECTOR_PUNCTUATION","B",23},{"OTHER_PUNCTUATION","B",24},{"MATH_SYMBOL","B",25},{"CURRENCY_SYMBOL","B",26},{"MODIFIER_SYMBOL","B",27},{"OTHER_SYMBOL","B",28},{"INITIAL_QUOTE_PUNCTUATION","B",29},{"FINAL_QUOTE_PUNCTUATION","B",30},
        {"DIRECTIONALITY_UNDEFINED","B",-1},{"DIRECTIONALITY_LEFT_TO_RIGHT","B",0},{"DIRECTIONALITY_RIGHT_TO_LEFT","B",1},{"DIRECTIONALITY_RIGHT_TO_LEFT_ARABIC","B",2},{"DIRECTIONALITY_EUROPEAN_NUMBER","B",3},{"DIRECTIONALITY_EUROPEAN_NUMBER_SEPARATOR","B",4},{"DIRECTIONALITY_EUROPEAN_NUMBER_TERMINATOR","B",5},{"DIRECTIONALITY_ARABIC_NUMBER","B",6},{"DIRECTIONALITY_COMMON_NUMBER_SEPARATOR","B",7},{"DIRECTIONALITY_NONSPACING_MARK","B",8},{"DIRECTIONALITY_BOUNDARY_NEUTRAL","B",9},{"DIRECTIONALITY_PARAGRAPH_SEPARATOR","B",10},{"DIRECTIONALITY_SEGMENT_SEPARATOR","B",11},{"DIRECTIONALITY_WHITESPACE","B",12},{"DIRECTIONALITY_OTHER_NEUTRALS","B",13},{"DIRECTIONALITY_LEFT_TO_RIGHT_EMBEDDING","B",14},{"DIRECTIONALITY_LEFT_TO_RIGHT_OVERRIDE","B",15},{"DIRECTIONALITY_RIGHT_TO_LEFT_EMBEDDING","B",16},{"DIRECTIONALITY_RIGHT_TO_LEFT_OVERRIDE","B",17},{"DIRECTIONALITY_POP_DIRECTIONAL_FORMAT","B",18},
        {"MIN_HIGH_SURROGATE","C",0xd800},{"MAX_HIGH_SURROGATE","C",0xdbff},{"MIN_LOW_SURROGATE","C",0xdc00},{"MAX_LOW_SURROGATE","C",0xdfff}
    }}) builder.ConstantInt(name, descriptor, value);
    builder.ConstantInt("MIN_SURROGATE","C",0xd800).ConstantInt("MAX_SURROGATE","C",0xdfff)
           .ConstantInt("MIN_SUPPLEMENTARY_CODE_POINT","I",0x10000).ConstantInt("MIN_CODE_POINT","I",0)
           .ConstantInt("MAX_CODE_POINT","I",0x10ffff).ConstantInt("SIZE","I",16);
    builder.Constructor("(C)V", [](IntrinsicContext& c) { SetBoxedBits(c, c.receiver, static_cast<std::uint16_t>(c.arguments[0].AsInt()), false); return VmValue::Void(); });
    builder.FinalMethod("charValue", "()C", [](IntrinsicContext& c) { return VmValue::Int(static_cast<std::uint16_t>(ReadBoxed(c, c.receiver, false))); });
    builder.StaticMethod("valueOf", "(C)Ljava/lang/Character;", [](IntrinsicContext& c) { return MakeBoxed(c, "Ljava/lang/Character;", static_cast<std::uint16_t>(c.arguments[0].AsInt()), false); });
    builder.FinalMethod("compareTo", "(Ljava/lang/Character;)I", [](IntrinsicContext& c) { return VmValue::Int(static_cast<int>(static_cast<std::uint16_t>(ReadBoxed(c, c.receiver, false))) - static_cast<int>(static_cast<std::uint16_t>(ReadBoxed(c, c.arguments[0].ref, false)))); });
    builder.StaticMethod("compare", "(CC)I", [](IntrinsicContext& c) { return VmValue::Int(static_cast<std::uint16_t>(c.arguments[0].AsInt()) - static_cast<std::uint16_t>(c.arguments[1].AsInt())); });
    builder.FinalMethod("equals", "(Ljava/lang/Object;)Z", [](IntrinsicContext& c) { return Bool(IsExactClass(c, c.arguments[0].ref, "Ljava/lang/Character;") && ReadBoxed(c, c.receiver, false) == ReadBoxed(c, c.arguments[0].ref, false)); });
    builder.FinalMethod("hashCode", "()I", [](IntrinsicContext& c) { return VmValue::Int(static_cast<std::uint16_t>(ReadBoxed(c, c.receiver, false))); });
    builder.FinalMethod("toString", "()Ljava/lang/String;", [](IntrinsicContext& c) { return Make(c, std::u16string(1, static_cast<char16_t>(ReadBoxed(c, c.receiver, false)))); });
    builder.StaticMethod("toString", "(C)Ljava/lang/String;", [](IntrinsicContext& c) { return Make(c, std::u16string(1, static_cast<char16_t>(c.arguments[0].AsInt()))); });
    const auto digit = [](std::int32_t cp, std::int32_t radix) { const auto value = cp < 128 ? DigitValue(static_cast<char>(cp)) : -1; return radix >= 2 && radix <= 36 && value < radix ? value : -1; };
    builder.StaticMethod("digit", "(CI)I", [digit](IntrinsicContext& c) { return VmValue::Int(digit(static_cast<std::uint16_t>(c.arguments[0].AsInt()), c.arguments[1].AsInt())); });
    builder.StaticMethod("digit", "(II)I", [digit](IntrinsicContext& c) { return VmValue::Int(digit(c.arguments[0].AsInt(), c.arguments[1].AsInt())); });
    builder.StaticMethod("forDigit", "(II)C", [](IntrinsicContext& c) { const auto d=c.arguments[0].AsInt(), r=c.arguments[1].AsInt(); return VmValue::Int(r>=2&&r<=36&&d>=0&&d<r ? kDigits[d] : 0); });
    const auto add_predicate = [&builder](const char* name, auto predicate) { builder.StaticMethod(name, "(C)Z", [predicate](IntrinsicContext& c){return Bool(predicate(static_cast<std::uint16_t>(c.arguments[0].AsInt())));}); builder.StaticMethod(name, "(I)Z", [predicate](IntrinsicContext& c){return Bool(predicate(c.arguments[0].AsInt()));}); };
    add_predicate("isDigit", [](int cp){return cp>='0'&&cp<='9';});
    add_predicate("isLetter", [](int cp){return (cp>='A'&&cp<='Z')||(cp>='a'&&cp<='z');});
    add_predicate("isLetterOrDigit", [](int cp){return (cp>='0'&&cp<='9')||(cp>='A'&&cp<='Z')||(cp>='a'&&cp<='z');});
    add_predicate("isLowerCase", [](int cp){return cp>='a'&&cp<='z';});
    add_predicate("isUpperCase", [](int cp){return cp>='A'&&cp<='Z';});
    add_predicate("isWhitespace", [](int cp){return CharacterIsWhitespace(cp);});
    add_predicate("isSpaceChar", [](int cp){return CharacterIsSpace(cp);});
    add_predicate("isISOControl", [](int cp){return (cp>=0&&cp<=0x1f)||(cp>=0x7f&&cp<=0x9f);});
    builder.StaticMethod("isSpace", "(C)Z", [](IntrinsicContext& c){const auto cp=static_cast<std::uint16_t>(c.arguments[0].AsInt()); return Bool(cp=='\n'||cp=='\t'||cp=='\f'||cp=='\r'||cp==' ');});
    builder.StaticMethod("toLowerCase", "(C)C", [](IntrinsicContext& c){const auto cp=c.arguments[0].AsInt(); return VmValue::Int(cp>='A'&&cp<='Z'?cp+32:cp);});
    builder.StaticMethod("toLowerCase", "(I)I", [](IntrinsicContext& c){const auto cp=c.arguments[0].AsInt(); return VmValue::Int(cp>='A'&&cp<='Z'?cp+32:cp);});
    builder.StaticMethod("toUpperCase", "(C)C", [](IntrinsicContext& c){const auto cp=c.arguments[0].AsInt(); return VmValue::Int(cp>='a'&&cp<='z'?cp-32:cp);});
    builder.StaticMethod("toUpperCase", "(I)I", [](IntrinsicContext& c){const auto cp=c.arguments[0].AsInt(); return VmValue::Int(cp>='a'&&cp<='z'?cp-32:cp);});
    builder.StaticMethod("isHighSurrogate", "(C)Z", [](IntrinsicContext& c){const auto cp=static_cast<std::uint16_t>(c.arguments[0].AsInt());return Bool(cp>=0xd800&&cp<=0xdbff);});
    builder.StaticMethod("isLowSurrogate", "(C)Z", [](IntrinsicContext& c){const auto cp=static_cast<std::uint16_t>(c.arguments[0].AsInt());return Bool(cp>=0xdc00&&cp<=0xdfff);});
    builder.StaticMethod("isSurrogatePair", "(CC)Z", [](IntrinsicContext& c){const auto h=static_cast<std::uint16_t>(c.arguments[0].AsInt()),l=static_cast<std::uint16_t>(c.arguments[1].AsInt());return Bool(h>=0xd800&&h<=0xdbff&&l>=0xdc00&&l<=0xdfff);});
    builder.StaticMethod("isValidCodePoint", "(I)Z", [](IntrinsicContext& c){const auto cp=c.arguments[0].AsInt();return Bool(cp>=0&&cp<=0x10ffff);});
    builder.StaticMethod("isBmpCodePoint", "(I)Z", [](IntrinsicContext& c){const auto cp=c.arguments[0].AsInt();return Bool(cp>=0&&cp<=0xffff);});
    builder.StaticMethod("isSupplementaryCodePoint", "(I)Z", [](IntrinsicContext& c){const auto cp=c.arguments[0].AsInt();return Bool(cp>=0x10000&&cp<=0x10ffff);});
    builder.StaticMethod("charCount", "(I)I", [](IntrinsicContext& c){return VmValue::Int(c.arguments[0].AsInt()>=0x10000?2:1);});
    builder.StaticMethod("toCodePoint", "(CC)I", [](IntrinsicContext& c){const auto h=static_cast<std::uint16_t>(c.arguments[0].AsInt()),l=static_cast<std::uint16_t>(c.arguments[1].AsInt());return VmValue::Int(((h-0xd800)<<10)+(l-0xdc00)+0x10000);});
    builder.StaticMethod("highSurrogate", "(I)C", [](IntrinsicContext& c){return VmValue::Int(static_cast<std::uint16_t>(((c.arguments[0].AsInt()-0x10000)>>10)+0xd800));});
    builder.StaticMethod("lowSurrogate", "(I)C", [](IntrinsicContext& c){return VmValue::Int(static_cast<std::uint16_t>(((c.arguments[0].AsInt()-0x10000)&0x3ff)+0xdc00));});
    builder.StaticMethod("reverseBytes", "(C)C", [](IntrinsicContext& c){return VmValue::Int(ByteSwap(static_cast<std::uint16_t>(c.arguments[0].AsInt())));});
    builder.ClassInitializer([](IntrinsicContext& c) { InitializeType(c, "Ljava/lang/Character;", "C"); return VmValue::Void(); });
    return std::move(builder).Build();
}

}  // namespace

void AppendJavaLangPrimitiveWrappers(
    std::vector<IntrinsicClassDecl>& catalog) {
    catalog.push_back(Declare_java_lang_Number());
    catalog.push_back(Declare_java_lang_Byte());
    catalog.push_back(Declare_java_lang_Short());
    catalog.push_back(Declare_java_lang_Integer());
    catalog.push_back(Declare_java_lang_Long());
    catalog.push_back(Declare_java_lang_Float());
    catalog.push_back(Declare_java_lang_Double());
    catalog.push_back(Declare_java_lang_Boolean());
    catalog.push_back(Declare_java_lang_Character());
}

}  // namespace ogplay::runtime::dexvm::intrinsics

namespace ogplay::runtime::dexvm::intrinsics {
IntrinsicClassDecl Declare_java_lang_Number() {
    return dvm80_java_lang_primitive_wrappers::Declare_java_lang_Number();
}
IntrinsicClassDecl Declare_java_lang_Byte() {
    return dvm80_java_lang_primitive_wrappers::Declare_java_lang_Byte();
}
IntrinsicClassDecl Declare_java_lang_Short() {
    return dvm80_java_lang_primitive_wrappers::Declare_java_lang_Short();
}
IntrinsicClassDecl Declare_java_lang_Integer() {
    return dvm80_java_lang_primitive_wrappers::Declare_java_lang_Integer();
}
IntrinsicClassDecl Declare_java_lang_Long() {
    return dvm80_java_lang_primitive_wrappers::Declare_java_lang_Long();
}
IntrinsicClassDecl Declare_java_lang_Boolean() {
    return dvm80_java_lang_primitive_wrappers::Declare_java_lang_Boolean();
}
IntrinsicClassDecl Declare_java_lang_Float() {
    return dvm80_java_lang_primitive_wrappers::Declare_java_lang_Float();
}
IntrinsicClassDecl Declare_java_lang_Double() {
    return dvm80_java_lang_primitive_wrappers::Declare_java_lang_Double();
}
IntrinsicClassDecl Declare_java_lang_Character() {
    return dvm80_java_lang_primitive_wrappers::Declare_java_lang_Character();
}
}  // namespace ogplay::runtime::dexvm::intrinsics

// ---- migrated from java_lang_ref_WeakReference.cpp ----
#include "catalog.h"
#include "shared.h"

#include "ogplay/runtime/dexvm/intrinsic_builder.h"

namespace ogplay::runtime::dexvm::intrinsics::dvm80_java_lang_ref_WeakReference {
using namespace detail;

IntrinsicClassDecl Declare_java_lang_ref_WeakReference() {
    auto builder = IntrinsicClassBuilder::Class("Ljava/lang/ref/WeakReference;", "Ljava/lang/Object;");
    builder.InstanceField("referent", "Ljava/lang/Object;");
    builder.Constructor("(Ljava/lang/Object;)V",
        [](IntrinsicContext &context) {
                const auto slots = context.vm.Model().InstanceSlots(context.receiver);
                slots[0] = {context.arguments[0].ref.Value(), SlotTag::ref};
                return VmValue::Void();
            });
    builder.FinalMethod("get", "()Ljava/lang/Object;",
        [](IntrinsicContext &context) {
                const auto slots = context.vm.Model().InstanceSlots(context.receiver);
                return VmValue::Ref(VmObjectRef(slots[0].bits));
            });
    auto result = std::move(builder).Build();
    return result;
}

}  // namespace ogplay::runtime::dexvm::intrinsics

namespace ogplay::runtime::dexvm::intrinsics {
IntrinsicClassDecl Declare_java_lang_ref_WeakReference() {
    return dvm80_java_lang_ref_WeakReference::Declare_java_lang_ref_WeakReference();
}
}  // namespace ogplay::runtime::dexvm::intrinsics

// ---- migrated from java_lang_String.cpp ----
#include "catalog.h"
#include "shared.h"

#include "ogplay/runtime/dexvm/intrinsic_builder.h"

namespace ogplay::runtime::dexvm::intrinsics::dvm80_java_lang_String {
using namespace detail;

namespace {

std::int64_t IntegralFormatValue(IntrinsicContext& context,
                                 const VmObjectRef argument) {
    if (!argument.IsValid()) {
        throw VmJavaThrow{"Ljava/lang/IllegalArgumentException;",
                          "%d argument is null"};
    }
    const auto descriptor = context.vm.Linker()
                                .Class(context.vm.Model().ObjectClass(argument))
                                .descriptor;
    const auto slots = context.vm.Model().InstanceSlots(argument);
    if (descriptor == "Ljava/lang/Byte;" && !slots.empty()) {
        return static_cast<std::int8_t>(slots[0].bits & 0xffU);
    }
    if (descriptor == "Ljava/lang/Short;" && !slots.empty()) {
        return static_cast<std::int16_t>(slots[0].bits & 0xffffU);
    }
    if (descriptor == "Ljava/lang/Integer;" && !slots.empty()) {
        return static_cast<std::int32_t>(slots[0].bits);
    }
    if (descriptor == "Ljava/lang/Long;" && slots.size() >= 2U) {
        return static_cast<std::int64_t>(
            static_cast<std::uint64_t>(slots[0].bits) |
            (static_cast<std::uint64_t>(slots[1].bits) << 32U));
    }
    throw VmJavaThrow{"Ljava/lang/IllegalArgumentException;",
                      "%d requires an integral wrapper"};
}

VmValue FormatSequential(IntrinsicContext& context) {
    const auto format = Value(context, context.arguments[0].ref);
    const auto arguments = context.arguments[1].ref;
    auto& model = context.vm.Model();
    if (arguments.IsValid() &&
        model.Kind(arguments) != VmObjectKind::object_array) {
        throw VmJavaThrow{"Ljava/lang/IllegalArgumentException;",
                          "String.format arguments are not Object[]"};
    }
    const auto argument_count = arguments.IsValid()
                                    ? model.ArrayLength(arguments)
                                    : 0;
    JniSize argument_index{};
    std::u16string output;
    output.reserve(format.size() +
                   static_cast<std::size_t>(argument_count) * 10U);
    for (std::size_t index = 0; index < format.size(); ++index) {
        if (format[index] != u'%') {
            output.push_back(format[index]);
            continue;
        }
        if (++index >= format.size()) {
            throw VmJavaThrow{"Ljava/lang/UnsupportedOperationException;",
                              "unterminated String.format conversion"};
        }
        const auto conversion = format[index];
        if (conversion == u'%') {
            output.push_back(u'%');
            continue;
        }
        if (conversion != u'd' && conversion != u's') {
            throw VmJavaThrow{
                "Ljava/lang/UnsupportedOperationException;",
                "String.format conversion is not provided: %" +
                    std::string(1, static_cast<char>(conversion))};
        }
        if (argument_index >= argument_count) {
            throw VmJavaThrow{"Ljava/lang/IllegalArgumentException;",
                              "String.format is missing an argument"};
        }
        const auto argument =
            model.GetObjectElement(arguments, argument_index++);
        if (conversion == u'd') {
            output += Widen(
                std::to_string(IntegralFormatValue(context, argument)));
            continue;
        }
        if (!argument.IsValid()) {
            output += u"null";
        } else if (model.Kind(argument) == VmObjectKind::string) {
            output += model.StringValue(argument);
        } else {
            throw VmJavaThrow{
                "Ljava/lang/UnsupportedOperationException;",
                "String.format %s requires virtual toString for " +
                    context.vm.Linker()
                        .Class(model.ObjectClass(argument))
                        .descriptor};
        }
    }
    return Make(context, output);
}

}  // namespace

IntrinsicClassDecl Declare_java_lang_String() {
    auto builder = IntrinsicClassBuilder::Class("Ljava/lang/String;", "Ljava/lang/Object;", {"Ljava/lang/CharSequence;", "Ljava/lang/Comparable;", "Ljava/io/Serializable;"});
    builder.Constructor("()V",
        [](IntrinsicContext& context) {
                context.vm.Model().BindString(context.receiver, {});
                return VmValue::Void();
            });
    builder.Constructor("(Ljava/lang/String;)V",
        [](IntrinsicContext& context) {
                context.vm.Model().BindString(
                    context.receiver, Value(context, context.arguments[0].ref));
                return VmValue::Void();
            });
    builder.Constructor("([B)V",
        [](IntrinsicContext& context) {
                auto& model = context.vm.Model();
                const auto array =
                    RequireArray(context.arguments[0].ref);
                const auto bytes =
                    model.ReadByteRegion(array, 0, model.ArrayLength(array));
                model.BindString(context.receiver, Utf8DecodeReplace(bytes));
                return VmValue::Void();
            });
    builder.Constructor("([BII)V",
        [](IntrinsicContext& context) {
                auto& model = context.vm.Model();
                const auto array =
                    RequireArray(context.arguments[0].ref);
                const auto offset = context.arguments[1].AsInt();
                const auto count = context.arguments[2].AsInt();
                CheckRegion(model.ArrayLength(array), offset, count);
                const auto bytes = model.ReadByteRegion(array, offset, count);
                model.BindString(context.receiver, Utf8DecodeReplace(bytes));
                return VmValue::Void();
            });
    builder.Constructor("([BLjava/lang/String;)V",
        [](IntrinsicContext& context) {
                auto& model = context.vm.Model();
                const auto array = RequireArray(context.arguments[0].ref);
                auto charset = Value(context, context.arguments[1].ref);
                for (auto& unit : charset) unit = AsciiUpper(unit);
                const auto bytes =
                    model.ReadByteRegion(array, 0, model.ArrayLength(array));
                std::u16string decoded;
                if (charset == u"UTF-8" || charset == u"UTF8") {
                    decoded = Utf8DecodeReplace(bytes);
                } else if (charset == u"ISO-8859-1" || charset == u"LATIN1" ||
                           charset == u"ISO8859_1") {
                    decoded.reserve(bytes.size());
                    for (const auto byte : bytes) {
                        decoded.push_back(static_cast<std::uint8_t>(byte));
                    }
                } else if (charset == u"US-ASCII" || charset == u"ASCII") {
                    decoded.reserve(bytes.size());
                    for (const auto byte : bytes) {
                        const auto unit = static_cast<std::uint8_t>(byte);
                        decoded.push_back(unit < 0x80U ? unit : u'\ufffd');
                    }
                } else {
                    throw VmJavaThrow{
                        "Ljava/io/UnsupportedEncodingException;",
                        "charset is not provided: " + ToUtf8(charset)};
                }
                model.BindString(context.receiver, decoded);
                return VmValue::Void();
            });
    builder.Constructor("([C)V",
        [](IntrinsicContext& context) {
                auto& model = context.vm.Model();
                const auto array =
                    RequireArray(context.arguments[0].ref);
                context.vm.Model().BindString(
                    context.receiver,
                    CharsValue(context, array, 0, model.ArrayLength(array)));
                return VmValue::Void();
            });
    builder.Constructor("([CII)V",
        [](IntrinsicContext& context) {
                auto& model = context.vm.Model();
                const auto array =
                    RequireArray(context.arguments[0].ref);
                const auto offset = context.arguments[1].AsInt();
                const auto count = context.arguments[2].AsInt();
                CheckRegion(model.ArrayLength(array), offset, count);
                model.BindString(context.receiver,
                                 CharsValue(context, array, offset, count));
                return VmValue::Void();
            });
    builder.FinalMethod("getBytes", "()[B",
        [](IntrinsicContext& context) {
                auto& vm = context.vm;
                const auto bytes =
                    Utf8Encode(Value(context, context.receiver));
                const auto array_class = vm.Linker().ResolveDescriptor("[B");
                const auto array = vm.Model().NewPrimitiveArray(
                    array_class, JniPrimitiveKind::byte,
                    static_cast<JniSize>(bytes.size()));
                if (!bytes.empty()) {
                    vm.Model().WriteByteRegion(array, 0, bytes);
                }
                return VmValue::Ref(array);
            });
    builder.FinalMethod("length", "()I",
        [](IntrinsicContext& context) {
                return VmValue::Int(static_cast<std::int32_t>(
                    context.vm.Model().StringValue(context.receiver).size()));
            });
    builder.FinalMethod("charAt", "(I)C",
        [](IntrinsicContext& context) {
            const auto value = context.vm.Model().StringValue(context.receiver);
                const auto index = context.arguments[0].AsInt();
                if (index < 0 || static_cast<std::size_t>(index) >= value.size()) {
              throw VmJavaThrow{"Ljava/lang/StringIndexOutOfBoundsException;",
                        "index " + std::to_string(index)};
                }
                return VmValue::Int(value[static_cast<std::size_t>(index)]);
            });
    builder.FinalMethod("equals", "(Ljava/lang/Object;)Z",
        [](IntrinsicContext& context) {
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
    builder.FinalMethod("equalsIgnoreCase", "(Ljava/lang/String;)Z",
        [](IntrinsicContext& context) {
                const auto other = context.arguments[0].ref;
                if (!other.IsValid()) return VmValue::Int(0);
                return VmValue::Int(
                    CompareStrings(Value(context, context.receiver),
                                   Value(context, other), true) == 0
                        ? 1
                        : 0);
            });
    builder.FinalMethod("hashCode", "()I",
        [](IntrinsicContext& context) {
            return VmValue::Int(
                JavaStringHash(context.vm.Model().StringValue(context.receiver)));
            });
    builder.FinalMethod("toString", "()Ljava/lang/String;",
        [](IntrinsicContext& context) {
                return VmValue::Ref(context.receiver);
            });
    builder.FinalMethod("compareTo", "(Ljava/lang/String;)I",
        [](IntrinsicContext& context) {
                return VmValue::Int(CompareStrings(
                    Value(context, context.receiver),
                    Value(context, context.arguments[0].ref), false));
            });
    builder.FinalMethod("compareToIgnoreCase", "(Ljava/lang/String;)I",
        [](IntrinsicContext& context) {
                return VmValue::Int(CompareStrings(
                    Value(context, context.receiver),
                    Value(context, context.arguments[0].ref), true));
            });
    builder.FinalMethod("concat", "(Ljava/lang/String;)Ljava/lang/String;",
        [](IntrinsicContext& context) {
                return Make(context, Value(context, context.receiver) +
                                         Value(context, context.arguments[0].ref));
            });
    builder.FinalMethod("startsWith", "(Ljava/lang/String;)Z",
        [](IntrinsicContext& context) {
                return VmValue::Int(
                    Value(context, context.receiver)
                            .starts_with(Value(context, context.arguments[0].ref))
                        ? 1
                        : 0);
            });
    builder.FinalMethod("endsWith", "(Ljava/lang/String;)Z",
        [](IntrinsicContext& context) {
                return VmValue::Int(
                    Value(context, context.receiver)
                            .ends_with(Value(context, context.arguments[0].ref))
                        ? 1
                        : 0);
            });
    builder.FinalMethod("indexOf", "(I)I",
        [](IntrinsicContext& context) {
                const auto haystack = Value(context, context.receiver);
                const auto found = haystack.find(static_cast<char16_t>(
                    context.arguments[0].cat1 & 0xffffU));
                return VmValue::Int(found == std::u16string::npos
                                        ? -1
                                        : static_cast<std::int32_t>(found));
            });
    builder.FinalMethod("indexOf", "(II)I",
        [](IntrinsicContext& context) {
                const auto haystack = Value(context, context.receiver);
                const auto from = context.arguments[1].AsInt();
                const auto start = from < 0 ? 0 : static_cast<std::size_t>(from);
                if (start >= haystack.size()) return VmValue::Int(-1);
                const auto found = haystack.find(
                    static_cast<char16_t>(context.arguments[0].cat1 & 0xffffU),
                    start);
                return VmValue::Int(found == std::u16string::npos
                                        ? -1
                                        : static_cast<std::int32_t>(found));
            });
    builder.FinalMethod("contains", "(Ljava/lang/CharSequence;)Z",
        [](IntrinsicContext& context) {
                const auto haystack = Value(context, context.receiver);
                const auto needle = Value(context, context.arguments[0].ref);
                return VmValue::Int(
                    haystack.find(needle) != std::u16string::npos ? 1 : 0);
            });
    builder.FinalMethod("getChars", "(II[CI)V",
        [](IntrinsicContext& context) {
                auto& model = context.vm.Model();
                const auto value = Value(context, context.receiver);
                const auto src_begin = context.arguments[0].AsInt();
                const auto src_end = context.arguments[1].AsInt();
                const auto target = RequireArray(context.arguments[2].ref);
                const auto dst_begin = context.arguments[3].AsInt();
                if (src_begin < 0 || src_begin > src_end ||
                    static_cast<std::size_t>(src_end) > value.size()) {
                    throw VmJavaThrow{
                        "Ljava/lang/StringIndexOutOfBoundsException;",
                        "getChars source range is invalid"};
                }
                const auto count = src_end - src_begin;
                if (dst_begin < 0 ||
                    static_cast<std::int64_t>(dst_begin) + count >
                        model.ArrayLength(target)) {
                    throw VmJavaThrow{
                        "Ljava/lang/ArrayIndexOutOfBoundsException;",
                        "getChars destination range is invalid"};
                }
                for (std::int32_t index = 0; index < count; ++index) {
                    model.SetPrimitiveElement(
                        target, dst_begin + index,
                        value[static_cast<std::size_t>(src_begin + index)]);
                }
                return VmValue::Void();
            });
    builder.FinalMethod("toCharArray", "()[C",
        [](IntrinsicContext& context) {
                auto& vm = context.vm;
                const auto value = Value(context, context.receiver);
                const auto array_class = vm.Linker().ResolveDescriptor("[C");
                const auto array = vm.Model().NewPrimitiveArray(
                    array_class, JniPrimitiveKind::character,
                    static_cast<JniSize>(value.size()));
                for (std::size_t index = 0; index < value.size(); ++index) {
                    vm.Model().SetPrimitiveElement(
                        array, static_cast<JniSize>(index), value[index]);
                }
                return VmValue::Ref(array);
            });
    builder.FinalMethod("replace", "(Ljava/lang/CharSequence;Ljava/lang/CharSequence;)Ljava/lang/String;",
        [](IntrinsicContext& context) {
                const auto value = Value(context, context.receiver);
                const auto target = Value(context, context.arguments[0].ref);
                const auto replacement = Value(context, context.arguments[1].ref);
                std::u16string out;
                if (target.empty()) {
                    // Empty target: the replacement goes between every char.
                    out = replacement;
                    for (const auto unit : value) {
                        out.push_back(unit);
                        out += replacement;
                    }
                } else {
                    std::size_t cursor = 0;
                    while (cursor <= value.size()) {
                        const auto found = value.find(target, cursor);
                        if (found == std::u16string::npos) {
                            out.append(value, cursor, value.size() - cursor);
                            break;
                        }
                        out.append(value, cursor, found - cursor);
                        out += replacement;
                        cursor = found + target.size();
                    }
                }
                return Make(context, out);
            });
    builder.FinalMethod("replaceAll", "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;",
        [](IntrinsicContext& context) {
                const auto value = ToUtf8(Value(context, context.receiver));
                const auto pattern =
                    CompileRegex(Value(context, context.arguments[0].ref));
                const auto replacement =
                    ToUtf8(Value(context, context.arguments[1].ref));
                return Make(context,
                            FromUtf8(std::regex_replace(value, pattern,
                                                        replacement)));
            });
    builder.FinalMethod("split", "(Ljava/lang/String;)[Ljava/lang/String;",
        [](IntrinsicContext& context) {
                auto& vm = context.vm;
                const auto value = ToUtf8(Value(context, context.receiver));
                const auto pattern =
                    CompileRegex(Value(context, context.arguments[0].ref));
                // Java limit-0 semantics: split on every match, then drop trailing
                // empty segments.
                std::vector<std::string> segments;
                std::size_t cursor = 0;
                std::smatch match;
                auto remaining = value;
                while (std::regex_search(remaining, match, pattern) &&
                       !(match.position(0) == 0 && match.length(0) == 0)) {
                    const auto position =
                        static_cast<std::string::size_type>(match.position(0));
                    const auto length =
                        static_cast<std::string::size_type>(match.length(0));
                    if (match.length(0) == 0) {
                        // Zero-width match: split after the next character.
                        segments.push_back(remaining.substr(0, position + 1));
                        remaining = remaining.substr(position + 1);
                    } else {
                        segments.push_back(remaining.substr(0, position));
                        remaining = remaining.substr(position + length);
                    }
                    cursor += 1;
                    if (cursor > value.size()) break;  // defensive progress bound
                }
                segments.push_back(remaining);
                while (!segments.empty() && segments.back().empty()) {
                    segments.pop_back();
                }
                const auto array_class =
                    vm.Linker().ResolveDescriptor("[Ljava/lang/String;");
                const auto element_class =
                    vm.Linker().ResolveDescriptor("Ljava/lang/String;");
                const auto array = vm.Model().NewObjectArray(
                    array_class, element_class,
                    static_cast<JniSize>(segments.size()));
                for (std::size_t index = 0; index < segments.size(); ++index) {
                    vm.Model().SetObjectElement(
                        array, static_cast<JniSize>(index),
                        vm.Model().NewString(FromUtf8(segments[index])));
                }
                return VmValue::Ref(array);
            });
    builder.FinalMethod("indexOf", "(Ljava/lang/String;)I",
        [](IntrinsicContext& context) {
                const auto haystack = Value(context, context.receiver);
                const auto found =
                    haystack.find(Value(context, context.arguments[0].ref));
                return VmValue::Int(found == std::u16string::npos
                                        ? -1
                                        : static_cast<std::int32_t>(found));
            });
    builder.FinalMethod("lastIndexOf", "(I)I",
        [](IntrinsicContext& context) {
                const auto haystack = Value(context, context.receiver);
                const auto found = haystack.rfind(static_cast<char16_t>(
                    context.arguments[0].cat1 & 0xffffU));
                return VmValue::Int(found == std::u16string::npos
                                        ? -1
                                        : static_cast<std::int32_t>(found));
            });
    builder.FinalMethod("lastIndexOf", "(Ljava/lang/String;)I",
        [](IntrinsicContext& context) {
                const auto haystack = Value(context, context.receiver);
                const auto found =
                    haystack.rfind(Value(context, context.arguments[0].ref));
                return VmValue::Int(found == std::u16string::npos
                                        ? -1
                                        : static_cast<std::int32_t>(found));
            });
    builder.FinalMethod("substring", "(I)Ljava/lang/String;",
        [](IntrinsicContext& context) {
                const auto value = Value(context, context.receiver);
                return Substring(context, context.arguments[0].AsInt(),
                                 static_cast<std::int32_t>(value.size()));
            });
    builder.FinalMethod("substring", "(II)Ljava/lang/String;",
        [](IntrinsicContext& context) {
                return Substring(context, context.arguments[0].AsInt(),
                                 context.arguments[1].AsInt());
            });
    builder.FinalMethod("toLowerCase", "()Ljava/lang/String;",
        [](IntrinsicContext& context) {
                auto value = Value(context, context.receiver);
                for (auto& unit : value) unit = AsciiLower(unit);
                return Make(context, value);
            });
    builder.FinalMethod("toUpperCase", "()Ljava/lang/String;",
        [](IntrinsicContext& context) {
                auto value = Value(context, context.receiver);
                for (auto& unit : value) unit = AsciiUpper(unit);
                return Make(context, value);
            });
    builder.FinalMethod("trim", "()Ljava/lang/String;",
        [](IntrinsicContext& context) {
                const auto value = Value(context, context.receiver);
                std::size_t begin = 0;
                std::size_t end = value.size();
                while (begin < end && value[begin] <= u' ') ++begin;
                while (end > begin && value[end - 1] <= u' ') --end;
                return Make(context, value.substr(begin, end - begin));
            });
    builder.FinalMethod("isEmpty", "()Z",
        [](IntrinsicContext& context) {
                return VmValue::Int(
                    Value(context, context.receiver).empty() ? 1 : 0);
            });
    builder.StaticMethod(
        "format", "(Ljava/lang/String;[Ljava/lang/Object;)Ljava/lang/String;",
        FormatSequential);
    builder.StaticMethod("valueOf", "(I)Ljava/lang/String;",
        [](IntrinsicContext& context) {
                return Make(context,
                            Widen(std::to_string(context.arguments[0].AsInt())));
            });
    builder.StaticMethod("valueOf", "(J)Ljava/lang/String;",
        [](IntrinsicContext& context) {
                return Make(context,
                            Widen(std::to_string(context.arguments[0].AsLong())));
            });
    builder.StaticMethod("valueOf", "(F)Ljava/lang/String;",
        [](IntrinsicContext& context) {
                return Make(context,
                            Widen(std::to_string(context.arguments[0].AsFloat())));
            });
    builder.StaticMethod("valueOf", "(D)Ljava/lang/String;",
        [](IntrinsicContext& context) {
                return Make(context,
                            Widen(std::to_string(context.arguments[0].AsDouble())));
            });
    builder.StaticMethod("valueOf", "(Z)Ljava/lang/String;",
        [](IntrinsicContext& context) {
                return Make(context, context.arguments[0].AsInt() != 0
                                         ? u"true"
                                         : std::u16string(u"false"));
            });
    builder.StaticMethod("valueOf", "(C)Ljava/lang/String;",
        [](IntrinsicContext& context) {
                return Make(context,
                            std::u16string(1, static_cast<char16_t>(
                                                  context.arguments[0].cat1 &
                                                  0xffffU)));
            });
    builder.StaticMethod("valueOf", "(Ljava/lang/Object;)Ljava/lang/String;",
        [](IntrinsicContext& context) {
                const auto argument = context.arguments[0].ref;
                if (!argument.IsValid()) {
                    return Make(context, std::u16string(u"null"));
                }
                auto& model = context.vm.Model();
                const auto kind = model.Kind(argument);
                if (kind == VmObjectKind::string || kind == VmObjectKind::external) {
                    return Make(context, Value(context, argument));
                }
                return Make(context,
                            Widen("@" + std::to_string(argument.Value())));
            });
    auto result = std::move(builder).Build();
    return result;
}

}  // namespace ogplay::runtime::dexvm::intrinsics

namespace ogplay::runtime::dexvm::intrinsics {
IntrinsicClassDecl Declare_java_lang_String() {
    return dvm80_java_lang_String::Declare_java_lang_String();
}
}  // namespace ogplay::runtime::dexvm::intrinsics

// ---- migrated from java_lang_StringBuffer.cpp ----
#include "catalog.h"
#include "shared.h"

#include "ogplay/runtime/dexvm/intrinsic_builder.h"

namespace ogplay::runtime::dexvm::intrinsics::detail {

[[nodiscard]] IntrinsicClassDecl DeclareStringBuilderLike(
    const std::string_view descriptor) {
    const std::string self(descriptor);
    auto builder = IntrinsicClassBuilder::Class(
        self, "Ljava/lang/Object;", {"Ljava/lang/CharSequence;"});
    builder.Constructor("()V", [](IntrinsicContext& context) {
        context.vm.BuilderBuffer(context.receiver).clear();
        return VmValue::Void();
    });
    builder.Constructor("(I)V", [](IntrinsicContext& context) {
        context.vm.BuilderBuffer(context.receiver).clear();
        return VmValue::Void();
    });
    builder.Constructor("(Ljava/lang/String;)V", [](IntrinsicContext& context) {
        context.vm.BuilderBuffer(context.receiver) =
            Value(context, context.arguments[0].ref);
        return VmValue::Void();
    });
    builder.FinalMethod("append", "(Ljava/lang/String;)" + self,
        [](IntrinsicContext& context) {
            const auto argument = context.arguments[0].ref;
            context.vm.BuilderBuffer(context.receiver) +=
                argument.IsValid() ? Value(context, argument)
                                   : std::u16string(u"null");
            return BuilderSelf(context);
        });
    builder.FinalMethod("append", "(Ljava/lang/Object;)" + self,
        [](IntrinsicContext& context) {
            const auto argument = context.arguments[0].ref;
            auto& buffer = context.vm.BuilderBuffer(context.receiver);
            if (!argument.IsValid()) {
                buffer += u"null";
            } else {
                auto& model = context.vm.Model();
                const auto kind = model.Kind(argument);
                if (kind == VmObjectKind::string ||
                    kind == VmObjectKind::external) {
                    buffer += Value(context, argument);
                } else {
                    buffer += Widen("@" + std::to_string(argument.Value()));
                }
            }
            return BuilderSelf(context);
        });
    builder.FinalMethod("append", "(I)" + self, [](IntrinsicContext& context) {
        context.vm.BuilderBuffer(context.receiver) +=
            Widen(std::to_string(context.arguments[0].AsInt()));
        return BuilderSelf(context);
    });
    builder.FinalMethod("append", "(J)" + self, [](IntrinsicContext& context) {
        context.vm.BuilderBuffer(context.receiver) +=
            Widen(std::to_string(context.arguments[0].AsLong()));
        return BuilderSelf(context);
    });
    builder.FinalMethod("append", "(Z)" + self, [](IntrinsicContext& context) {
        context.vm.BuilderBuffer(context.receiver) +=
            context.arguments[0].AsInt() != 0 ? u"true"
                                              : std::u16string(u"false");
        return BuilderSelf(context);
    });
    builder.FinalMethod("append", "(C)" + self, [](IntrinsicContext& context) {
        context.vm.BuilderBuffer(context.receiver) +=
            static_cast<char16_t>(context.arguments[0].cat1 & 0xffffU);
        return BuilderSelf(context);
    });
    builder.FinalMethod("append", "(F)" + self, [](IntrinsicContext& context) {
        context.vm.BuilderBuffer(context.receiver) +=
            Widen(std::to_string(context.arguments[0].AsFloat()));
        return BuilderSelf(context);
    });
    builder.FinalMethod("append", "(D)" + self, [](IntrinsicContext& context) {
        context.vm.BuilderBuffer(context.receiver) +=
            Widen(std::to_string(context.arguments[0].AsDouble()));
        return BuilderSelf(context);
    });
    builder.FinalMethod("toString", "()Ljava/lang/String;",
        [](IntrinsicContext& context) {
            return Make(context, context.vm.BuilderBuffer(context.receiver));
        });
    builder.FinalMethod("length", "()I", [](IntrinsicContext& context) {
        return VmValue::Int(static_cast<std::int32_t>(
            context.vm.BuilderBuffer(context.receiver).size()));
    });
    builder.FinalMethod("charAt", "(I)C", [](IntrinsicContext& context) {
        auto& buffer = context.vm.BuilderBuffer(context.receiver);
        const auto index = context.arguments[0].AsInt();
        CheckBuilderIndex(buffer, index);
        return VmValue::Int(buffer[static_cast<std::size_t>(index)]);
    });
    builder.FinalMethod("setCharAt", "(IC)V", [](IntrinsicContext& context) {
        auto& buffer = context.vm.BuilderBuffer(context.receiver);
        const auto index = context.arguments[0].AsInt();
        CheckBuilderIndex(buffer, index);
        buffer[static_cast<std::size_t>(index)] =
            static_cast<char16_t>(context.arguments[1].cat1 & 0xffffU);
        return VmValue::Void();
    });
    builder.FinalMethod("deleteCharAt", "(I)" + self,
        [](IntrinsicContext& context) {
            auto& buffer = context.vm.BuilderBuffer(context.receiver);
            const auto index = context.arguments[0].AsInt();
            CheckBuilderIndex(buffer, index);
            buffer.erase(buffer.begin() + index);
            return VmValue::Ref(context.receiver);
        });
    builder.FinalMethod("insert", "(IC)" + self, [](IntrinsicContext& context) {
        auto& buffer = context.vm.BuilderBuffer(context.receiver);
        const auto index = context.arguments[0].AsInt();
        if (index < 0 || static_cast<std::size_t>(index) > buffer.size()) {
            throw VmJavaThrow{"Ljava/lang/StringIndexOutOfBoundsException;",
                              "insert offset " + std::to_string(index)};
        }
        buffer.insert(buffer.begin() + index,
                      static_cast<char16_t>(context.arguments[1].cat1 & 0xffffU));
        return VmValue::Ref(context.receiver);
    });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::dexvm::intrinsics::detail

namespace ogplay::runtime::dexvm::intrinsics::dvm80_java_lang_StringBuffer {

IntrinsicClassDecl Declare_java_lang_StringBuffer() {
    return detail::DeclareStringBuilderLike("Ljava/lang/StringBuffer;");
}

}  // namespace ogplay::runtime::dexvm::intrinsics::dvm80_java_lang_StringBuffer

namespace ogplay::runtime::dexvm::intrinsics {
IntrinsicClassDecl Declare_java_lang_StringBuffer() {
    return dvm80_java_lang_StringBuffer::Declare_java_lang_StringBuffer();
}
}  // namespace ogplay::runtime::dexvm::intrinsics

// ---- migrated from java_lang_StringBuilder.cpp ----
#include "catalog.h"
#include "shared.h"

#include "ogplay/runtime/dexvm/intrinsic_builder.h"

namespace ogplay::runtime::dexvm::intrinsics::dvm80_java_lang_StringBuilder {

IntrinsicClassDecl Declare_java_lang_StringBuilder() {
    return detail::DeclareStringBuilderLike("Ljava/lang/StringBuilder;");
}

}  // namespace ogplay::runtime::dexvm::intrinsics::dvm80_java_lang_StringBuilder

namespace ogplay::runtime::dexvm::intrinsics {
IntrinsicClassDecl Declare_java_lang_StringBuilder() {
    return dvm80_java_lang_StringBuilder::Declare_java_lang_StringBuilder();
}
}  // namespace ogplay::runtime::dexvm::intrinsics

// ---- migrated from java_lang_System.cpp ----
#include "catalog.h"
#include "shared.h"

#include "ogplay/runtime/dexvm/intrinsic_builder.h"

namespace ogplay::runtime::dexvm::intrinsics::dvm80_java_lang_System {
using namespace detail;
namespace {

[[nodiscard]] std::string PropertyKey(IntrinsicContext& context,
                                      const VmObjectRef reference) {
    if (!reference.IsValid()) {
        throw VmJavaThrow{"Ljava/lang/NullPointerException;",
                          "system property key is null"};
    }
    auto key = context.vm.StringUtf8(reference);
    if (key.empty()) {
        throw VmJavaThrow{"Ljava/lang/IllegalArgumentException;",
                          "system property key is empty"};
    }
    return key;
}

[[nodiscard]] std::string PropertyValue(IntrinsicContext& context,
                                        const VmObjectRef reference) {
    if (!reference.IsValid()) {
        throw VmJavaThrow{"Ljava/lang/NullPointerException;",
                          "system property value is null"};
    }
    return context.vm.StringUtf8(reference);
}

}  // namespace

IntrinsicClassDecl Declare_java_lang_System() {
    auto builder = IntrinsicClassBuilder::Class("Ljava/lang/System;", "Ljava/lang/Object;");
    builder.StaticField("out", "Ljava/io/PrintStream;");
    builder.StaticField("err", "Ljava/io/PrintStream;");
    builder.StaticMethod("arraycopy", "(Ljava/lang/Object;ILjava/lang/Object;II)V",
        [](IntrinsicContext &context) {
                auto& model = context.vm.Model();
                const auto source = context.arguments[0].ref;
                const auto source_pos = context.arguments[1].AsInt();
                const auto target = context.arguments[2].ref;
                const auto target_pos = context.arguments[3].AsInt();
                const auto length = context.arguments[4].AsInt();
                if (!source.IsValid() || !target.IsValid()) {
                    throw VmJavaThrow{"Ljava/lang/NullPointerException;",
                                      "arraycopy on null array"};
                }
                if (length < 0 || source_pos < 0 || target_pos < 0 ||
                    source_pos > model.ArrayLength(source) - length ||
                    target_pos > model.ArrayLength(target) - length) {
              throw VmJavaThrow{"Ljava/lang/ArrayIndexOutOfBoundsException;",
                        "arraycopy range is out of bounds"};
                }
                const auto source_kind = model.Kind(source);
                const auto target_kind = model.Kind(target);
                if (source_kind == VmObjectKind::object_array &&
                    target_kind == VmObjectKind::object_array) {
                    // Overlap-safe copy: buffer the source range first.
                    std::vector<VmObjectRef> staged;
                    staged.reserve(static_cast<std::size_t>(length));
                    for (std::int32_t index = 0; index < length; ++index) {
                staged.push_back(model.GetObjectElement(source, source_pos + index));
                    }
                    for (std::int32_t index = 0; index < length; ++index) {
                        model.SetObjectElement(target, target_pos + index,
                                       staged[static_cast<std::size_t>(index)]);
                    }
                    return VmValue::Void();
                }
                if (source_kind == VmObjectKind::primitive_array &&
                    target_kind == VmObjectKind::primitive_array &&
                model.PrimitiveArrayKind(source) == model.PrimitiveArrayKind(target)) {
                    std::vector<std::uint64_t> staged;
                    staged.reserve(static_cast<std::size_t>(length));
                    for (std::int32_t index = 0; index < length; ++index) {
                staged.push_back(model.GetPrimitiveElement(source, source_pos + index));
                    }
                    for (std::int32_t index = 0; index < length; ++index) {
                        model.SetPrimitiveElement(target, target_pos + index,
                                          staged[static_cast<std::size_t>(index)]);
                    }
                    return VmValue::Void();
                }
                throw VmJavaThrow{"Ljava/lang/ArrayStoreException;",
                                  "arraycopy element types are incompatible"};
            });
    builder.StaticMethod("gc", "()V",
        [](IntrinsicContext&) {
                // GC-B remains allocation-flow driven (09 §7). Explicit
                // System.gc() is legal but intentionally does not force a cycle.
                return VmValue::Void();
            });
    builder.StaticMethod("identityHashCode", "(Ljava/lang/Object;)I",
        [](IntrinsicContext& context) {
                return VmValue::Int(context.vm.Model().IdentityHashCode(
                    context.arguments[0].ref));
            });
    builder.StaticMethod("getProperty", "(Ljava/lang/String;)Ljava/lang/String;",
        [](IntrinsicContext& context) {
                const auto key = PropertyKey(context, context.arguments[0].ref);
                const auto value = context.vm.GetSystemProperty(key);
                if (!value.has_value()) {
                    return VmValue::Ref(VmObjectRef{});
                }
                return VmValue::Ref(context.vm.NewStringUtf8(*value));
            });
    builder.StaticMethod("setProperty", "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;",
        [](IntrinsicContext& context) {
                const auto key = PropertyKey(context, context.arguments[0].ref);
                const auto value = PropertyValue(context, context.arguments[1].ref);
                const auto previous = context.vm.SetSystemProperty(key, value);
                return previous.has_value()
                           ? VmValue::Ref(context.vm.NewStringUtf8(*previous))
                           : VmValue::Ref(VmObjectRef{});
                });
    builder.UnimplementedStatic("currentTimeMillis", "()J");
    builder.UnimplementedStatic("nanoTime", "()J");
    builder.UnimplementedStatic("load", "(Ljava/lang/String;)V");
    builder.UnimplementedStatic("loadLibrary", "(Ljava/lang/String;)V");
    builder.UnimplementedStatic("exit", "(I)V");
    builder.ClassInitializer(
        [](IntrinsicContext& context) {
                auto& vm = context.vm;
            vm.SetIntrinsicStaticRef("Ljava/lang/System;", "out",
                                     "Ljava/io/PrintStream;",
                    vm.NewIntrinsicInstance("Ljava/io/PrintStream;"));
            vm.SetIntrinsicStaticRef("Ljava/lang/System;", "err",
                                     "Ljava/io/PrintStream;",
                    vm.NewIntrinsicInstance("Ljava/io/PrintStream;"));
                return VmValue::Void();
            });
    auto result = std::move(builder).Build();
    return result;
}

}  // namespace ogplay::runtime::dexvm::intrinsics

namespace ogplay::runtime::dexvm::intrinsics {
IntrinsicClassDecl Declare_java_lang_System() {
    return dvm80_java_lang_System::Declare_java_lang_System();
}
}  // namespace ogplay::runtime::dexvm::intrinsics

// ---- migrated from java_lang_Thread.cpp ----
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

namespace ogplay::runtime::dexvm::intrinsics::dvm80_java_lang_Thread {
    namespace {
        struct ThreadFields final {
            IntrinsicFieldHandle target;
            IntrinsicFieldHandle name;
            IntrinsicFieldHandle priority;
            IntrinsicFieldHandle daemon;
            IntrinsicFieldHandle stack_size;
            IntrinsicFieldHandle id;
            IntrinsicFieldHandle has_been_started;
        };

        void InitializeFields(const IntrinsicCall& call,
                              const ThreadFields& fields,
                              const VmObjectRef object,
                              const VmObjectRef target, const VmObjectRef name,
                              const std::int64_t id, const std::int32_t priority) {
            call.SetRef(fields.target, object, target);
            call.SetRef(fields.name, object, name);
            call.SetInt(fields.priority, object, priority);
            call.SetInt(fields.daemon, object, 0);
            call.SetLong(fields.stack_size, object, 0);
            call.SetLong(fields.id, object, id);
            call.SetInt(fields.has_been_started, object, 0);
        }

        [[nodiscard]] VmObjectRef EnsureCurrentThread(
            const IntrinsicCall& call, const ThreadFields& fields) {
            auto& vm = call.Vm();
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
            InitializeFields(call, fields, root, VmObjectRef{},
                             vm.NewStringUtf8("main"), 1, 5);
            return root;
        }

        [[nodiscard]] VmValue Construct(IntrinsicContext& context,
                                        const ThreadFields& fields,
                                        const VmObjectRef target,
                                        const VmObjectRef explicit_name,
                                        const bool has_explicit_name) {
            if (has_explicit_name && !explicit_name.IsValid()) {
                throw VmJavaThrow{
                    "Ljava/lang/NullPointerException;", "threadName == null"
                };
            }
            const IntrinsicCall call(context);
            auto& runtime = context.vm.Threads();
            const auto current = EnsureCurrentThread(call, fields);
            const auto priority = call.GetInt(fields.priority, current);
            const auto id = runtime.AllocateThreadId();
            const auto name = has_explicit_name
                                  ? explicit_name
                                  : context.vm.NewStringUtf8("Thread-" + std::to_string(id));
            InitializeFields(call, fields, context.receiver, target, name,
                             static_cast<std::int64_t>(id), priority);
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
        const ThreadFields fields{
            builder.BoundInstanceField("target", "Ljava/lang/Runnable;"),
            builder.BoundInstanceField("name", "Ljava/lang/String;"),
            builder.BoundInstanceField("priority", "I"),
            builder.BoundInstanceField("daemon", "Z"),
            builder.BoundInstanceField("stackSize", "J"),
            builder.BoundInstanceField("id", "J"),
            builder.BoundInstanceField("hasBeenStarted", "Z"),
        };
        builder.ConstantInt("MIN_PRIORITY", "I", 1)
                .ConstantInt("NORM_PRIORITY", "I", 5)
                .ConstantInt("MAX_PRIORITY", "I", 10);

        builder.Constructor("()V", [fields](IntrinsicContext& context) {
            return Construct(context, fields, VmObjectRef{}, VmObjectRef{}, false);
        });
        builder.Constructor("(Ljava/lang/Runnable;)V", [fields](IntrinsicContext& context) {
            IntrinsicCall call(context);
            return Construct(context, fields, call.Ref(0), VmObjectRef{}, false);
        });
        builder.Constructor("(Ljava/lang/String;)V", [fields](IntrinsicContext& context) {
            IntrinsicCall call(context);
            return Construct(context, fields, VmObjectRef{}, call.Ref(0), true);
        });
        builder.Constructor("(Ljava/lang/Runnable;Ljava/lang/String;)V", [fields](IntrinsicContext& context) {
            IntrinsicCall call(context);
            return Construct(context, fields, call.Ref(0), call.Ref(1), true);
        });

        // AOSP Thread.run: target dispatch belongs here. VmThreadRuntime always
        // dispatches virtual this.run(), so a Thread subclass override wins.
        builder.VirtualMethod("run", "()V", [fields](IntrinsicContext& context) {
            IntrinsicCall call(context);
            const auto target = call.GetRef(fields.target);
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

        builder.VirtualMethod("start", "()V", [fields](IntrinsicContext& context) {
            IntrinsicCall call(context);
            if (call.GetInt(fields.has_been_started) != 0) {
                throw VmJavaThrow{
                    "Ljava/lang/IllegalThreadStateException;", "Thread already started"
                };
            }
            call.SetInt(fields.has_been_started, 1);
            const auto name = call.GetRef(fields.name);
            context.vm.Threads().Start(
                context.receiver, context.vm.StringUtf8(name),
                static_cast<std::uint64_t>(call.GetLong(fields.id))
            );
            return VmValue::Void();
        });

        builder.StaticMethod("currentThread", "()Ljava/lang/Thread;", [fields](IntrinsicContext& context) {
            IntrinsicCall call(context);
            return VmValue::Ref(EnsureCurrentThread(call, fields));
        });
        builder.VirtualMethod("getId", "()J", [fields](IntrinsicContext& context) {
            return VmValue::Long(IntrinsicCall(context).GetLong(fields.id));
        });
        builder.FinalMethod("getName", "()Ljava/lang/String;", [fields](IntrinsicContext& context) {
            return VmValue::Ref(IntrinsicCall(context).GetRef(fields.name));
        });
        builder.FinalMethod("setName", "(Ljava/lang/String;)V", [fields](IntrinsicContext& context) {
            IntrinsicCall call(context);
            const auto name = call.NonNullRef(0, "threadName");
            call.SetRef(fields.name, name);
            context.vm.Threads().Rename(context.receiver, context.vm.StringUtf8(name));
            return VmValue::Void();
        });
        builder.FinalMethod("isAlive", "()Z", [](IntrinsicContext& context) {
            return VmValue::Int(context.vm.Threads().IsAlive(context.receiver) ? 1 : 0);
        });
        builder.FinalMethod("getPriority", "()I", [fields](IntrinsicContext& context) {
            return VmValue::Int(IntrinsicCall(context).GetInt(fields.priority));
        });
        builder.FinalMethod("setPriority", "(I)V", [fields](IntrinsicContext& context) {
            IntrinsicCall call(context);
            const auto priority = call.Int(0);
            if (priority < 1 || priority > 10) {
                throw VmJavaThrow{
                    "Ljava/lang/IllegalArgumentException;", "Priority out of range: " + std::to_string(priority)
                };
            }
            // Guest-visible only: host scheduler priority is
            // deliberately not modeled yet.
            call.SetInt(fields.priority, priority);
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
            IntrinsicCall call(context);
            const auto millis = call.Long(0);
            ValidateTimeout(millis, 0);
            if (millis == 0) {
                context.vm.Threads().Join(context.receiver);
            } else {
                context.vm.Threads().Join(context.receiver, millis);
            }
            return VmValue::Void();
        });
        builder.FinalMethod("join", "(JI)V", [](IntrinsicContext& context) {
            IntrinsicCall call(context);
            const auto millis = call.Long(0);
            const auto nanos = call.Int(1);
            ValidateTimeout(millis, nanos);
            if (millis == 0 && nanos == 0) {
                context.vm.Threads().Join(context.receiver);
            } else {
                context.vm.Threads().Join(context.receiver, RoundedMillis(millis, nanos));
            }
            return VmValue::Void();
        });

        builder.StaticMethod("sleep", "(J)V", [](IntrinsicContext& context) {
            IntrinsicCall call(context);
            const auto millis = call.Long(0);
            ValidateTimeout(millis, 0);
            context.vm.Threads().Sleep(millis);
            return VmValue::Void();
        });
        builder.StaticMethod("sleep", "(JI)V", [](IntrinsicContext& context) {
            IntrinsicCall call(context);
            const auto millis = call.Long(0);
            const auto nanos = call.Int(1);
            ValidateTimeout(millis, nanos);
            context.vm.Threads().Sleep(RoundedMillis(millis, nanos));
            return VmValue::Void();
        });
        builder.StaticMethod("yield", "()V", [](IntrinsicContext& context) {
            context.vm.Threads().Yield();
            return VmValue::Void();
        });
        builder.StaticMethod("holdsLock", "(Ljava/lang/Object;)Z", [](IntrinsicContext& context) {
            IntrinsicCall call(context);
            const auto object = call.NonNullRef(0, "object");
            return VmValue::Int(context.vm.Monitors().IsOwner(object, context.vm.CurrentContextToken()) ? 1 : 0);
        });

        builder.FinalMethod("isDaemon", "()Z", [fields](IntrinsicContext& context) {
            return VmValue::Int(IntrinsicCall(context).GetInt(fields.daemon));
        });
        builder.FinalMethod("setDaemon", "(Z)V", [fields](IntrinsicContext& context) {
            IntrinsicCall call(context);
            if (call.GetInt(fields.has_been_started) != 0) {
                throw VmJavaThrow{"Ljava/lang/IllegalThreadStateException;", "Thread already started"};
            }
            // Bounded semantics: the flag is exposed, but session termination is
            // not daemon-driven in OGPlay.
            call.SetInt(fields.daemon, call.Int(0) != 0 ? 1 : 0);
            return VmValue::Void();
        });

        builder.UnimplementedFinal("stop", "()V");
        builder.UnimplementedFinal("suspend", "()V");
        builder.UnimplementedFinal("resume", "()V");
        builder.UnimplementedFinal("destroy", "()V");
        return std::move(builder).Build();
    }
} // namespace ogplay::runtime::dexvm::intrinsics

namespace ogplay::runtime::dexvm::intrinsics {
IntrinsicClassDecl Declare_java_lang_Thread() {
    return dvm80_java_lang_Thread::Declare_java_lang_Thread();
}
}  // namespace ogplay::runtime::dexvm::intrinsics

// ---- migrated from java_lang_throwables.cpp ----
#include "catalog.h"
#include "shared.h"

#include "ogplay/runtime/dexvm/intrinsic_builder.h"

namespace ogplay::runtime::dexvm::intrinsics::dvm80_java_lang_throwables {
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
    return DeclareSimpleThrowable("Ljava/lang/ArithmeticException;", "Ljava/lang/RuntimeException;");
}

IntrinsicClassDecl Declare_java_lang_ArrayIndexOutOfBoundsException() {
    return DeclareSimpleThrowable("Ljava/lang/ArrayIndexOutOfBoundsException;", "Ljava/lang/IndexOutOfBoundsException;");
}

IntrinsicClassDecl Declare_java_lang_ArrayStoreException() {
    return DeclareSimpleThrowable("Ljava/lang/ArrayStoreException;", "Ljava/lang/RuntimeException;");
}

IntrinsicClassDecl Declare_java_lang_ClassCastException() {
    return DeclareSimpleThrowable("Ljava/lang/ClassCastException;", "Ljava/lang/RuntimeException;");
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
    return DeclareSimpleThrowable("Ljava/lang/Error;", "Ljava/lang/Throwable;");
}

IntrinsicClassDecl Declare_java_lang_Exception() {
    return DeclareSimpleThrowable("Ljava/lang/Exception;", "Ljava/lang/Throwable;");
}

IntrinsicClassDecl Declare_java_lang_IllegalArgumentException() {
    return DeclareSimpleThrowable("Ljava/lang/IllegalArgumentException;", "Ljava/lang/RuntimeException;");
}

IntrinsicClassDecl Declare_java_lang_IllegalMonitorStateException() {
    return DeclareSimpleThrowable("Ljava/lang/IllegalMonitorStateException;", "Ljava/lang/RuntimeException;");
}

IntrinsicClassDecl Declare_java_lang_IllegalStateException() {
    return DeclareSimpleThrowable("Ljava/lang/IllegalStateException;", "Ljava/lang/RuntimeException;");
}

IntrinsicClassDecl Declare_java_lang_IllegalThreadStateException() {
    return DeclareSimpleThrowable("Ljava/lang/IllegalThreadStateException;", "Ljava/lang/IllegalArgumentException;");
}

IntrinsicClassDecl Declare_java_lang_IndexOutOfBoundsException() {
    return DeclareSimpleThrowable("Ljava/lang/IndexOutOfBoundsException;", "Ljava/lang/RuntimeException;");
}

IntrinsicClassDecl Declare_java_lang_InterruptedException() {
    return DeclareSimpleThrowable("Ljava/lang/InterruptedException;", "Ljava/lang/Exception;");
}

IntrinsicClassDecl Declare_java_lang_LinkageError() {
    return DeclareSimpleThrowable("Ljava/lang/LinkageError;", "Ljava/lang/Error;");
}

IntrinsicClassDecl Declare_java_lang_NegativeArraySizeException() {
    return DeclareSimpleThrowable("Ljava/lang/NegativeArraySizeException;", "Ljava/lang/RuntimeException;");
}

IntrinsicClassDecl Declare_java_lang_NoClassDefFoundError() {
    return DeclareSimpleThrowable("Ljava/lang/NoClassDefFoundError;", "Ljava/lang/LinkageError;");
}

IntrinsicClassDecl Declare_java_lang_NullPointerException() {
    return DeclareSimpleThrowable("Ljava/lang/NullPointerException;", "Ljava/lang/RuntimeException;");
}

IntrinsicClassDecl Declare_java_lang_NumberFormatException() {
    return DeclareSimpleThrowable("Ljava/lang/NumberFormatException;", "Ljava/lang/IllegalArgumentException;");
}

IntrinsicClassDecl Declare_java_lang_OutOfMemoryError() {
    return DeclareSimpleThrowable("Ljava/lang/OutOfMemoryError;", "Ljava/lang/VirtualMachineError;");
}

IntrinsicClassDecl Declare_java_lang_RuntimeException() {
    return DeclareSimpleThrowable("Ljava/lang/RuntimeException;", "Ljava/lang/Exception;");
}

IntrinsicClassDecl Declare_java_lang_StackOverflowError() {
    return DeclareSimpleThrowable("Ljava/lang/StackOverflowError;", "Ljava/lang/VirtualMachineError;");
}

IntrinsicClassDecl Declare_java_lang_StringIndexOutOfBoundsException() {
    return DeclareSimpleThrowable("Ljava/lang/StringIndexOutOfBoundsException;", "Ljava/lang/IndexOutOfBoundsException;");
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
    return DeclareSimpleThrowable("Ljava/lang/UnsatisfiedLinkError;", "Ljava/lang/LinkageError;");
}

IntrinsicClassDecl Declare_java_lang_UnsupportedOperationException() {
    return DeclareSimpleThrowable("Ljava/lang/UnsupportedOperationException;", "Ljava/lang/RuntimeException;");
}

IntrinsicClassDecl Declare_java_lang_VirtualMachineError() {
    return DeclareSimpleThrowable("Ljava/lang/VirtualMachineError;", "Ljava/lang/Error;");
}

IntrinsicClassDecl Declare_java_lang_AbstractMethodError() {
    return DeclareSimpleThrowable("Ljava/lang/AbstractMethodError;", "Ljava/lang/IncompatibleClassChangeError;");
}

IntrinsicClassDecl Declare_java_lang_ClassCircularityError() {
    return DeclareSimpleThrowable("Ljava/lang/ClassCircularityError;", "Ljava/lang/LinkageError;");
}

IntrinsicClassDecl Declare_java_lang_ClassFormatError() {
    return DeclareSimpleThrowable("Ljava/lang/ClassFormatError;", "Ljava/lang/LinkageError;");
}

IntrinsicClassDecl Declare_java_lang_CloneNotSupportedException() {
    return DeclareSimpleThrowable("Ljava/lang/CloneNotSupportedException;", "Ljava/lang/Exception;");
}

IntrinsicClassDecl Declare_java_lang_IllegalAccessError() {
    return DeclareSimpleThrowable("Ljava/lang/IllegalAccessError;", "Ljava/lang/IncompatibleClassChangeError;");
}

IntrinsicClassDecl Declare_java_lang_IllegalAccessException() {
    return DeclareSimpleThrowable("Ljava/lang/IllegalAccessException;", "Ljava/lang/ReflectiveOperationException;");
}

IntrinsicClassDecl Declare_java_lang_IncompatibleClassChangeError() {
    return DeclareSimpleThrowable("Ljava/lang/IncompatibleClassChangeError;", "Ljava/lang/LinkageError;");
}

IntrinsicClassDecl Declare_java_lang_InternalError() {
    return DeclareSimpleThrowable("Ljava/lang/InternalError;", "Ljava/lang/VirtualMachineError;");
}

IntrinsicClassDecl Declare_java_lang_NoSuchFieldError() {
    return DeclareSimpleThrowable("Ljava/lang/NoSuchFieldError;", "Ljava/lang/IncompatibleClassChangeError;");
}

IntrinsicClassDecl Declare_java_lang_NoSuchFieldException() {
    return DeclareSimpleThrowable("Ljava/lang/NoSuchFieldException;", "Ljava/lang/ReflectiveOperationException;");
}

IntrinsicClassDecl Declare_java_lang_NoSuchMethodError() {
    return DeclareSimpleThrowable("Ljava/lang/NoSuchMethodError;", "Ljava/lang/IncompatibleClassChangeError;");
}

IntrinsicClassDecl Declare_java_lang_NoSuchMethodException() {
    return DeclareSimpleThrowable("Ljava/lang/NoSuchMethodException;", "Ljava/lang/ReflectiveOperationException;");
}

IntrinsicClassDecl Declare_java_lang_UnknownError() {
    return DeclareSimpleThrowable("Ljava/lang/UnknownError;", "Ljava/lang/VirtualMachineError;");
}

IntrinsicClassDecl Declare_java_lang_UnsupportedClassVersionError() {
    return DeclareSimpleThrowable("Ljava/lang/UnsupportedClassVersionError;", "Ljava/lang/ClassFormatError;");
}

IntrinsicClassDecl Declare_java_lang_VerifyError() {
    return DeclareSimpleThrowable("Ljava/lang/VerifyError;", "Ljava/lang/LinkageError;");
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

namespace ogplay::runtime::dexvm::intrinsics {
void AppendJavaLangThrowables(std::vector<IntrinsicClassDecl>& catalog) {
    dvm80_java_lang_throwables::AppendJavaLangThrowables(catalog);
}
}  // namespace ogplay::runtime::dexvm::intrinsics
