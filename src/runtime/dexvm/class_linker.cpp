#include "ogplay/runtime/dexvm/class_linker.h"

#include <algorithm>
#include <set>
#include <unordered_map>
#include <utility>

#include "ogplay/runtime/dexvm/generated/opcode_table.h"

namespace ogplay::runtime::dexvm {
namespace {

namespace gen = ogplay::runtime::dexvm::generated;

constexpr std::uint32_t kAccStatic = 0x0008;
constexpr std::uint32_t kAccFinal = 0x0010;
constexpr std::uint32_t kAccInterface = 0x0200;
constexpr std::uint32_t kAccNative = 0x0100;
constexpr std::uint32_t kAccAbstract = 0x0400;

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

struct DescriptorParts final {
    std::vector<std::string> parameters;
    std::string return_type;
};

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

}  // namespace

class DexClassLinker::Impl final {
public:
    struct ClassExtras final {
        std::unordered_map<std::string, std::uint16_t> virtual_lookup;
        std::unordered_map<std::string, VmMethodId> direct_lookup;
        std::unordered_map<std::string, VmFieldId> field_lookup;
        bool linked{};
    };

    std::vector<LinkedClass> classes;
    std::vector<ClassExtras> extras;
    std::vector<LinkedMethod> methods;
    std::vector<LinkedField> fields;
    std::unordered_map<std::string, std::uint32_t> class_by_descriptor;

    std::vector<std::uint8_t> dex_bytes;
    std::optional<loader::DexImage> image;
    std::vector<loader::DexClassData> class_data;

    std::vector<std::optional<DexClassId>> type_cache;
    std::vector<std::optional<ResolvedMethodRef>> method_cache;
    std::vector<std::optional<ResolvedFieldRef>> field_cache;

    bool link_complete{};

    [[nodiscard]] DexClassId AddClass(LinkedClass linked) {
        const auto id = DexClassId(
            static_cast<std::uint32_t>(classes.size() + 1));
        linked.id = id;
        if (!class_by_descriptor
                 .emplace(linked.descriptor,
                          static_cast<std::uint32_t>(classes.size()))
                 .second) {
            Fail(DexVmErrorReason::duplicate_class,
                 "class is declared twice: " + linked.descriptor);
        }
        classes.push_back(std::move(linked));
        extras.emplace_back();
        return id;
    }

    [[nodiscard]] VmMethodId AddMethod(LinkedMethod method) {
        const auto id = VmMethodId(
            static_cast<std::uint32_t>(methods.size() + 1));
        method.id = id;
        methods.push_back(std::move(method));
        return id;
    }

    [[nodiscard]] VmFieldId AddField(LinkedField field) {
        const auto id = VmFieldId(
            static_cast<std::uint32_t>(fields.size() + 1));
        field.id = id;
        fields.push_back(std::move(field));
        return id;
    }

    [[nodiscard]] LinkedClass& ClassAt(const DexClassId id) {
        if (!id.IsValid() || id.Value() > classes.size()) {
            Fail(DexVmErrorReason::unknown_class, "class id is invalid");
        }
        return classes[id.Value() - 1];
    }
    [[nodiscard]] ClassExtras& ExtrasAt(const DexClassId id) {
        return extras[id.Value() - 1];
    }
    [[nodiscard]] LinkedMethod& MethodAt(const VmMethodId id) {
        if (!id.IsValid() || id.Value() > methods.size()) {
            Fail(DexVmErrorReason::invalid_member, "method id is invalid");
        }
        return methods[id.Value() - 1];
    }
    [[nodiscard]] LinkedField& FieldAt(const VmFieldId id) {
        if (!id.IsValid() || id.Value() > fields.size()) {
            Fail(DexVmErrorReason::invalid_member, "field id is invalid");
        }
        return fields[id.Value() - 1];
    }

