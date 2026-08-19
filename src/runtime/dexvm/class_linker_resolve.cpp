// Constant-pool resolution (type/method/field indices), member lookup and
// assignability for the linked class table. Registration, layout and
// vtable construction live in class_linker.cpp.

#include "class_linker_internal.h"

namespace ogplay::runtime::dexvm {

DexClassId DexClassLinker::ResolveTypeIndex(const std::uint32_t type_index) {
    if (!impl_->image.has_value() ||
        type_index >= impl_->image->types.size()) {
        Fail(DexVmErrorReason::unresolved_reference,
             "type index is out of range");
    }
    auto& cached = impl_->type_cache[type_index];
    if (!cached.has_value()) {
        cached = ResolveDescriptor(
            impl_->image->types[type_index].descriptor);
    }
    return *cached;
}

ResolvedMethodRef DexClassLinker::ResolveMethodIndex(
    const std::uint32_t method_index, const bool direct_or_static) {
    if (!impl_->image.has_value() ||
        method_index >= impl_->image->methods.size()) {
        Fail(DexVmErrorReason::unresolved_reference,
             "method index is out of range");
    }
    auto& cached = impl_->method_cache[method_index];
    if (cached.has_value()) return *cached;

    const auto& image = *impl_->image;
    const auto& entry = image.methods[method_index];
    const auto owner_descriptor =
        image.types[entry.class_type_index].descriptor;
    const auto owner = ResolveDescriptor(owner_descriptor);
    const auto name = Ascii(image.strings[entry.name_string_index]);
    const auto& prototype = image.prototypes[entry.prototype_index];
    std::string descriptor = "(";
    for (const auto parameter : prototype.parameter_type_indices) {
        descriptor += image.types[parameter].descriptor;
    }
    descriptor += ")";
    descriptor += image.types[prototype.return_type_index].descriptor;

    std::optional<VmMethodId> resolved;
    if (direct_or_static) {
        resolved = FindDirectMethod(owner, name, descriptor);
    }
    if (!resolved.has_value()) {
        // Virtual resolution walks the vtable lookup of the named class.
        if (const auto index = FindVtableIndex(owner, name, descriptor);
            index.has_value()) {
            resolved = impl_->ClassAt(owner).vtable[*index];
        }
    }
    if (!resolved.has_value()) {
        resolved = FindDirectMethod(owner, name, descriptor);
    }
    if (!resolved.has_value() && impl_->ClassAt(owner).is_interface) {
        // Interface methods dispatch by name+descriptor on the receiver at
        // runtime (02 §7); synthesize the abstract declaration so the
        // constant pool resolves even when the interface (typically an
        // intrinsic one) declares no method table.
        LinkedMethod synthetic;
        synthetic.owner = owner;
        synthetic.name = name;
        synthetic.descriptor = descriptor;
        synthetic.kind = MethodKind::abstract;
        const auto parts = SplitDescriptor(descriptor);
        synthetic.return_shorty = ShortyOf(parts.return_type);
        synthetic.ins_words = ArgumentWords(parts, false);
        resolved = impl_->AddMethod(std::move(synthetic));
    }
    if (!resolved.has_value() && GapSurveyEnabled() &&
        IsPlatformDescriptor(owner_descriptor)) {
        // invoke-direct covers constructors too: <init> is never static,
        // so its synthesized stub must count the receiver word.
        resolved = SynthesizeSurveyMethod(
            owner, name, descriptor, direct_or_static && name != "<init>");
    }
    if (!resolved.has_value()) {
        Fail(DexVmErrorReason::unresolved_reference,
             "method cannot be resolved: " + owner_descriptor + "->" + name +
                 descriptor);
    }
    cached = ResolvedMethodRef{*resolved, owner};
    return *cached;
}

ResolvedFieldRef DexClassLinker::ResolveFieldIndex(
    const std::uint32_t field_index, const bool is_static) {
    if (!impl_->image.has_value() ||
        field_index >= impl_->image->fields.size()) {
        Fail(DexVmErrorReason::unresolved_reference,
             "field index is out of range");
    }
    auto& cached = impl_->field_cache[field_index];
    if (cached.has_value()) {
        const auto& field = impl_->FieldAt(cached->field);
        if (field.is_static != is_static) {
            Fail(DexVmErrorReason::unresolved_reference,
                 "field static kind mismatch: " + field.name);
        }
        return *cached;
    }
    const auto& image = *impl_->image;
    const auto& entry = image.fields[field_index];
    const auto owner_descriptor =
        image.types[entry.class_type_index].descriptor;
    const auto owner = ResolveDescriptor(owner_descriptor);
    const auto name = Ascii(image.strings[entry.name_string_index]);
    const auto descriptor = image.types[entry.type_index].descriptor;
    const auto resolved = FindFieldRecursive(owner, name, descriptor);
    if (!resolved.has_value()) {
        Fail(DexVmErrorReason::unresolved_reference,
             "field cannot be resolved: " + owner_descriptor + "->" + name +
                 ":" + descriptor);
    }
    const auto& field = impl_->FieldAt(*resolved);
    if (field.is_static != is_static) {
        Fail(DexVmErrorReason::unresolved_reference,
             "field static kind mismatch: " + owner_descriptor + "->" + name);
    }
    cached = ResolvedFieldRef{*resolved, owner};
    return *cached;
}

std::optional<std::uint16_t> DexClassLinker::FindVtableIndex(
    const DexClassId owner, const std::string& name,
    const std::string& descriptor) const {
    const auto& extra = impl_->ExtrasAt(owner);
    const auto found = extra.virtual_lookup.find(MemberKey(name, descriptor));
    if (found == extra.virtual_lookup.end()) return std::nullopt;
    return found->second;
}

std::optional<VmMethodId> DexClassLinker::FindDirectMethod(
    const DexClassId owner, const std::string& name,
    const std::string& descriptor) const {
    auto current = std::optional<DexClassId>(owner);
    while (current.has_value()) {
        const auto& extra = impl_->ExtrasAt(*current);
        const auto found =
            extra.direct_lookup.find(MemberKey(name, descriptor));
        if (found != extra.direct_lookup.end()) return found->second;
        current = impl_->ClassAt(*current).super;
    }
    return std::nullopt;
}

std::optional<VmFieldId> DexClassLinker::FindFieldRecursive(
    const DexClassId owner, const std::string& name,
    const std::string& descriptor) const {
    auto current = std::optional<DexClassId>(owner);
    while (current.has_value()) {
        const auto& extra = impl_->ExtrasAt(*current);
        const auto found =
            extra.field_lookup.find(MemberKey(name, descriptor));
        if (found != extra.field_lookup.end()) return found->second;
        // Interface constants participate in resolution.
        for (const auto interface_id : impl_->ClassAt(*current).interfaces) {
            const auto& interface_extra = impl_->ExtrasAt(interface_id);
            const auto interface_found =
                interface_extra.field_lookup.find(
                    MemberKey(name, descriptor));
            if (interface_found != interface_extra.field_lookup.end()) {
                return interface_found->second;
            }
        }
        current = impl_->ClassAt(*current).super;
    }
    return std::nullopt;
}

bool DexClassLinker::IsAssignable(const DexClassId target,
                                  const DexClassId source) {
    if (target == source) return true;
    const auto& target_class = impl_->ClassAt(target);
    const auto& source_class = impl_->ClassAt(source);
    if (source_class.is_array) {
        // JLS 10.7 / AOSP TypeCheck: every array type is a subtype of
        // Object, Cloneable, and Serializable. Object.clone() uses
        // `this instanceof Cloneable`, so this must match the Java check.
        if (target_class.descriptor == "Ljava/lang/Object;" ||
            target_class.descriptor == "Ljava/lang/Cloneable;" ||
            target_class.descriptor == "Ljava/io/Serializable;") {
            return true;
        }
        if (!target_class.is_array) return false;
        const auto& source_element = source_class.array_element_descriptor;
        const auto& target_element = target_class.array_element_descriptor;
        if (source_element == target_element) return true;
        if (IsRefDescriptor(source_element) &&
            IsRefDescriptor(target_element)) {
            return IsAssignable(ResolveDescriptor(target_element),
                                ResolveDescriptor(source_element));
        }
        return false;
    }
    auto current = std::optional<DexClassId>(source);
    while (current.has_value()) {
        if (*current == target) return true;
        const auto& linked = impl_->ClassAt(*current);
        for (const auto interface_id : linked.interfaces) {
            if (interface_id == target ||
                IsAssignable(target, interface_id)) {
                return true;
            }
        }
        current = linked.super;
    }
    return false;
}

}  // namespace ogplay::runtime::dexvm
