#include "class_linker_internal.h"

namespace ogplay::runtime::dexvm {
namespace {

[[nodiscard]] std::uint8_t LoaderMask(const VmClassLoaderId loader) {
    if (loader == kBootstrapLoader) return 0x01U;
    if (loader == kApplicationLoader) return 0x02U;
    Fail(DexVmErrorReason::internal_invariant,
         "class loader role is invalid");
}

}  // namespace

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
    // 临时方案：OGPlay 尚未加载完整的 API 19 boot class path，而是以
    // intrinsic catalog 代替 bootstrap 类，因此目前只能按 descriptor 前缀近似判断
    // 平台归属。旧版 Android Support Library 虽位于 android.* 命名空间，实际是 APK
    // 自带的应用字节码，必须由 application loader 定义。
    //
    // 正确做法（后续实现）：从固定的 API 19 BOOTCLASSPATH 产物生成精确类描述符索引，
    // 按 boot class path 和 defining ClassLoader 判断归属；intrinsic catalog 只表示
    // 平台类的实现状态。届时应移除此前缀规则及 android/support 特例。
    return (descriptor.starts_with("Landroid/") &&
            !descriptor.starts_with("Landroid/support/")) ||
           descriptor.starts_with("Ljava/") ||
           descriptor.starts_with("Ljavax/") ||
           descriptor.starts_with("Ldalvik/") ||
           descriptor.starts_with("Lorg/apache/http/") ||
           // AOSP 随系统提供的库包（SAX、JSON）也暂按平台类处理。
           descriptor.starts_with("Lorg/xml/") ||
           descriptor.starts_with("Lorg/json/");
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
    try {
        return ClassNameCodec::ParseMethod(descriptor);
    } catch (const ClassNameCodecError&) {
        Fail(DexVmErrorReason::invalid_member,
             "method descriptor is malformed: " + descriptor);
    }
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

[[nodiscard]] MethodShape ShapeOf(const DescriptorParts& parts,
                                  const bool is_static) {
    const auto kind_of = [](const std::string_view descriptor) {
        if (descriptor == "V") return ValueKind::void_value;
        if (IsWideDescriptor(descriptor)) return ValueKind::wide;
        if (IsRefDescriptor(descriptor)) return ValueKind::ref;
        return ValueKind::cat1;
    };
    MethodShape shape;
    shape.return_kind = kind_of(parts.return_type);
    std::uint32_t words = is_static ? 0U : 1U;
    shape.parameter_kinds.reserve(parts.parameters.size());
    shape.parameter_word_offsets.reserve(parts.parameters.size());
    for (const auto& parameter : parts.parameters) {
        if (words > 0xffffU) {
            Fail(DexVmErrorReason::invalid_member,
                 "method argument word offset overflow");
        }
        shape.parameter_word_offsets.push_back(
            static_cast<std::uint16_t>(words));
        const auto kind = kind_of(parameter);
        shape.parameter_kinds.push_back(kind);
        words += kind == ValueKind::wide ? 2U : 1U;
    }
    if (words > 0xffffU) {
        Fail(DexVmErrorReason::invalid_member,
             "method argument words overflow");
    }
    shape.incoming_words = static_cast<std::uint16_t>(words);
    return shape;
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
        if (const auto existing = FindClass(declaration.descriptor);
            existing.has_value() && impl_->ClassAt(*existing).is_boot_dex) {
            Fail(DexVmErrorReason::internal_invariant,
                 "intrinsic overlays must be registered before boot dex: " +
                     declaration.descriptor);
        }
        LinkedClass linked;
        linked.descriptor = declaration.descriptor;
        linked.is_intrinsic = true;
        linked.is_interface = declaration.is_interface;
        linked.defining_loader = kBootstrapLoader;
        linked.initiating_loader_mask = LoaderMask(kBootstrapLoader);
        linked.access_flags = declaration.access_flags;
        const auto id = impl_->AddClass(std::move(linked));
        pending.push_back({&declaration, id});
    }
    for (const auto& [declaration, id] : pending) {
        auto& linked = impl_->ClassAt(id);
        auto& extra = impl_->ExtrasAt(id);
        if (declaration->superclass.has_value()) {
            const auto super = FindClass(*declaration->superclass);
            if (super.has_value()) linked.super = *super;
            else extra.missing_super = *declaration->superclass;
        }
        // Resolve the whole list together so BootDex dependencies do not move
        // behind already registered interfaces in reflection metadata.
        extra.missing_interfaces = declaration->interfaces;
        for (const auto& method : declaration->methods) {
            if (method.guest_native && (method.implementation || !method.is_static ||
                !(method.access_flags & kAccNative) || (method.access_flags & kAccAbstract))) {
                Fail(DexVmErrorReason::invalid_member, "invalid guest JNI admission: " + method.name);
            }
            LinkedMethod linked_method;
            linked_method.owner = id;
            linked_method.name = method.name;
            linked_method.descriptor = method.descriptor;
            linked_method.is_static = method.is_static;
            linked_method.access_flags = method.access_flags;
            linked_method.kind = method.guest_native ? MethodKind::native : MethodKind::intrinsic;
            linked_method.overridable = method.overridable;
            linked_method.declared_invoke_kind =
                declaration->is_interface &&
                        method.invoke_kind == DeclaredInvokeKind::virtual_call
                    ? DeclaredInvokeKind::interface_call
                    : method.invoke_kind;
            linked_method.must_override = method.must_override;
            linked_method.implementation = method.implementation;
            const auto parts = SplitDescriptor(method.descriptor);
            linked_method.shape = ShapeOf(parts, method.is_static);
            linked_method.return_shorty = ShortyOf(parts.return_type);
            linked_method.ins_words = linked_method.shape.incoming_words;
            const bool is_direct =
                linked_method.declared_invoke_kind ==
                    DeclaredInvokeKind::direct ||
                linked_method.declared_invoke_kind ==
                    DeclaredInvokeKind::static_call;
            // vtable_index -2 marks "virtual, pending vtable placement".
            linked_method.vtable_index = is_direct ? -1 : -2;
            const auto method_id = impl_->AddMethod(std::move(linked_method));
            if (is_direct) {
                linked.own_direct_methods.push_back(method_id);
                extra.direct_lookup.emplace(
                    MemberKey(method.name, method.descriptor), method_id);
            } else {
                linked.own_virtual_methods.push_back(method_id);
            }
        }
        for (const auto& declared_field : declaration->fields) {
            LinkedField field;
            field.owner = id;
            field.name = declared_field.name;
            field.descriptor = declared_field.descriptor;
            field.is_static = declared_field.is_static;
            field.access_flags = declared_field.access_flags;
            field.is_wide = IsWideDescriptor(declared_field.descriptor);
            field.is_ref = IsRefDescriptor(declared_field.descriptor);
            const auto field_id = impl_->AddField(std::move(field));
            if (declared_field.binding_token != 0U &&
                !impl_->intrinsic_field_bindings
                     .emplace(declared_field.binding_token, field_id)
                     .second) {
                Fail(DexVmErrorReason::internal_invariant,
                     "duplicate intrinsic field binding token");
            }
            (declared_field.is_static ? linked.own_static_fields
                                      : linked.own_instance_fields)
                .push_back(field_id);
            if (declared_field.has_constant) {
                linked.intrinsic_constants.push_back(declared_field);
            }
        }
        linked.clinit_implementation = declaration->clinit_implementation;
        linked.host_state_destructor = declaration->host_state_destructor;
    }
}