    void LinkClass(const DexClassId id, std::set<std::uint32_t>& visiting) {
        auto& linked = ClassAt(id);
        auto& extra = ExtrasAt(id);
        if (extra.linked) return;
        if (!visiting.insert(id.Value()).second) {
            Fail(DexVmErrorReason::invalid_hierarchy,
                 "class hierarchy contains a cycle at " + linked.descriptor);
        }

        std::uint16_t slot_cursor = 0;
        if (linked.super.has_value()) {
            LinkClass(*linked.super, visiting);
            const auto& super = ClassAt(*linked.super);
            const auto& super_extra = ExtrasAt(*linked.super);
            if ((super.access_flags & kAccFinal) != 0) {
                Fail(DexVmErrorReason::invalid_hierarchy,
                     "final class is extended: " + super.descriptor);
            }
            if (super.is_interface) {
                Fail(DexVmErrorReason::invalid_hierarchy,
                     "interface used as superclass of " + linked.descriptor);
            }
            slot_cursor = super.instance_slots;
            linked.vtable = super.vtable;
            extra.virtual_lookup = super_extra.virtual_lookup;
            // Inherited interfaces flatten into this class's iftable.
            std::vector<DexClassId> merged = super.interfaces;
            for (const auto interface_id : linked.interfaces) {
                if (std::find(merged.begin(), merged.end(), interface_id) ==
                    merged.end()) {
                    merged.push_back(interface_id);
                }
            }
            linked.interfaces = std::move(merged);
        }
        for (const auto interface_id : linked.interfaces) {
            LinkClass(interface_id, visiting);
            if (!ClassAt(interface_id).is_interface) {
                Fail(DexVmErrorReason::invalid_hierarchy,
                     "non-interface implemented by " + linked.descriptor);
            }
        }

        // Instance field layout: append after super, declaration order,
        // wide fields aligned to slot pairs (AOSP Class.cpp layout intent;
        // exact offsets are ours, they never leak to guest memory).
        for (const auto field_id : linked.own_instance_fields) {
            auto& field = FieldAt(field_id);
            if (field.is_wide && (slot_cursor % 2) != 0) {
                ++slot_cursor;
            }
            field.slot = slot_cursor;
            slot_cursor = static_cast<std::uint16_t>(
                slot_cursor + (field.is_wide ? 2 : 1));
            extra.field_lookup.emplace(
                MemberKey(field.name, field.descriptor), field_id);
        }
        linked.instance_slots = slot_cursor;

        // Static storage layout.
        std::uint16_t static_cursor = 0;
        for (const auto field_id : linked.own_static_fields) {
            auto& field = FieldAt(field_id);
            if (field.is_wide && (static_cursor % 2) != 0) ++static_cursor;
            field.slot = static_cursor;
            static_cursor = static_cast<std::uint16_t>(
                static_cursor + (field.is_wide ? 2 : 1));
            extra.field_lookup.emplace(
                MemberKey(field.name, field.descriptor), field_id);
        }
        linked.static_storage.assign(static_cursor, 0U);

        // vtable: override by name+descriptor or append.
        for (auto& method_id : OwnVirtualMethods(linked)) {
            auto& method = MethodAt(method_id);
            const auto key = MemberKey(method.name, method.descriptor);
            const auto existing = extra.virtual_lookup.find(key);
            if (existing != extra.virtual_lookup.end()) {
                const auto index = existing->second;
                const auto& overridden = MethodAt(linked.vtable[index]);
                if (overridden.kind == MethodKind::intrinsic &&
                    !overridden.overridable && !linked.is_intrinsic) {
                    Fail(DexVmErrorReason::invalid_override,
                         "intrinsic method is not overridable: " +
                             ClassAt(overridden.owner).descriptor + "." +
                             overridden.name);
                }
                if ((overridden.access_flags & kAccFinal) != 0) {
                    Fail(DexVmErrorReason::invalid_override,
                         "final method is overridden: " + overridden.name);
                }
                method.vtable_index = static_cast<std::int32_t>(index);
                linked.vtable[index] = method_id;
            } else {
                method.vtable_index =
                    static_cast<std::int32_t>(linked.vtable.size());
                extra.virtual_lookup.emplace(
                    key, static_cast<std::uint16_t>(linked.vtable.size()));
                linked.vtable.push_back(method_id);
            }
        }
        extra.linked = true;
        visiting.erase(id.Value());
    }

    [[nodiscard]] std::vector<VmMethodId> OwnVirtualMethods(
        const LinkedClass& linked) {
        std::vector<VmMethodId> result;
        for (const auto& method : methods) {
            if (method.owner == linked.id && method.vtable_index == -2) {
                result.push_back(method.id);
            }
        }
        return result;
    }
};

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
    // Resolve dex-declared hierarchy names to ids.
    if (impl_->image.has_value()) {
        const auto& image = *impl_->image;
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
                    Fail(DexVmErrorReason::unknown_class,
                         "superclass is not available: " + name +
                             " (required by " + linked.descriptor + ")");
                }
                linked.super = *super;
            } else if (linked.descriptor != "Ljava/lang/Object;") {
                Fail(DexVmErrorReason::invalid_hierarchy,
                     "class without superclass: " + linked.descriptor);
            }
            for (const auto interface_index :
                 definition.interface_type_indices) {
                const auto name = image.types[interface_index].descriptor;
                const auto interface_id = FindClass(name);
                if (!interface_id.has_value()) {
                    Fail(DexVmErrorReason::unknown_class,
                         "interface is not available: " + name +
                             " (required by " + linked.descriptor + ")");
                }
                linked.interfaces.push_back(*interface_id);
            }
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
    Fail(DexVmErrorReason::unknown_class,
         "class is not available: " + std::string(descriptor));
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
        if (target_class.descriptor == "Ljava/lang/Object;") return true;
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
