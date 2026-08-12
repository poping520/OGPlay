#include "class_linker_internal.h"

namespace ogplay::runtime::dexvm {

[[noreturn]] void Fail(const DexVmErrorReason reason, std::string message) {
    throw DexVmError(reason, std::move(message));
}

[[nodiscard]] std::string Ascii(const loader::DexString& value) {
    std::string result;
    result.reserve(value.value.size());
    for (const auto unit : value.value) {
        if (unit > 0x7f) {
            // Member and type names in the supported titles are ASCII;
            // anything else is preserved via UTF-16 escape formatting.
            result += "\\u";
            constexpr char digits[] = "0123456789abcdef";
            result += digits[(unit >> 12) & 0xf];
            result += digits[(unit >> 8) & 0xf];
            result += digits[(unit >> 4) & 0xf];
            result += digits[unit & 0xf];
            continue;
        }
        result.push_back(static_cast<char>(unit));
    }
    return result;
}

[[nodiscard]] bool IsPlatformDescriptor(const std::string_view descriptor) {
    return descriptor.starts_with("Landroid/") ||
           descriptor.starts_with("Ljava/") ||
           descriptor.starts_with("Ljavax/") ||
           descriptor.starts_with("Ldalvik/") ||
           descriptor.starts_with("Lorg/apache/http/");
}

[[nodiscard]] bool IsWideDescriptor(const std::string_view descriptor) {
    return descriptor == "J" || descriptor == "D";
}

[[nodiscard]] bool IsRefDescriptor(const std::string_view descriptor) {
    return descriptor.starts_with("L") || descriptor.starts_with("[");
}

[[nodiscard]] char ShortyOf(const std::string_view descriptor) {
    if (IsRefDescriptor(descriptor)) return 'L';
    return descriptor.empty() ? 'V' : descriptor.front();
}

[[nodiscard]] DescriptorParts SplitDescriptor(const std::string& descriptor) {
    DescriptorParts parts;
    if (descriptor.empty() || descriptor.front() != '(') {
        Fail(DexVmErrorReason::invalid_member,
             "method descriptor is malformed: " + descriptor);
    }
    std::size_t index = 1;
    while (index < descriptor.size() && descriptor[index] != ')') {
        const std::size_t start = index;
        while (index < descriptor.size() && descriptor[index] == '[') ++index;
        if (index >= descriptor.size()) break;
        if (descriptor[index] == 'L') {
            const auto end = descriptor.find(';', index);
            if (end == std::string::npos) {
                Fail(DexVmErrorReason::invalid_member,
                     "method descriptor reference is unterminated: " +
                         descriptor);
            }
            index = end + 1;
        } else {
            ++index;
        }
        parts.parameters.push_back(descriptor.substr(start, index - start));
    }
    if (index >= descriptor.size() || descriptor[index] != ')') {
        Fail(DexVmErrorReason::invalid_member,
             "method descriptor has no return type: " + descriptor);
    }
    parts.return_type = descriptor.substr(index + 1);
    return parts;
}

[[nodiscard]] std::uint16_t ArgumentWords(const DescriptorParts& parts,
                                          const bool is_static) {
    std::uint32_t words = is_static ? 0U : 1U;
    for (const auto& parameter : parts.parameters) {
        words += IsWideDescriptor(parameter) ? 2U : 1U;
    }
    if (words > 0xffffU) {
        Fail(DexVmErrorReason::invalid_member,
             "method argument words overflow");
    }
    return static_cast<std::uint16_t>(words);
}

[[nodiscard]] std::string MemberKey(const std::string& name,
                                    const std::string& descriptor) {
    return name + ":" + descriptor;
}

DexClassLinker::DexClassLinker() : impl_(std::make_unique<Impl>()) {}
DexClassLinker::~DexClassLinker() = default;

void DexClassLinker::RegisterIntrinsics(
    const std::span<const IntrinsicClassDecl> catalog) {
    if (impl_->link_complete) {
        Fail(DexVmErrorReason::internal_invariant,
             "intrinsics registered after linking");
    }
    struct Pending final {
        const IntrinsicClassDecl* declaration;
        DexClassId id;
    };
    std::vector<Pending> pending;
    for (const auto& declaration : catalog) {
        LinkedClass linked;
        linked.descriptor = declaration.descriptor;
        linked.is_intrinsic = true;
        linked.is_interface = declaration.is_interface;
        linked.access_flags = declaration.is_interface ? kAccInterface : 0U;
        const auto id = impl_->AddClass(std::move(linked));
        pending.push_back({&declaration, id});
    }
    for (const auto& [declaration, id] : pending) {
        auto& linked = impl_->ClassAt(id);
        auto& extra = impl_->ExtrasAt(id);
        if (declaration->superclass.has_value()) {
            const auto super = FindClass(*declaration->superclass);
            if (!super.has_value()) {
                Fail(DexVmErrorReason::unknown_class,
                     "intrinsic superclass is not registered: " +
                         *declaration->superclass);
            }
            linked.super = *super;
        }
        for (const auto& interface_name : declaration->interfaces) {
            const auto interface_id = FindClass(interface_name);
            if (!interface_id.has_value()) {
                Fail(DexVmErrorReason::unknown_class,
                     "intrinsic interface is not registered: " +
                         interface_name);
            }
            linked.interfaces.push_back(*interface_id);
        }
        for (const auto& method : declaration->methods) {
            LinkedMethod linked_method;
            linked_method.owner = id;
            linked_method.name = method.name;
            linked_method.descriptor = method.descriptor;
            linked_method.is_static = method.is_static;
            linked_method.kind = MethodKind::intrinsic;
            linked_method.overridable = method.overridable;
            linked_method.intrinsic_handler = method.handler;
            const auto parts = SplitDescriptor(method.descriptor);
            linked_method.return_shorty = ShortyOf(parts.return_type);
            linked_method.ins_words = ArgumentWords(parts, method.is_static);
            const bool is_direct = method.is_static ||
                                   method.name == "<init>" ||
                                   method.name == "<clinit>";
            // vtable_index -2 marks "virtual, pending vtable placement".
            linked_method.vtable_index = is_direct ? -1 : -2;
            const auto method_id = impl_->AddMethod(std::move(linked_method));
            if (is_direct) {
                extra.direct_lookup.emplace(
                    MemberKey(method.name, method.descriptor), method_id);
            }
        }
        for (const auto& declared_field : declaration->fields) {
            LinkedField field;
            field.owner = id;
            field.name = declared_field.name;
            field.descriptor = declared_field.descriptor;
            field.is_static = declared_field.is_static;
            field.access_flags = declared_field.is_static
                                     ? (kAccStatic |
                                        (declared_field.has_constant
                                             ? kAccFinal
                                             : 0U))
                                     : 0U;
            field.is_wide = IsWideDescriptor(declared_field.descriptor);
            field.is_ref = IsRefDescriptor(declared_field.descriptor);
            const auto field_id = impl_->AddField(std::move(field));
            (declared_field.is_static ? linked.own_static_fields
                                      : linked.own_instance_fields)
                .push_back(field_id);
            if (declared_field.has_constant) {
                linked.intrinsic_constants.push_back(declared_field);
            }
        }
        linked.intrinsic_clinit_handler = declaration->clinit_handler;
    }
}

void DexClassLinker::RegisterDex(std::vector<std::uint8_t> dex_bytes) {
    if (impl_->link_complete) {
        Fail(DexVmErrorReason::internal_invariant,
             "dex registered after linking");
    }
    if (impl_->image.has_value()) {
        Fail(DexVmErrorReason::invalid_image,
             "only a single application dex is supported");
    }
    impl_->dex_bytes = std::move(dex_bytes);
    impl_->image = loader::ParseDex(impl_->dex_bytes);
    impl_->class_data =
        loader::ReadDexClassData(impl_->dex_bytes, *impl_->image);
    const auto& image = *impl_->image;

    impl_->type_cache.assign(image.types.size(), std::nullopt);
    impl_->method_cache.assign(image.methods.size(), std::nullopt);
    impl_->field_cache.assign(image.fields.size(), std::nullopt);

    for (std::uint32_t class_index = 0; class_index < image.classes.size();
         ++class_index) {
        const auto& definition = image.classes[class_index];
        const auto descriptor =
            image.types[definition.class_type_index].descriptor;
        if (IsPlatformDescriptor(descriptor)) {
            // Bundled support-library copies of platform classes are never
            // interpreted (03 §1); the intrinsic catalog wins.
            continue;
        }
        LinkedClass linked;
        linked.descriptor = descriptor;
        linked.access_flags = definition.access_flags;
        linked.is_interface = (definition.access_flags & kAccInterface) != 0;
        linked.dex_class_def_index = class_index;
        const auto id = impl_->AddClass(std::move(linked));

        const auto& data = impl_->class_data[class_index];
        auto& stored = impl_->ClassAt(id);
        auto& extra = impl_->ExtrasAt(id);
        for (const auto* field_list :
             {&data.static_fields, &data.instance_fields}) {
            const bool is_static = field_list == &data.static_fields;
            for (const auto& encoded : *field_list) {
                const auto& field_id_entry = image.fields[encoded.field_index];
                LinkedField field;
                field.owner = id;
                field.name =
                    Ascii(image.strings[field_id_entry.name_string_index]);
                field.descriptor =
                    image.types[field_id_entry.type_index].descriptor;
                field.access_flags = encoded.access_flags;
                field.is_static = is_static;
                field.is_wide = IsWideDescriptor(field.descriptor);
                field.is_ref = IsRefDescriptor(field.descriptor);
                const auto field_id = impl_->AddField(std::move(field));
                (is_static ? stored.own_static_fields
                           : stored.own_instance_fields)
                    .push_back(field_id);
            }
        }
        for (const auto* method_list :
             {&data.direct_methods, &data.virtual_methods}) {
            const bool direct = method_list == &data.direct_methods;
            for (const auto& encoded : *method_list) {
                const auto& method_id_entry =
                    image.methods[encoded.method_index];
                LinkedMethod method;
                method.owner = id;
                method.name =
                    Ascii(image.strings[method_id_entry.name_string_index]);
                const auto& prototype =
                    image.prototypes[method_id_entry.prototype_index];
                std::string method_descriptor = "(";
                for (const auto parameter : prototype.parameter_type_indices) {
                    method_descriptor += image.types[parameter].descriptor;
                }
                method_descriptor += ")";
                method_descriptor +=
                    image.types[prototype.return_type_index].descriptor;
                method.descriptor = std::move(method_descriptor);
                method.access_flags = encoded.access_flags;
                method.is_static = (encoded.access_flags & kAccStatic) != 0;
                if ((encoded.access_flags & kAccNative) != 0) {
                    method.kind = MethodKind::native;
                } else if ((encoded.access_flags & kAccAbstract) != 0) {
                    method.kind = MethodKind::abstract;
                } else {
                    method.kind = MethodKind::interpreted;
                    if (!encoded.code.has_value()) {
                        Fail(DexVmErrorReason::invalid_member,
                             "concrete method has no code: " + method.name);
                    }
                    method.code = loader::ReadDexMethodCode(
                        impl_->dex_bytes, image, *encoded.code);
                }
                const auto parts = SplitDescriptor(method.descriptor);
                method.return_shorty = ShortyOf(parts.return_type);
                method.ins_words = ArgumentWords(parts, method.is_static);
                if (method.code.has_value() &&
                    method.code->info.incoming_words != method.ins_words) {
                    Fail(DexVmErrorReason::invalid_code,
                         "method ins does not match descriptor: " +
                             stored.descriptor + "." + method.name);
                }
                method.vtable_index = direct ? -1 : -2;
                const auto vm_method_id = impl_->AddMethod(std::move(method));
                if (direct) {
                    const auto& added = impl_->MethodAt(vm_method_id);
                    extra.direct_lookup.emplace(
                        MemberKey(added.name, added.descriptor), vm_method_id);
                    if (added.name == "<clinit>") {
                        stored.clinit = vm_method_id;
                    }
                }
            }
        }
    }
}

void DexClassLinker::Link() {
    if (impl_->link_complete) return;
    if (!FindClass("Ljava/lang/Object;").has_value()) {
        Fail(DexVmErrorReason::unknown_class,
             "core intrinsic catalog is not registered");
    }
    // Resolve dex-declared hierarchy names to ids. Missing classes are
    // collected across the whole dex before failing so a single run reports
    // the complete linkage gap instead of only the first hit.
    if (impl_->image.has_value()) {
        const auto& image = *impl_->image;
        // missing descriptor -> first requiring class (deterministic order).
        std::map<std::string, std::string> missing;
        for (auto& linked : impl_->classes) {
            if (linked.is_intrinsic ||
                !linked.dex_class_def_index.has_value()) {
                continue;
            }
            const auto& definition =
                image.classes[*linked.dex_class_def_index];
            if (definition.superclass_type_index.has_value()) {
                const auto name =
                    image.types[*definition.superclass_type_index].descriptor;
                const auto super = FindClass(name);
                if (!super.has_value()) {
                    missing.emplace(name, linked.descriptor);
                } else {
                    linked.super = *super;
                }
            } else if (linked.descriptor != "Ljava/lang/Object;") {
                Fail(DexVmErrorReason::invalid_hierarchy,
                     "class without superclass: " + linked.descriptor);
            }
            for (const auto interface_index :
                 definition.interface_type_indices) {
                const auto name = image.types[interface_index].descriptor;
                const auto interface_id = FindClass(name);
                if (!interface_id.has_value()) {
                    missing.emplace(name, linked.descriptor);
                    continue;
                }
                linked.interfaces.push_back(*interface_id);
            }
        }
        if (!missing.empty()) {
            std::string message =
                "hierarchy classes are not available (" +
                std::to_string(missing.size()) + "):";
            for (const auto& [name, required_by] : missing) {
                message += "\n  " + name + " (required by " + required_by +
                           ")";
            }
            Fail(DexVmErrorReason::unknown_class, std::move(message));
        }
    }
    std::set<std::uint32_t> visiting;
    for (auto& linked : impl_->classes) {
        impl_->LinkClass(linked.id, visiting);
    }
    impl_->link_complete = true;
}

bool DexClassLinker::IsLinked() const noexcept {
    return impl_->link_complete;
}

std::optional<DexClassId> DexClassLinker::FindClass(
    const std::string_view descriptor) const {
    const auto found = impl_->class_by_descriptor.find(std::string(descriptor));
    if (found == impl_->class_by_descriptor.end()) return std::nullopt;
    return impl_->classes[found->second].id;
}

DexClassId DexClassLinker::ResolveDescriptor(
    const std::string_view descriptor) {
    if (const auto existing = FindClass(descriptor); existing.has_value()) {
        return *existing;
    }
    if (descriptor.starts_with("[")) {
        // Synthesize the array class; element ref classes must resolve.
        const auto element = descriptor.substr(1);
        if (IsRefDescriptor(element)) {
            (void)ResolveDescriptor(element);
        } else if (element.size() != 1 ||
                   std::string_view("ZBSCIJFD").find(element) ==
                       std::string_view::npos) {
            Fail(DexVmErrorReason::unknown_class,
                 "array element descriptor is invalid: " +
                     std::string(descriptor));
        }
        LinkedClass linked;
        linked.descriptor = std::string(descriptor);
        linked.is_array = true;
        linked.array_element_descriptor = std::string(element);
        linked.super = FindClass("Ljava/lang/Object;");
        const auto id = impl_->AddClass(std::move(linked));
        if (impl_->link_complete) {
            auto& stored = impl_->ClassAt(id);
            auto& object_class = impl_->ClassAt(*stored.super);
            stored.vtable = object_class.vtable;
            impl_->ExtrasAt(id).virtual_lookup =
                impl_->ExtrasAt(object_class.id).virtual_lookup;
            impl_->ExtrasAt(id).linked = true;
        }
        return id;
    }
    if (impl_->gap_survey && IsPlatformDescriptor(descriptor)) {
        // Survey mode: stand the platform class up as a neutral intrinsic so
        // the run continues and the gap is reported instead of aborting.
        LinkedClass linked;
        linked.descriptor = std::string(descriptor);
        linked.is_intrinsic = true;
        linked.super = FindClass("Ljava/lang/Object;");
        const auto id = impl_->AddClass(std::move(linked));
        if (impl_->link_complete && impl_->ClassAt(id).super.has_value()) {
            auto& stored = impl_->ClassAt(id);
            const auto object_id = *stored.super;
            stored.vtable = impl_->ClassAt(object_id).vtable;
            impl_->ExtrasAt(id).virtual_lookup =
                impl_->ExtrasAt(object_id).virtual_lookup;
            impl_->ExtrasAt(id).linked = true;
        }
        RecordGapSurveyHit(std::string(descriptor), {});
        return id;
    }
    Fail(DexVmErrorReason::unknown_class,
         "class is not available: " + std::string(descriptor));
}

void DexClassLinker::EnableGapSurvey() { impl_->gap_survey = true; }

bool DexClassLinker::GapSurveyEnabled() const noexcept {
    return impl_->gap_survey;
}

void DexClassLinker::RecordGapSurveyHit(const std::string& owner_descriptor,
                                        const std::string& member) {
    const auto key = member.empty() ? owner_descriptor
                                    : owner_descriptor + "->" + member;
    ++impl_->survey_hits[key];
}

VmMethodId DexClassLinker::SynthesizeSurveyMethod(
    const DexClassId owner, const std::string& name,
    const std::string& descriptor, const bool is_static) {
    if (!impl_->gap_survey) {
        Fail(DexVmErrorReason::internal_invariant,
             "survey stubs require survey mode");
    }
    LinkedMethod method;
    method.owner = owner;
    method.name = name;
    method.descriptor = descriptor;
    method.is_static = is_static;
    method.kind = MethodKind::intrinsic;
    // Deliberately unregistered: the interpreter answers neutrally and
    // records the hit, so the stub can never be mistaken for an
    // implementation.
    method.intrinsic_handler = "survey.unimplemented";
    const auto parts = SplitDescriptor(descriptor);
    method.return_shorty = ShortyOf(parts.return_type);
    method.ins_words = ArgumentWords(parts, is_static);
    const bool is_direct =
        is_static || name == "<init>" || name == "<clinit>";
    auto& linked = impl_->ClassAt(owner);
    auto& extra = impl_->ExtrasAt(owner);
    const auto slot = static_cast<std::uint16_t>(linked.vtable.size());
    method.vtable_index = is_direct ? -1 : static_cast<std::int32_t>(slot);
    const auto id = impl_->AddMethod(std::move(method));
    if (is_direct) {
        extra.direct_lookup.insert_or_assign(MemberKey(name, descriptor), id);
    } else {
        extra.virtual_lookup.insert_or_assign(MemberKey(name, descriptor),
                                             slot);
        linked.vtable.push_back(id);
    }
    return id;
}

std::vector<GapSurveyHit> DexClassLinker::GapSurveyHits() const {
    std::vector<GapSurveyHit> hits;
    hits.reserve(impl_->survey_hits.size());
    for (const auto& [key, count] : impl_->survey_hits) {
        const auto separator = key.find("->");
        GapSurveyHit hit;
        hit.owner_descriptor = key.substr(0, separator);
        if (separator != std::string::npos) {
            hit.member = key.substr(separator + 2);
        }
        hit.hits = count;
        hits.push_back(std::move(hit));
    }
    return hits;
}

const LinkedClass& DexClassLinker::Class(const DexClassId id) const {
    return impl_->ClassAt(id);
}
LinkedClass& DexClassLinker::MutableClass(const DexClassId id) {
    return impl_->ClassAt(id);
}
const LinkedMethod& DexClassLinker::Method(const VmMethodId id) const {
    return impl_->MethodAt(id);
}
LinkedMethod& DexClassLinker::MutableMethod(const VmMethodId id) {
    return impl_->MethodAt(id);
}
const LinkedField& DexClassLinker::Field(const VmFieldId id) const {
    return impl_->FieldAt(id);
}

const loader::DexImage& DexClassLinker::Image() const {
    if (!impl_->image.has_value()) {
        Fail(DexVmErrorReason::invalid_image, "no dex image is registered");
    }
    return *impl_->image;
}

std::span<const std::uint8_t> DexClassLinker::DexBytes() const {
    return impl_->dex_bytes;
}

std::vector<loader::DexEncodedValue> DexClassLinker::StaticValues(
    const LinkedClass& linked) const {
    if (!linked.dex_class_def_index.has_value() ||
        !impl_->image.has_value()) {
        return {};
    }
    const auto& definition =
        impl_->image->classes[*linked.dex_class_def_index];
    return loader::ReadDexStaticValues(impl_->dex_bytes, *impl_->image,
                                       definition.static_values_offset);
}

std::size_t DexClassLinker::ClassCount() const noexcept {
    return impl_->classes.size();
}

std::vector<VmMethodId> DexClassLinker::MethodsOf(
    const DexClassId owner) const {
    std::vector<VmMethodId> result;
    for (const auto& method : impl_->methods) {
        if (method.owner == owner) result.push_back(method.id);
    }
    return result;
}

std::vector<DexClassId> DexClassLinker::AllClasses() const {
    std::vector<DexClassId> result;
    result.reserve(impl_->classes.size());
    for (const auto& linked : impl_->classes) result.push_back(linked.id);
    return result;
}
}  // namespace ogplay::runtime::dexvm
