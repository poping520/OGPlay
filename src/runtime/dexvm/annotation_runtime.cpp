#include "ogplay/runtime/dexvm/annotation_runtime.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ogplay/core/text.h"
#include "ogplay/runtime/dexvm/access_flags.h"
#include "ogplay/runtime/dexvm/class_name_codec.h"
#include "ogplay/runtime/dexvm/reflection_codec.h"

namespace ogplay::runtime::dexvm {
namespace {

constexpr std::string_view kInherited = "Ljava/lang/annotation/Inherited;";
constexpr std::string_view kAnnotation = "Ljava/lang/annotation/Annotation;";

[[nodiscard]] std::int32_t JavaStringHash(const std::u16string& value) {
    std::int32_t hash = 0;
    for (const auto unit : value) {
        hash = static_cast<std::int32_t>(
            static_cast<std::uint32_t>(hash) * 31U + unit);
    }
    return hash;
}

[[nodiscard]] std::int32_t JavaUtf8Hash(const std::string_view value) {
    std::int32_t hash = 0;
    for (const auto byte : value) {
        hash = static_cast<std::int32_t>(
            static_cast<std::uint32_t>(hash) * 31U +
            static_cast<unsigned char>(byte));
    }
    return hash;
}

[[nodiscard]] char ReturnShorty(const std::string_view descriptor) {
    const auto parsed = ClassNameCodec::ParseMethod(std::string(descriptor));
    if (parsed.return_type.size() == 1U) return parsed.return_type.front();
    return 'L';
}

[[nodiscard]] std::string ReturnType(const std::string_view descriptor) {
    return ClassNameCodec::ParseMethod(std::string(descriptor)).return_type;
}

[[nodiscard]] bool IsArrayDescriptor(const std::string_view descriptor) {
    return !descriptor.empty() && descriptor.front() == '[';
}

[[nodiscard]] VmValue BitsToValue(const char shorty, const std::int64_t bits,
                                  const double floating) {
    switch (shorty) {
    case 'Z':
    case 'B':
    case 'S':
    case 'C':
    case 'I':
        return VmValue::Int(static_cast<std::int32_t>(bits));
    case 'J':
        return VmValue::Long(bits);
    case 'F':
        return VmValue::Float(static_cast<float>(floating));
    case 'D':
        return VmValue::Double(floating);
    default:
        return VmValue::Ref(VmObjectRef{});
    }
}

[[nodiscard]] JniPrimitiveKind PrimitiveKindOf(const char shorty) {
    switch (shorty) {
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
    case 'D':
        return JniPrimitiveKind::double_value;
    default:
        throw DexVmError(DexVmErrorReason::internal_invariant,
                         "annotation array element is not primitive");
    }
}

[[nodiscard]] bool KindMatches(const LinkedAnnotationValue& value,
                               const std::string_view expected) {
    using Kind = LinkedAnnotationValue::Kind;
    if (value.kind == Kind::null_reference) {
        return ClassNameCodec::IsReference(expected);
    }
    if (IsArrayDescriptor(expected)) {
        return value.kind == Kind::array;
    }
    switch (value.kind) {
    case Kind::byte_value:
        return expected == "B";
    case Kind::short_value:
        return expected == "S";
    case Kind::char_value:
        return expected == "C";
    case Kind::int_value:
        return expected == "I";
    case Kind::long_value:
        return expected == "J";
    case Kind::float_value:
        return expected == "F";
    case Kind::double_value:
        return expected == "D";
    case Kind::boolean_value:
        return expected == "Z";
    case Kind::string_value:
        return expected == "Ljava/lang/String;";
    case Kind::type_descriptor:
        return expected == "Ljava/lang/Class;";
    case Kind::enum_constant:
        return expected == value.descriptor;
    case Kind::annotation:
        return expected == value.nested_type;
    default:
        return false;
    }
}

[[noreturn]] void ThrowIncomplete(const std::string_view type,
                                  const std::string_view member) {
    throw VmJavaThrow{"Ljava/lang/annotation/IncompleteAnnotationException;",
                      std::string(type) + " missing " + std::string(member)};
}

[[noreturn]] void ThrowMismatch(const std::string_view found) {
    throw VmJavaThrow{
        "Ljava/lang/annotation/AnnotationTypeMismatchException;",
        std::string(found)};
}

[[noreturn]] void ThrowMissingType(const std::string_view descriptor) {
    throw VmJavaThrow{"Ljava/lang/TypeNotPresentException;",
                      std::string(descriptor)};
}

[[noreturn]] void ThrowMissingEnum(const std::string_view type,
                                   const std::string_view name) {
    throw VmJavaThrow{"Ljava/lang/EnumConstantNotPresentException;",
                      std::string(type) + "." + std::string(name)};
}

}  // namespace

AnnotationRuntime::AnnotationRuntime(Interpreter& interpreter)
    : interpreter_(&interpreter),
      instances_(
          "annotations", OwnedStateTracePolicy::trace_guest_references,
          OwnedStateClonePolicy::clone_state,
          [this](const InstanceState& state, const VmRootVisitor& visit) {
              TraceState(state, visit);
          }) {}

IntrinsicStateTableHooks AnnotationRuntime::Hooks() {
    return instances_.Hooks();
}

DexClassId AnnotationRuntime::RequireAnnotationClass(
    const VmObjectRef class_object) {
    if (!class_object.IsValid()) {
        throw VmJavaThrow{"Ljava/lang/NullPointerException;",
                          "annotation class == null"};
    }
    return interpreter_->Model().ClassOfClassObject(class_object);
}

DexClassId AnnotationRuntime::EnsureImplClass(const DexClassId annotation_type) {
    if (const auto found = impl_classes_.find(annotation_type.Value());
        found != impl_classes_.end()) {
        return found->second.impl;
    }
    auto& linker = interpreter_->Linker();
    linker.EnsureClassLinked(annotation_type);
    const auto& annotation = linker.Class(annotation_type);
    std::vector<IntrinsicMethodDecl> methods;
    ImplClassInfo info;
    for (const auto method_id : annotation.own_virtual_methods) {
        const auto& method = linker.Method(method_id);
        const auto parsed = ClassNameCodec::ParseMethod(method.descriptor);
        if (method.is_static || !parsed.parameters.empty()) continue;
        IntrinsicMethodDecl decl;
        decl.name = method.name;
        decl.descriptor = method.descriptor;
        decl.access_flags = kAccPublic | kAccFinal;
        decl.overridable = false;
        decl.invoke_kind = DeclaredInvokeKind::virtual_call;
        const auto index = info.names.size();
        decl.implementation = [this, index](IntrinsicContext& context) {
            return ReadMember(context.receiver, index);
        };
        methods.push_back(std::move(decl));
        info.names.push_back(method.name);
        info.descriptors.push_back(method.descriptor);
    }
    const auto make_override = [this](std::string name, std::string descriptor,
                                      IntrinsicHandler handler) {
        IntrinsicMethodDecl decl;
        decl.name = std::move(name);
        decl.descriptor = std::move(descriptor);
        decl.access_flags = kAccPublic | kAccFinal;
        decl.overridable = false;
        decl.must_override = true;
        decl.invoke_kind = DeclaredInvokeKind::virtual_call;
        decl.implementation = std::move(handler);
        return decl;
    };
    methods.push_back(make_override(
        "equals", "(Ljava/lang/Object;)Z",
        [this](IntrinsicContext& context) {
            return Equals(context.receiver, context.arguments[0].ref);
        }));
    methods.push_back(make_override(
        "hashCode", "()I",
        [this](IntrinsicContext& context) { return HashCode(context.receiver); }));
    methods.push_back(make_override(
        "toString", "()Ljava/lang/String;",
        [this](IntrinsicContext& context) { return ToString(context.receiver); }));
    IntrinsicMethodDecl annotation_type_method;
    annotation_type_method.name = "annotationType";
    annotation_type_method.descriptor = "()Ljava/lang/Class;";
    annotation_type_method.access_flags = kAccPublic | kAccFinal;
    annotation_type_method.invoke_kind = DeclaredInvokeKind::virtual_call;
    annotation_type_method.implementation = [this](IntrinsicContext& context) {
        return AnnotationType(context.receiver);
    };
    methods.push_back(std::move(annotation_type_method));
    info.impl = linker.DefineRestrictedAnnotationClass(annotation_type, methods);
    impl_classes_.emplace(annotation_type.Value(), info);
    return info.impl;
}

VmObjectRef AnnotationRuntime::Materialize(const LinkedAnnotation& annotation) {
    auto& linker = interpreter_->Linker();
    DexClassId type;
    try {
        type = linker.ResolveDescriptor(annotation.type_descriptor);
    } catch (const DexVmError& error) {
        if (error.Reason() == DexVmErrorReason::unknown_class ||
            error.Reason() == DexVmErrorReason::unresolved_reference) {
            ThrowMissingType(annotation.type_descriptor);
        }
        throw;
    }
    const auto impl = EnsureImplClass(type);
    const auto instance = interpreter_->Model().NewInstance(
        impl, linker.Class(impl).instance_slots);
    const auto roots = interpreter_->ProtectReferences(std::array{instance});
    InstanceState state;
    state.annotation_type = type;
    BindMembers(state, type, annotation.elements);
    instances_.InsertOrAssign(instance, std::move(state));
    static_cast<void>(roots);
    return instance;
}

void AnnotationRuntime::BindMembers(
    InstanceState& state, const DexClassId annotation_type,
    const std::vector<std::pair<std::string, LinkedAnnotationValue>>&
        elements) {
    auto& linker = interpreter_->Linker();
    linker.EnsureClassLinked(annotation_type);
    const auto& linked = linker.Class(annotation_type);
    const auto& defaults = linked.annotation_defaults;
    for (const auto method_id : linked.own_virtual_methods) {
        const auto& method = linker.Method(method_id);
        const auto parsed = ClassNameCodec::ParseMethod(method.descriptor);
        if (method.is_static || !parsed.parameters.empty()) continue;
        MemberState member;
        member.name = method.name;
        member.descriptor = method.descriptor;
        if (const auto* explicit_value = FindNamed(elements, method.name);
            explicit_value != nullptr) {
            member.present = true;
            member.encoded = *explicit_value;
        } else if (const auto* default_value = FindNamed(defaults, method.name);
                   default_value != nullptr) {
            member.present = true;
            member.encoded = *default_value;
        }
        state.members.push_back(std::move(member));
    }
}

const LinkedAnnotationValue* AnnotationRuntime::FindNamed(
    const std::vector<std::pair<std::string, LinkedAnnotationValue>>& elements,
    const std::string_view name) const {
    const auto found = std::find_if(
        elements.begin(), elements.end(),
        [&](const auto& item) { return item.first == name; });
    return found == elements.end() ? nullptr : &found->second;
}

const LinkedAnnotation* AnnotationRuntime::FindDeclared(
    const std::vector<LinkedAnnotation>& annotations,
    const std::string_view descriptor) const {
    const auto found = std::find_if(
        annotations.begin(), annotations.end(),
        [&](const auto& item) { return item.type_descriptor == descriptor; });
    return found == annotations.end() ? nullptr : &*found;
}

bool AnnotationRuntime::AnnotationTypeIsInherited(
    const DexClassId annotation_type) {
    auto& linker = interpreter_->Linker();
    linker.EnsureClassLinked(annotation_type);
    return FindDeclared(linker.Class(annotation_type).runtime_annotations,
                        kInherited) != nullptr;
}

VmObjectRef AnnotationRuntime::EmptyAnnotationArray() {
    return MakeAnnotationArray({});
}

VmObjectRef AnnotationRuntime::MakeAnnotationArray(
    const std::vector<VmObjectRef>& values) {
    auto& linker = interpreter_->Linker();
    const auto element = linker.ResolveDescriptor(kAnnotation);
    const auto array_class = linker.ResolveDescriptor("[Ljava/lang/annotation/Annotation;");
    const auto array = interpreter_->Model().NewObjectArray(
        array_class, element, static_cast<JniSize>(values.size()));
    const auto roots = interpreter_->ProtectReferences(std::array{array});
    for (std::size_t index = 0; index < values.size(); ++index) {
        interpreter_->Model().SetObjectElement(
            array, static_cast<JniSize>(index), values[index]);
    }
    static_cast<void>(roots);
    return array;
}

VmObjectRef AnnotationRuntime::GetClassAnnotation(
    const DexClassId java_class, const VmObjectRef annotation_class,
    const bool inherited) {
    const auto requested = RequireAnnotationClass(annotation_class);
    auto& linker = interpreter_->Linker();
    linker.EnsureClassLinked(java_class);
    const auto& descriptor = linker.Class(requested).descriptor;
    if (const auto* found = FindDeclared(
            linker.Class(java_class).runtime_annotations, descriptor);
        found != nullptr) {
        return Materialize(*found);
    }
        if (!inherited || !AnnotationTypeIsInherited(requested)) {
            return VmObjectRef{};
        }
    auto current = linker.Class(java_class).super;
    while (current.has_value()) {
        linker.EnsureClassLinked(*current);
        const auto& linked = linker.Class(*current);
        if (linked.is_interface) break;
        if (const auto* found =
                FindDeclared(linked.runtime_annotations, descriptor);
            found != nullptr) {
            return Materialize(*found);
        }
        current = linked.super;
    }
    return VmObjectRef{};
}

bool AnnotationRuntime::IsClassAnnotationPresent(
    const DexClassId java_class, const VmObjectRef annotation_class) {
    return GetClassAnnotation(java_class, annotation_class, true).IsValid();
}

VmObjectRef AnnotationRuntime::GetClassAnnotations(const DexClassId java_class,
                                                   const bool inherited) {
    auto& linker = interpreter_->Linker();
    linker.EnsureClassLinked(java_class);
    std::vector<VmObjectRef> values;
    std::vector<std::string> seen;
    auto current = java_class;
    bool first = true;
    while (current.IsValid()) {
        linker.EnsureClassLinked(current);
        const auto& linked = linker.Class(current);
        for (const auto& annotation : linked.runtime_annotations) {
            if (std::find(seen.begin(), seen.end(), annotation.type_descriptor) !=
                seen.end()) {
                continue;
            }
            if (!first) {
                try {
                    const auto type =
                        linker.ResolveDescriptor(annotation.type_descriptor);
                    if (!AnnotationTypeIsInherited(type)) continue;
                } catch (const DexVmError&) {
                    continue;
                }
            }
            seen.push_back(annotation.type_descriptor);
            values.push_back(Materialize(annotation));
        }
        if (!inherited) break;
        if (!linked.super.has_value()) break;
        first = false;
        current = *linked.super;
        if (linker.Class(current).is_interface) break;
    }
    return MakeAnnotationArray(values);
}

VmObjectRef AnnotationRuntime::GetFieldAnnotation(
    const VmFieldId field, const VmObjectRef annotation_class) {
    const auto requested = RequireAnnotationClass(annotation_class);
    const auto& descriptor =
        interpreter_->Linker().Class(requested).descriptor;
    const auto* found = FindDeclared(
        interpreter_->Linker().Field(field).runtime_annotations, descriptor);
    return found == nullptr ? VmObjectRef{} : Materialize(*found);
}

bool AnnotationRuntime::IsFieldAnnotationPresent(
    const VmFieldId field, const VmObjectRef annotation_class) {
    return GetFieldAnnotation(field, annotation_class).IsValid();
}

VmObjectRef AnnotationRuntime::GetFieldAnnotations(const VmFieldId field) {
    std::vector<VmObjectRef> values;
    for (const auto& annotation :
         interpreter_->Linker().Field(field).runtime_annotations) {
        values.push_back(Materialize(annotation));
    }
    return MakeAnnotationArray(values);
}

VmObjectRef AnnotationRuntime::GetDefaultValue(const VmMethodId method) {
    auto& linker = interpreter_->Linker();
    const auto& linked_method = linker.Method(method);
    const auto& owner = linker.Class(linked_method.owner);
    if ((owner.access_flags & kAccAnnotation) == 0U) return VmObjectRef{};
    const auto* found = FindNamed(owner.annotation_defaults, linked_method.name);
    if (found == nullptr) return VmObjectRef{};
    const auto expected = ReturnType(linked_method.descriptor);
    const auto value = MaterializeEncoded(*found, expected);
    ReflectionCodec codec(*interpreter_, linker, interpreter_->Model());
    const auto return_type = linker.ResolveDescriptor(expected);
    return codec.BoxReturn(return_type, value);
}

AnnotationRuntime::InstanceState& AnnotationRuntime::RequireInstance(
    const VmObjectRef instance) {
    auto* state = instances_.Find(instance);
    if (state == nullptr) {
        throw DexVmError(DexVmErrorReason::internal_invariant,
                         "annotation instance has no host state");
    }
    return *state;
}

VmValue AnnotationRuntime::ReadMember(const VmObjectRef instance,
                                      const std::size_t index) {
    auto& state = RequireInstance(instance);
    auto value = MemberValue(state, index);
    if (value.kind == VmValue::Kind::ref) {
        value.ref = CloneIfArray(value.ref);
    }
    return value;
}

VmValue AnnotationRuntime::MemberValue(InstanceState& state,
                                       const std::size_t index) {
    if (index >= state.members.size()) {
        throw DexVmError(DexVmErrorReason::internal_invariant,
                         "annotation member index is invalid");
    }
    auto& member = state.members[index];
    if (!member.present) {
        ThrowIncomplete(
            interpreter_->Linker().Class(state.annotation_type).descriptor,
            member.name);
    }
    if (!member.materialized) {
        member.value =
            MaterializeEncoded(member.encoded, ReturnType(member.descriptor));
        member.materialized = true;
    }
    return member.value;
}

VmValue AnnotationRuntime::AnnotationType(const VmObjectRef instance) {
    const auto& state = RequireInstance(instance);
    return VmValue::Ref(
        interpreter_->Model().ClassObject(state.annotation_type));
}

VmValue AnnotationRuntime::Equals(const VmObjectRef instance,
                                  const VmObjectRef other) {
    if (!other.IsValid() || instances_.Find(other) == nullptr) {
        return VmValue::Int(0);
    }
    auto& left = RequireInstance(instance);
    auto& right = RequireInstance(other);
    if (left.annotation_type != right.annotation_type) return VmValue::Int(0);
    if (left.members.size() != right.members.size()) return VmValue::Int(0);
    for (std::size_t index = 0; index < left.members.size(); ++index) {
        const auto expected = ReturnType(left.members[index].descriptor);
        if (!ValuesEqual(MemberValue(left, index), MemberValue(right, index),
                         expected)) {
            return VmValue::Int(0);
        }
    }
    return VmValue::Int(1);
}

VmValue AnnotationRuntime::HashCode(const VmObjectRef instance) {
    auto& state = RequireInstance(instance);
    std::int32_t hash = 0;
    for (std::size_t index = 0; index < state.members.size(); ++index) {
        const auto expected = ReturnType(state.members[index].descriptor);
        hash += 127 * JavaUtf8Hash(state.members[index].name) ^
                ValueHash(MemberValue(state, index), expected);
    }
    return VmValue::Int(hash);
}

VmValue AnnotationRuntime::ToString(const VmObjectRef instance) {
    auto& state = RequireInstance(instance);
    std::ostringstream text;
    text << '@'
         << ClassNameCodec::ClassGetName(
                interpreter_->Linker().Class(state.annotation_type).descriptor);
    text << '(';
    for (std::size_t index = 0; index < state.members.size(); ++index) {
        if (index != 0U) text << ", ";
        text << state.members[index].name << '=';
        text << ValueText(MemberValue(state, index),
                          ReturnType(state.members[index].descriptor));
    }
    text << ')';
    return VmValue::Ref(interpreter_->NewStringUtf8(text.str()));
}

VmValue AnnotationRuntime::MaterializeEncoded(
    const LinkedAnnotationValue& encoded, const std::string_view expected) {
    using Kind = LinkedAnnotationValue::Kind;
    if (encoded.kind == Kind::unsupported || !KindMatches(encoded, expected)) {
        ThrowMismatch(expected);
    }
    auto& linker = interpreter_->Linker();
    auto& model = interpreter_->Model();
    switch (encoded.kind) {
    case Kind::byte_value:
    case Kind::short_value:
    case Kind::char_value:
    case Kind::int_value:
    case Kind::boolean_value:
        return VmValue::Int(static_cast<std::int32_t>(encoded.integral));
    case Kind::long_value:
        return VmValue::Long(encoded.integral);
    case Kind::float_value:
        return VmValue::Float(static_cast<float>(encoded.floating));
    case Kind::double_value:
        return VmValue::Double(encoded.floating);
    case Kind::string_value:
        return VmValue::Ref(model.InternString(encoded.text));
    case Kind::type_descriptor: {
        try {
            const auto type = linker.ResolveDescriptor(encoded.descriptor);
            return VmValue::Ref(model.ClassObject(type));
        } catch (const DexVmError& error) {
            if (error.Reason() == DexVmErrorReason::unknown_class ||
                error.Reason() == DexVmErrorReason::unresolved_reference) {
                ThrowMissingType(encoded.descriptor);
            }
            throw;
        }
    }
    case Kind::enum_constant: {
        DexClassId type;
        try {
            type = linker.ResolveDescriptor(encoded.descriptor);
        } catch (const DexVmError& error) {
            if (error.Reason() == DexVmErrorReason::unknown_class ||
                error.Reason() == DexVmErrorReason::unresolved_reference) {
                ThrowMissingType(encoded.descriptor);
            }
            throw;
        }
        const auto initialized = interpreter_->EnsureClassInitialized(type);
        if (initialized.exception.IsValid()) {
            throw VmJavaThrow{
                linker.Class(initialized.exception_class).descriptor,
                initialized.exception_message, initialized.exception};
        }
        const auto field = linker.FindFieldRecursive(type, encoded.name,
                                                     encoded.descriptor);
        if (!field.has_value()) {
            ThrowMissingEnum(encoded.descriptor, encoded.name);
        }
        const auto& linked = linker.Class(type);
        const auto& field_linked = linker.Field(*field);
        if (field_linked.slot >= linked.static_storage.size()) {
            ThrowMissingEnum(encoded.descriptor, encoded.name);
        }
        const auto object = VmObjectRef(linked.static_storage[field_linked.slot]);
        if (!object.IsValid()) {
            ThrowMissingEnum(encoded.descriptor, encoded.name);
        }
        return VmValue::Ref(object);
    }
    case Kind::array:
        return VmValue::Ref(MaterializeArray(encoded, expected.substr(1)).ref);
    case Kind::annotation: {
        LinkedAnnotation nested;
        nested.type_descriptor = encoded.nested_type;
        nested.elements = encoded.nested_elements;
        return VmValue::Ref(Materialize(nested));
    }
    case Kind::null_reference:
        return VmValue::Ref(VmObjectRef{});
    default:
        ThrowMismatch(expected);
    }
}

VmValue AnnotationRuntime::MaterializeArray(
    const LinkedAnnotationValue& encoded, const std::string_view component) {
    auto& linker = interpreter_->Linker();
    auto& model = interpreter_->Model();
    const auto array_descriptor = "[" + std::string(component);
    DexClassId array_class;
    try {
        array_class = linker.ResolveDescriptor(array_descriptor);
    } catch (const DexVmError& error) {
        if (error.Reason() == DexVmErrorReason::unknown_class ||
            error.Reason() == DexVmErrorReason::unresolved_reference) {
            ThrowMissingType(array_descriptor);
        }
        throw;
    }
    const auto length = static_cast<JniSize>(encoded.values.size());
    if (component.size() == 1U &&
        std::string_view("ZBSCIJFD").find(component) != std::string_view::npos) {
        const auto array = model.NewPrimitiveArray(
            array_class, PrimitiveKindOf(component.front()), length);
        const auto roots = interpreter_->ProtectReferences(std::array{array});
        for (JniSize index = 0; index < length; ++index) {
            const auto& item = encoded.values[static_cast<std::size_t>(index)];
            if (!KindMatches(item, component)) ThrowMismatch(component);
            const auto value = MaterializeEncoded(item, component);
            const auto bits = component.front() == 'F' || component.front() == 'D'
                                  ? (component.front() == 'D'
                                         ? value.wide
                                         : static_cast<std::uint64_t>(value.cat1))
                                  : (component.front() == 'J' ? value.wide
                                                              : value.cat1);
            model.SetPrimitiveElement(array, index, bits);
        }
        static_cast<void>(roots);
        return VmValue::Ref(array);
    }
    const auto element = linker.ResolveDescriptor(component);
    const auto array = model.NewObjectArray(array_class, element, length);
    const auto roots = interpreter_->ProtectReferences(std::array{array});
    for (JniSize index = 0; index < length; ++index) {
        const auto& item = encoded.values[static_cast<std::size_t>(index)];
        if (!KindMatches(item, component)) ThrowMismatch(component);
        const auto value = MaterializeEncoded(item, component);
        model.SetObjectElement(array, index, value.ref);
    }
    static_cast<void>(roots);
    return VmValue::Ref(array);
}

VmObjectRef AnnotationRuntime::CloneIfArray(const VmObjectRef value) {
    if (!value.IsValid()) return value;
    const auto kind = interpreter_->Model().Kind(value);
    if (kind != VmObjectKind::primitive_array &&
        kind != VmObjectKind::object_array) {
        return value;
    }
    return interpreter_->Model().CloneObject(value);
}

bool AnnotationRuntime::ValuesEqual(const VmValue& left, const VmValue& right,
                                    const std::string_view descriptor) {
    if (IsArrayDescriptor(descriptor)) {
        return ArraysEqual(left.ref, right.ref, descriptor);
    }
    switch (ReturnShorty("()" + std::string(descriptor))) {
    case 'Z':
    case 'B':
    case 'S':
    case 'C':
    case 'I':
        return left.cat1 == right.cat1;
    case 'J':
        return left.wide == right.wide;
    case 'F':
        return left.cat1 == right.cat1;
    case 'D':
        return left.wide == right.wide;
    default:
        if (!left.ref.IsValid() || !right.ref.IsValid()) {
            return left.ref.IsValid() == right.ref.IsValid();
        }
        if (instances_.Find(left.ref) != nullptr) {
            return Equals(left.ref, right.ref).AsInt() != 0;
        }
        return interpreter_->JavaEquals(left.ref, right.ref);
    }
}

bool AnnotationRuntime::ArraysEqual(const VmObjectRef left,
                                    const VmObjectRef right,
                                    const std::string_view descriptor) {
    auto& model = interpreter_->Model();
    if (!left.IsValid() || !right.IsValid()) return left.Value() == right.Value();
    if (model.ArrayLength(left) != model.ArrayLength(right)) return false;
    const auto length = model.ArrayLength(left);
    const auto component = descriptor.substr(1);
    if (component.size() == 1U &&
        std::string_view("ZBSCIJFD").find(component) != std::string_view::npos) {
        for (JniSize index = 0; index < length; ++index) {
            if (model.GetPrimitiveElement(left, index) !=
                model.GetPrimitiveElement(right, index)) {
                return false;
            }
        }
        return true;
    }
    for (JniSize index = 0; index < length; ++index) {
        const auto lhs = model.GetObjectElement(left, index);
        const auto rhs = model.GetObjectElement(right, index);
        if (!ValuesEqual(VmValue::Ref(lhs), VmValue::Ref(rhs), component)) {
            return false;
        }
    }
    return true;
}

std::int32_t AnnotationRuntime::ValueHash(const VmValue& value,
                                          const std::string_view descriptor) {
    if (IsArrayDescriptor(descriptor)) return ArrayHash(value.ref, descriptor);
    switch (ReturnShorty("()" + std::string(descriptor))) {
    case 'Z':
        return value.AsInt() != 0 ? 1231 : 1237;
    case 'B':
    case 'S':
    case 'C':
    case 'I':
    case 'F':
        return value.AsInt();
    case 'J':
    case 'D': {
        const auto bits = value.wide;
        return static_cast<std::int32_t>(bits ^ (bits >> 32U));
    }
    default:
        return ObjectHash(value.ref);
    }
}

std::int32_t AnnotationRuntime::ArrayHash(const VmObjectRef array,
                                          const std::string_view descriptor) {
    if (!array.IsValid()) return 0;
    auto& model = interpreter_->Model();
    std::int32_t hash = 1;
    const auto length = model.ArrayLength(array);
    const auto component = descriptor.substr(1);
    if (component.size() == 1U &&
        std::string_view("ZBSCIJFD").find(component) != std::string_view::npos) {
        for (JniSize index = 0; index < length; ++index) {
            const auto bits = model.GetPrimitiveElement(array, index);
            std::int32_t element = 0;
            if (component.front() == 'Z') {
                element = (bits & 1U) != 0U ? 1231 : 1237;
            } else if (component.front() == 'J' || component.front() == 'D') {
                element = static_cast<std::int32_t>(bits ^ (bits >> 32U));
            } else {
                element = static_cast<std::int32_t>(bits);
            }
            hash = 31 * hash + element;
        }
        return hash;
    }
    for (JniSize index = 0; index < length; ++index) {
        hash = 31 * hash + ObjectHash(model.GetObjectElement(array, index));
    }
    return hash;
}

std::int32_t AnnotationRuntime::ObjectHash(const VmObjectRef object) {
    if (!object.IsValid()) return 0;
    if (instances_.Find(object) != nullptr) return HashCode(object).AsInt();
    if (interpreter_->Model().Kind(object) == VmObjectKind::string) {
        return JavaStringHash(interpreter_->Model().StringValue(object));
    }
    return interpreter_->Model().IdentityHashCode(object);
}

std::string AnnotationRuntime::ValueText(const VmValue& value,
                                         const std::string_view descriptor) {
    if (IsArrayDescriptor(descriptor)) {
        if (!value.ref.IsValid()) return "null";
        auto& model = interpreter_->Model();
        std::ostringstream text;
        text << '[';
        const auto length = model.ArrayLength(value.ref);
        const auto component = descriptor.substr(1);
        for (JniSize index = 0; index < length; ++index) {
            if (index != 0) text << ", ";
            if (component.size() == 1U &&
                std::string_view("ZBSCIJFD").find(component) !=
                    std::string_view::npos) {
                const auto bits = model.GetPrimitiveElement(value.ref, index);
                text << ValueText(
                    BitsToValue(component.front(),
                                static_cast<std::int64_t>(bits),
                                component.front() == 'D'
                                    ? std::bit_cast<double>(bits)
                                    : std::bit_cast<float>(
                                          static_cast<std::uint32_t>(bits))),
                    component);
            } else {
                text << ValueText(
                    VmValue::Ref(model.GetObjectElement(value.ref, index)),
                    component);
            }
        }
        text << ']';
        return text.str();
    }
    switch (ReturnShorty("()" + std::string(descriptor))) {
    case 'Z':
        return value.AsInt() != 0 ? "true" : "false";
    case 'B':
    case 'S':
    case 'C':
    case 'I':
        return std::to_string(value.AsInt());
    case 'J':
        return std::to_string(value.AsLong());
    case 'F':
        return std::to_string(value.AsFloat());
    case 'D':
        return std::to_string(value.AsDouble());
    default:
        if (!value.ref.IsValid()) return "null";
        if (instances_.Find(value.ref) != nullptr) {
            return interpreter_->StringUtf8(ToString(value.ref).ref);
        }
        if (interpreter_->Model().Kind(value.ref) == VmObjectKind::string) {
            return interpreter_->StringUtf8(value.ref);
        }
        if (interpreter_->Model().Kind(value.ref) == VmObjectKind::class_object) {
            return ClassNameCodec::ClassGetName(
                interpreter_->Linker()
                    .Class(interpreter_->Model().ClassOfClassObject(value.ref))
                    .descriptor);
        }
        return interpreter_->StringUtf8(interpreter_->NewStringUtf8(
            std::to_string(interpreter_->Model().IdentityHashCode(value.ref))));
    }
}

void AnnotationRuntime::TraceState(const InstanceState& state,
                                   const VmRootVisitor& visit) const {
    for (const auto& member : state.members) {
        if (member.materialized && member.value.kind == VmValue::Kind::ref &&
            member.value.ref.IsValid()) {
            visit(member.value.ref);
        }
    }
}

}  // namespace ogplay::runtime::dexvm
