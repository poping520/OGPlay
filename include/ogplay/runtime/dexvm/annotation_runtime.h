#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "ogplay/runtime/dexvm/class_linker.h"
#include "ogplay/runtime/dexvm/interpreter.h"
#include "ogplay/runtime/dexvm/owned_state_table.h"

namespace ogplay::runtime::dexvm {

// Per-VM restricted annotation instances. Generated implementation classes
// are created only after Link() from already loaded annotation interfaces;
// member dispatch uses the ordinary vtable/iftable, not name intercepts.
class AnnotationRuntime final {
public:
    explicit AnnotationRuntime(Interpreter& interpreter);

    [[nodiscard]] IntrinsicStateTableHooks Hooks();

    [[nodiscard]] VmObjectRef GetClassAnnotation(
        DexClassId java_class, VmObjectRef annotation_class, bool inherited);
    [[nodiscard]] bool IsClassAnnotationPresent(
        DexClassId java_class, VmObjectRef annotation_class);
    [[nodiscard]] VmObjectRef GetClassAnnotations(
        DexClassId java_class, bool inherited);

    [[nodiscard]] VmObjectRef GetFieldAnnotation(
        VmFieldId field, VmObjectRef annotation_class);
    [[nodiscard]] bool IsFieldAnnotationPresent(
        VmFieldId field, VmObjectRef annotation_class);
    [[nodiscard]] VmObjectRef GetFieldAnnotations(VmFieldId field);

    [[nodiscard]] VmObjectRef GetDefaultValue(VmMethodId method);

    [[nodiscard]] VmValue ReadMember(VmObjectRef instance, std::size_t index);
    [[nodiscard]] VmValue AnnotationType(VmObjectRef instance);
    [[nodiscard]] VmValue Equals(VmObjectRef instance, VmObjectRef other);
    [[nodiscard]] VmValue HashCode(VmObjectRef instance);
    [[nodiscard]] VmValue ToString(VmObjectRef instance);

private:
    struct MemberState final {
        std::string name;
        std::string descriptor;
        bool present{};
        LinkedAnnotationValue encoded;
        bool materialized{};
        VmValue value;
    };
    struct InstanceState final {
        DexClassId annotation_type;
        std::vector<MemberState> members;
    };
    struct ImplClassInfo final {
        DexClassId impl;
        std::vector<std::string> names;
        std::vector<std::string> descriptors;
    };

    [[nodiscard]] DexClassId RequireAnnotationClass(VmObjectRef class_object);
    [[nodiscard]] DexClassId EnsureImplClass(DexClassId annotation_type);
    [[nodiscard]] VmObjectRef Materialize(const LinkedAnnotation& annotation);
    [[nodiscard]] VmObjectRef EmptyAnnotationArray();
    [[nodiscard]] VmObjectRef MakeAnnotationArray(
        const std::vector<VmObjectRef>& values);
    [[nodiscard]] const LinkedAnnotation* FindDeclared(
        const std::vector<LinkedAnnotation>& annotations,
        std::string_view descriptor) const;
    [[nodiscard]] bool AnnotationTypeIsInherited(DexClassId annotation_type);
    [[nodiscard]] const LinkedAnnotationValue* FindNamed(
        const std::vector<std::pair<std::string, LinkedAnnotationValue>>&
            elements,
        std::string_view name) const;
    void BindMembers(InstanceState& state, DexClassId annotation_type,
                     const std::vector<std::pair<std::string, LinkedAnnotationValue>>&
                         elements);
    [[nodiscard]] VmValue MaterializeEncoded(
        const LinkedAnnotationValue& encoded, std::string_view expected);
    [[nodiscard]] VmValue MaterializeArray(
        const LinkedAnnotationValue& encoded, std::string_view component);
    [[nodiscard]] VmObjectRef CloneIfArray(VmObjectRef value);
    [[nodiscard]] VmValue MemberValue(InstanceState& state, std::size_t index);
    [[nodiscard]] bool ValuesEqual(const VmValue& left, const VmValue& right,
                                   std::string_view descriptor);
    [[nodiscard]] bool ArraysEqual(VmObjectRef left, VmObjectRef right,
                                   std::string_view descriptor);
    [[nodiscard]] std::int32_t ValueHash(const VmValue& value,
                                         std::string_view descriptor);
    [[nodiscard]] std::int32_t ArrayHash(VmObjectRef array,
                                         std::string_view descriptor);
    [[nodiscard]] std::int32_t ObjectHash(VmObjectRef object);
    [[nodiscard]] std::string ValueText(const VmValue& value,
                                        std::string_view descriptor);
    [[nodiscard]] InstanceState& RequireInstance(VmObjectRef instance);
    void TraceState(const InstanceState& state, const VmRootVisitor& visit) const;

    Interpreter* interpreter_;
    OwnedStateTable<InstanceState> instances_;
    std::unordered_map<std::uint32_t, ImplClassInfo> impl_classes_;
};

}  // namespace ogplay::runtime::dexvm