VmFieldId DexClassLinker::ResolveIntrinsicFieldBinding(
    const std::uint64_t token) const {
    const auto found = impl_->intrinsic_field_bindings.find(token);
    if (token == 0U || found == impl_->intrinsic_field_bindings.end()) {
        Fail(DexVmErrorReason::internal_invariant,
             "intrinsic field handle is not bound to this linker");
    }
    return found->second;
}

DexUnitId DexClassLinker::RegisterBootDex(
    std::vector<std::uint8_t> dex_bytes) {
    return RegisterDexUnit(std::move(dex_bytes), true);
}

DexUnitId DexClassLinker::RegisterDex(std::vector<std::uint8_t> dex_bytes) {
    return RegisterDexUnit(std::move(dex_bytes), false);
}

DexUnitId DexClassLinker::RegisterDexUnit(
    std::vector<std::uint8_t> dex_bytes, const bool boot) {
    if (impl_->link_complete) {
        Fail(DexVmErrorReason::internal_invariant,
             "dex registered after linking");
    }
    if ((boot && impl_->boot_unit.has_value()) ||
        (!boot && impl_->application_unit.has_value())) {
        Fail(DexVmErrorReason::invalid_image,
             boot ? "only a single boot dex is supported"
                  : "only a single application dex is supported");
    }
    const auto unit_id = DexUnitId{
        static_cast<std::uint32_t>(impl_->dex_units.size() + 1U)};
    auto image = loader::ParseDex(dex_bytes);
    auto class_data = loader::ReadDexClassData(dex_bytes, image);
    impl_->dex_units.push_back({unit_id,
                                boot,
                                std::move(dex_bytes),
                                std::move(image),
                                std::move(class_data),
                                {},
                                {},
                                {}});
    if (boot) {
        impl_->boot_unit = unit_id;
    } else {
        impl_->application_unit = unit_id;
    }
    auto& unit = impl_->UnitAt(unit_id);
    unit.type_cache.assign(unit.image.types.size(), std::nullopt);
    unit.method_cache.assign(unit.image.methods.size(), {});
    unit.field_cache.assign(unit.image.fields.size(), std::nullopt);
    const auto& image_ref = unit.image;

    for (std::uint32_t class_index = 0; class_index < image_ref.classes.size();
         ++class_index) {
        const auto& definition = image_ref.classes[class_index];
        const auto descriptor =
            image_ref.types[definition.class_type_index].descriptor;
        if (!boot && IsPlatformDescriptor(descriptor)) {
            // 临时规则判定为平台所有的 APK 类不参与解释（03 §1），由 intrinsic
            // catalog 胜出；android.support.* 已在上方作为应用类排除。
            continue;
        }
        const auto existing = FindClass(descriptor);
        const bool merge_intrinsic =
            boot && existing.has_value() &&
            impl_->ClassAt(*existing).is_intrinsic;
        DexClassId id;
        if (merge_intrinsic) {
            id = *existing;
            auto& linked = impl_->ClassAt(id);
            linked.is_intrinsic = false;
            linked.is_boot_dex = true;
            linked.is_interface =
                (definition.access_flags & kAccInterface) != 0;
            linked.defining_loader = kBootstrapLoader;
            linked.initiating_loader_mask = LoaderMask(kBootstrapLoader);
            linked.access_flags = definition.access_flags;
            linked.super.reset();
            linked.direct_interfaces.clear();
            impl_->ExtrasAt(id).missing_super.reset();
            impl_->ExtrasAt(id).missing_interfaces.clear();
            linked.intrinsic_constants.clear();
            linked.dex_class_def_index = class_index;
            linked.dex_unit = unit_id;
        } else {
            LinkedClass linked;
            linked.descriptor = descriptor;
            linked.defining_loader =
                boot ? kBootstrapLoader : kApplicationLoader;
            linked.initiating_loader_mask =
                boot ? LoaderMask(kBootstrapLoader) : 0U;
            linked.access_flags = definition.access_flags;
            linked.is_interface =
                (definition.access_flags & kAccInterface) != 0;
            linked.is_boot_dex = boot;
            linked.dex_class_def_index = class_index;
            linked.dex_unit = unit_id;
            id = impl_->AddClass(std::move(linked));
        }

        const auto& data = unit.class_data[class_index];
        auto& stored = impl_->ClassAt(id);
        auto& extra = impl_->ExtrasAt(id);
        for (const auto* field_list :
             {&data.static_fields, &data.instance_fields}) {
            const bool is_static = field_list == &data.static_fields;
            std::vector<VmFieldId> dex_order;
            for (const auto& encoded : *field_list) {
                const auto& field_id_entry = image_ref.fields[encoded.field_index];
                LinkedField field;
                field.owner = id;
                field.name =
                    Ascii(image_ref.strings[field_id_entry.name_string_index]);
                field.descriptor =
                    image_ref.types[field_id_entry.type_index].descriptor;
                field.access_flags = encoded.access_flags;
                field.is_static = is_static;
                field.is_wide = IsWideDescriptor(field.descriptor);
                field.is_ref = IsRefDescriptor(field.descriptor);
                if (encoded.field_index < image_ref.field_runtime_metadata.size()) {
                    for (const auto& annotation :
                         image_ref.field_runtime_metadata[encoded.field_index].annotations) {
                        field.runtime_annotations.push_back({
                            image_ref.types[annotation.type_index].descriptor,
                            annotation.has_elements});
                    }
                }
                auto& own_fields = is_static ? stored.own_static_fields
                                             : stored.own_instance_fields;
                const auto existing_field = std::find_if(
                    own_fields.begin(), own_fields.end(),
                    [&](const VmFieldId candidate) {
                        const auto& current = impl_->FieldAt(candidate);
                        return current.name == field.name &&
                               current.descriptor == field.descriptor;
                    });
                if (existing_field != own_fields.end()) {
                    auto& current = impl_->FieldAt(*existing_field);
                    current.access_flags = field.access_flags;
                    current.is_wide = field.is_wide;
                    current.is_ref = field.is_ref;
                    current.runtime_annotations = std::move(field.runtime_annotations);
                    dex_order.push_back(*existing_field);
                } else {
                    const auto field_id = impl_->AddField(std::move(field));
                    own_fields.push_back(field_id);
                    dex_order.push_back(field_id);
                }
            }
            if (is_static && merge_intrinsic) {
                // encoded_array values are indexed by DEX declaration order,
                // not by the order in which overlay fields were registered.
                for (const auto field : stored.own_static_fields) {
                    if (std::find(dex_order.begin(), dex_order.end(), field) == dex_order.end())
                        dex_order.push_back(field);
                }
                stored.own_static_fields = std::move(dex_order);
            }
        }
        for (const auto* method_list :
             {&data.direct_methods, &data.virtual_methods}) {
            const bool direct = method_list == &data.direct_methods;
            for (const auto& encoded : *method_list) {
                const auto& method_id_entry =
                    image_ref.methods[encoded.method_index];
                LinkedMethod method;
                method.owner = id;
                method.name =
                    Ascii(image_ref.strings[method_id_entry.name_string_index]);
                const auto& prototype =
                    image_ref.prototypes[method_id_entry.prototype_index];
                std::string method_descriptor = "(";
                for (const auto parameter : prototype.parameter_type_indices) {
                    method_descriptor += image_ref.types[parameter].descriptor;
                }
                method_descriptor += ")";
                method_descriptor +=
                    image_ref.types[prototype.return_type_index].descriptor;
                method.descriptor = std::move(method_descriptor);
                method.access_flags = encoded.access_flags;
                method.dex_method_index = encoded.method_index;
                method.dex_unit = unit_id;
                method.is_static = (encoded.access_flags & kAccStatic) != 0;
                method.declared_invoke_kind = direct
                    ? (method.is_static ? DeclaredInvokeKind::static_call
                                        : DeclaredInvokeKind::direct)
                    : (stored.is_interface
                           ? DeclaredInvokeKind::interface_call
                           : DeclaredInvokeKind::virtual_call);
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
                        unit.bytes, image_ref, *encoded.code);
                }
                const auto parts = SplitDescriptor(method.descriptor);
                method.shape = ShapeOf(parts, method.is_static);
                method.return_shorty = ShortyOf(parts.return_type);
                method.ins_words = method.shape.incoming_words;
                if (method.code.has_value() &&
                    method.code->info.incoming_words != method.ins_words) {
                    Fail(DexVmErrorReason::invalid_code,
                         "method ins does not match descriptor: " +
                             stored.descriptor + "." + method.name);
                }
                method.vtable_index = direct ? -1 : -2;
                auto& own_methods = direct ? stored.own_direct_methods
                                           : stored.own_virtual_methods;
                const auto existing_method = std::find_if(
                    own_methods.begin(), own_methods.end(),
                    [&](const VmMethodId candidate) {
                        const auto& current = impl_->MethodAt(candidate);
                        return current.name == method.name &&
                               current.descriptor == method.descriptor;
                    });
                if (existing_method != own_methods.end()) {
                    auto& current = impl_->MethodAt(*existing_method);
                    if (current.is_static != method.is_static ||
                        current.declared_invoke_kind !=
                            method.declared_invoke_kind) {
                        Fail(DexVmErrorReason::invalid_member,
                             "intrinsic overlay invoke kind differs from "
                             "boot dex: " + stored.descriptor + "." +
                                 method.name + method.descriptor);
                    }
                    if (current.kind == MethodKind::native && method.kind != MethodKind::native) {
                        Fail(DexVmErrorReason::invalid_member, "guest JNI admission requires a boot native declaration");
                    }
                    current.access_flags = method.access_flags;
                    current.dex_method_index = method.dex_method_index;
                    current.dex_unit = method.dex_unit;
                    current.code = std::move(method.code);
                    current.shape = std::move(method.shape);
                    current.return_shorty = method.return_shorty;
                    current.ins_words = method.ins_words;
                    if (current.name == "<clinit>") {
                        stored.clinit = current.id;
                    }
                } else {
                    if (method.kind == MethodKind::native && boot) {
                        Fail(DexVmErrorReason::invalid_member,
                             "curated boot dex contains an unbound native "
                             "method: " + stored.descriptor + "." +
                                 method.name);
                    }
                    const auto vm_method_id =
                        impl_->AddMethod(std::move(method));
                    own_methods.push_back(vm_method_id);
                    if (direct) {
                        const auto& added = impl_->MethodAt(vm_method_id);
                        extra.direct_lookup.emplace(
                            MemberKey(added.name, added.descriptor),
                            vm_method_id);
                        if (added.name == "<clinit>") {
                            stored.clinit = vm_method_id;
                        }
                    }
                }
            }
        }
    }
    return unit_id;
}

void DexClassLinker::Link() {
    if (impl_->link_complete) return;
    if (!FindClass("Ljava/lang/Object;").has_value()) {
        Fail(DexVmErrorReason::unknown_class,
             "core intrinsic catalog is not registered");
    }
    // Native-backed classes may implement interfaces supplied by the BootDex
    // registered after their declarations. Resolve those names before linking.
    for (auto& linked : impl_->classes) {
        if (!linked.is_intrinsic) continue;
        auto& extra = impl_->ExtrasAt(linked.id);
        const auto require = [&](const std::string& name) {
            const auto found = FindClass(name);
            if (!found) Fail(DexVmErrorReason::unknown_class,
                             "intrinsic hierarchy is not registered: " + name);
            return *found;
        };
        if (extra.missing_super) linked.super = require(*extra.missing_super);
        for (const auto& name : extra.missing_interfaces)
            linked.direct_interfaces.push_back(require(name));
        extra.missing_super.reset();
        extra.missing_interfaces.clear();
    }
    // Resolve hierarchy names that are already available. A DEX commonly
    // contains optional SDK/support classes that the selected execution path
    // never loads. Keep classes with absent hierarchy nodes registered but
    // defer their layout/vtable work until first use, so those dormant classes
    // do not become process-start requirements.
    for (auto& linked : impl_->classes) {
        if (linked.is_intrinsic || !linked.dex_class_def_index.has_value() ||
            !linked.dex_unit.has_value()) {
            continue;
        }
        const auto& image = impl_->UnitAt(*linked.dex_unit).image;
        const auto& definition = image.classes[*linked.dex_class_def_index];
        if (definition.superclass_type_index.has_value()) {
            const auto name =
                image.types[*definition.superclass_type_index].descriptor;
            const auto super = FindClass(name);
            if (!super.has_value()) {
                impl_->ExtrasAt(linked.id).missing_super = name;
            } else {
                linked.super = *super;
            }
        } else if (linked.descriptor != "Ljava/lang/Object;") {
            Fail(DexVmErrorReason::invalid_hierarchy,
                 "class without superclass: " + linked.descriptor);
        }
        for (const auto interface_index : definition.interface_type_indices) {
            const auto name = image.types[interface_index].descriptor;
            const auto interface_id = FindClass(name);
            if (!interface_id.has_value()) {
                impl_->ExtrasAt(linked.id).missing_interfaces.push_back(name);
                continue;
            }
            linked.direct_interfaces.push_back(*interface_id);
        }
    }
    std::set<std::uint32_t> visiting;
    for (auto& linked : impl_->classes) {
        // Platform catalogs are the VM's startup substrate. APK classes are
        // deliberately left registered-but-unlinked until their first real
        // use, at which point EnsureClassLinked runs every hierarchy/layout/
        // override check for that reachable chain.
        if (linked.is_intrinsic) impl_->LinkClass(linked.id, visiting);
    }
    if (!impl_->implicit_intrinsic_overrides.empty()) {
        std::string message =
            "intrinsic virtual declarations must use OverrideMethod:\n";
        for (const auto& violation : impl_->implicit_intrinsic_overrides) {
            message += "  " + violation + "\n";
        }
        Fail(DexVmErrorReason::invalid_override, std::move(message));
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

void DexClassLinker::EnsureClassLinked(const DexClassId id) {
    if (!impl_->link_complete) {
        Fail(DexVmErrorReason::internal_invariant,
             "class used before initial linking completed");
    }
    MarkInitiatedBy(id, impl_->ClassAt(id).defining_loader);
    if (impl_->ExtrasAt(id).linked) return;

    std::set<std::uint32_t> materializing;
    const auto materialize = [&](const auto& self,
                                 const DexClassId current) -> void {
        if (impl_->ExtrasAt(current).linked) return;
        if (!materializing.insert(current.Value()).second) return;

        // ResolveDescriptor may append a survey class and reallocate both
        // class vectors, so copy pending names and always re-fetch by id.
        const auto missing_super = impl_->ExtrasAt(current).missing_super;
        const auto missing_interfaces =
            impl_->ExtrasAt(current).missing_interfaces;
        const auto required_by = impl_->ClassAt(current).descriptor;

        if (missing_super.has_value()) {
            if (!impl_->gap_survey ||
                !IsPlatformDescriptor(*missing_super)) {
                Fail(DexVmErrorReason::unknown_class,
                     "class hierarchy is not available: " + *missing_super +
                         " (required by " + required_by + ")");
            }
            const auto super = ResolveDescriptor(*missing_super);
            impl_->ClassAt(current).super = super;
            impl_->ExtrasAt(current).missing_super.reset();
        }
        if (!missing_interfaces.empty()) {
            for (const auto& name : missing_interfaces) {
                if (!impl_->gap_survey || !IsPlatformDescriptor(name)) {
                    Fail(DexVmErrorReason::unknown_class,
                         "class hierarchy is not available: " + name +
                             " (required by " + required_by + ")");
                }
                const auto interface_id = ResolveDescriptor(name);
                impl_->ClassAt(interface_id).is_interface = true;
                impl_->ClassAt(current).direct_interfaces.push_back(interface_id);
            }
            impl_->ExtrasAt(current).missing_interfaces.clear();
        }

        const auto super = impl_->ClassAt(current).super;
        const auto interfaces = impl_->ClassAt(current).direct_interfaces;
        if (super.has_value()) self(self, *super);
        for (const auto interface_id : interfaces) {
            self(self, interface_id);
        }
        materializing.erase(current.Value());
    };
    materialize(materialize, id);

    std::set<std::uint32_t> visiting;
    impl_->LinkClass(id, visiting);
}

DexClassId DexClassLinker::ResolveDescriptor(
    const std::string_view descriptor) {
    if (const auto existing = FindClass(descriptor); existing.has_value()) {
        if (impl_->link_complete) EnsureClassLinked(*existing);
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
        if (IsRefDescriptor(element)) {
            linked.defining_loader =
                impl_->ClassAt(ResolveDescriptor(element)).defining_loader;
        } else {
            linked.defining_loader = kBootstrapLoader;
        }
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
        MarkInitiatedBy(id, impl_->ClassAt(id).defining_loader);
        return id;
    }
    if (descriptor.size() == 1 &&
        std::string_view("ZBSCIJFDV").find(descriptor) !=
            std::string_view::npos) {
        // Primitive class identity (Float.TYPE, Array.newInstance): a
        // synthesized class record that only backs a class object. It is
        // never an instance class and never participates in dispatch.
        LinkedClass linked;
        linked.descriptor = std::string(descriptor);
        linked.is_intrinsic = true;
        linked.defining_loader = kBootstrapLoader;
        linked.super = FindClass("Ljava/lang/Object;");
        const auto id = impl_->AddClass(std::move(linked));
        if (impl_->link_complete && impl_->ClassAt(id).super.has_value()) {
            auto& stored = impl_->ClassAt(id);
            auto& object_class = impl_->ClassAt(*stored.super);
            stored.vtable = object_class.vtable;
            impl_->ExtrasAt(id).virtual_lookup =
                impl_->ExtrasAt(object_class.id).virtual_lookup;
            impl_->ExtrasAt(id).linked = true;
        }
        MarkInitiatedBy(id, kBootstrapLoader);
        return id;
    }
    if (impl_->gap_survey && IsPlatformDescriptor(descriptor)) {
        // Survey mode: stand the platform class up as a neutral intrinsic so
        // the run continues and the gap is reported instead of aborting.
        LinkedClass linked;
        linked.descriptor = std::string(descriptor);
        linked.is_intrinsic = true;
        linked.defining_loader = kBootstrapLoader;
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

bool DexClassLinker::IsInitiatedBy(const DexClassId java_class,
                                   const VmClassLoaderId loader) const {
    return (impl_->ClassAt(java_class).initiating_loader_mask &
            LoaderMask(loader)) != 0U;
}

void DexClassLinker::MarkInitiatedBy(const DexClassId java_class,
                                     const VmClassLoaderId loader) {
    impl_->ClassAt(java_class).initiating_loader_mask |= LoaderMask(loader);
}

void DexClassLinker::RecordGapSurveyHit(const std::string& owner_descriptor,
                                        const std::string& member) {
    const auto key = member.empty() ? owner_descriptor
                                    : owner_descriptor + "->" + member;
    ++impl_->survey_hits[key];
}

VmMethodId DexClassLinker::SynthesizeSurveyMethod(
    const DexClassId owner, const std::string& name,
    const std::string& descriptor, const InvokeKind kind) {
    if (!impl_->gap_survey) {
        Fail(DexVmErrorReason::internal_invariant,
             "survey stubs require survey mode");
    }
    LinkedMethod method;
    method.owner = owner;
    method.name = name;
    method.descriptor = descriptor;
    const bool is_static = kind == InvokeKind::static_call;
    method.is_static = is_static;
    method.access_flags = is_static ? kAccStatic : 0U;
    method.declared_invoke_kind =
        is_static ? DeclaredInvokeKind::static_call
        : (kind == InvokeKind::constructor ||
           kind == InvokeKind::private_direct)
            ? DeclaredInvokeKind::direct
        : impl_->ClassAt(owner).is_interface
            ? DeclaredInvokeKind::interface_call
            : DeclaredInvokeKind::virtual_call;
    method.kind = MethodKind::intrinsic;
    // Deliberately unregistered: the interpreter answers neutrally and
    // records the hit, so the stub can never be mistaken for an
    // implementation.
    const auto parts = SplitDescriptor(descriptor);
    method.shape = ShapeOf(parts, is_static);
    method.return_shorty = ShortyOf(parts.return_type);
    method.ins_words = method.shape.incoming_words;
    const bool is_direct =
        is_static || kind == InvokeKind::constructor ||
        kind == InvokeKind::private_direct;
    auto& linked = impl_->ClassAt(owner);
    auto& extra = impl_->ExtrasAt(owner);
    const auto slot = static_cast<std::uint16_t>(linked.vtable.size());
    method.vtable_index = is_direct ? -1 : static_cast<std::int32_t>(slot);
    const auto id = impl_->AddMethod(std::move(method));
    if (is_direct) {
        linked.own_direct_methods.push_back(id);
        extra.direct_lookup.insert_or_assign(MemberKey(name, descriptor), id);
    } else {
        linked.own_virtual_methods.push_back(id);
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
    if (!impl_->application_unit.has_value()) {
        Fail(DexVmErrorReason::invalid_image, "no dex image is registered");
    }
    return Image(*impl_->application_unit);
}

const loader::DexImage& DexClassLinker::Image(const DexUnitId unit) const {
    return impl_->UnitAt(unit).image;
}

std::span<const std::uint8_t> DexClassLinker::DexBytes() const {
    if (!impl_->application_unit.has_value()) {
        Fail(DexVmErrorReason::invalid_image, "no dex image is registered");
    }
    return DexBytes(*impl_->application_unit);
}

std::span<const std::uint8_t> DexClassLinker::DexBytes(
    const DexUnitId unit) const {
    return impl_->UnitAt(unit).bytes;
}

std::vector<loader::DexEncodedValue> DexClassLinker::StaticValues(
    const LinkedClass& linked) const {
    if (!linked.dex_class_def_index.has_value() ||
        !linked.dex_unit.has_value()) {
        return {};
    }
    const auto& unit = impl_->UnitAt(*linked.dex_unit);
    const auto& definition = unit.image.classes[*linked.dex_class_def_index];
    return loader::ReadDexStaticValues(unit.bytes, unit.image,
                                       definition.static_values_offset);
}

std::size_t DexClassLinker::ClassCount() const noexcept {
    return impl_->classes.size();
}

std::vector<VmMethodId> DexClassLinker::MethodsOf(
    const DexClassId owner) const {
    const auto& linked = impl_->ClassAt(owner);
    std::vector<VmMethodId> result = linked.own_direct_methods;
    result.insert(result.end(), linked.own_virtual_methods.begin(),
                  linked.own_virtual_methods.end());
    return result;
}

std::vector<DexClassId> DexClassLinker::AllClasses() const {
    std::vector<DexClassId> result;
    result.reserve(impl_->classes.size());
    for (const auto& linked : impl_->classes) result.push_back(linked.id);
    return result;
}

ReflectionClassSystemMetadata DexClassLinker::ReflectionSystemMetadata(
    const DexClassId java_class) {
    ReflectionClassSystemMetadata result;
    const auto& linked = impl_->ClassAt(java_class);
    if (!linked.dex_class_def_index.has_value() ||
        !linked.dex_unit.has_value()) {
        return result;
    }
    const auto unit_id = *linked.dex_unit;
    const auto& image = impl_->UnitAt(unit_id).image;
    const auto class_index = *linked.dex_class_def_index;
    if (class_index >= image.class_system_metadata.size()) return result;
    const auto& source = image.class_system_metadata[class_index];
    result.has_inner_class = source.has_inner_class;
    result.inner_name = source.inner_name;
    result.inner_access_flags = source.inner_access_flags;
    const auto resolve_type = [&](const std::uint32_t type_index) {
        return ResolveDescriptor(image.types[type_index].descriptor);
    };
    if (source.enclosing_class_type_index.has_value()) {
        result.enclosing_class = resolve_type(
            *source.enclosing_class_type_index);
    }
    if (source.enclosing_method_index.has_value()) {
        const auto dex_method = *source.enclosing_method_index;
        const auto& method_id = image.methods[dex_method];
        result.enclosing_class = resolve_type(method_id.class_type_index);
        const auto found = std::find_if(
            impl_->methods.begin(), impl_->methods.end(),
            [dex_method, unit_id](const LinkedMethod& method) {
                return method.dex_method_index == dex_method &&
                       method.dex_unit == unit_id;
            });
        if (found != impl_->methods.end()) result.enclosing_method = found->id;
    }
    result.member_classes.reserve(source.member_class_type_indices.size());
    for (const auto type_index : source.member_class_type_indices) {
        result.member_classes.push_back(resolve_type(type_index));
    }
    return result;
}

std::vector<DexClassId> DexClassLinker::ReflectionExceptionTypes(
    const VmMethodId method) {
    const auto& linked = impl_->MethodAt(method);
    if (!linked.dex_method_index.has_value() ||
        !linked.dex_unit.has_value()) {
        return {};
    }
    const auto& image = impl_->UnitAt(*linked.dex_unit).image;
    const auto dex_method = *linked.dex_method_index;
    if (dex_method >= image.method_system_metadata.size()) return {};
    std::vector<DexClassId> result;
    for (const auto type_index :
         image.method_system_metadata[dex_method].exception_type_indices) {
        result.push_back(
            ResolveDescriptor(image.types[type_index].descriptor));
    }
    return result;
}
}  // namespace ogplay::runtime::dexvm
