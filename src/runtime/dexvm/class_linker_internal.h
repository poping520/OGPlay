#pragma once

#include <algorithm>
#include <array>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "ogplay/core/logger.h"
#include "ogplay/runtime/dexvm/access_flags.h"
#include "ogplay/runtime/dexvm/class_linker.h"
#include "ogplay/runtime/dexvm/class_name_codec.h"

// Private glue shared by class_linker.cpp (registration and linking) and
// class_linker_resolve.cpp (constant-pool resolution and assignability).
// Not part of the public linker surface.

namespace ogplay::runtime::dexvm {

[[noreturn]] void Fail(DexVmErrorReason reason, std::string message);
// Member and type names in the supported titles are ASCII; anything else
// is preserved via UTF-16 escape formatting.
[[nodiscard]] std::string Ascii(const loader::DexString& value);
[[nodiscard]] bool IsPlatformDescriptor(std::string_view descriptor);
[[nodiscard]] bool IsWideDescriptor(std::string_view descriptor);
[[nodiscard]] bool IsRefDescriptor(std::string_view descriptor);
[[nodiscard]] char ShortyOf(std::string_view descriptor);

using DescriptorParts = MethodTypeDescriptor;

[[nodiscard]] DescriptorParts SplitDescriptor(
    const std::string& descriptor);
[[nodiscard]] std::uint16_t ArgumentWords(const DescriptorParts& parts,
                                          bool is_static);
[[nodiscard]] MethodShape ShapeOf(const DescriptorParts& parts,
                                  bool is_static);
[[nodiscard]] std::string MemberKey(const std::string& name,
                                    const std::string& descriptor);

class DexClassLinker::Impl final {
public:
    struct DexUnit final {
        DexUnitId id;
        bool boot{};
        std::vector<std::uint8_t> bytes;
        loader::DexImage image;
        std::vector<loader::DexClassData> class_data;
        std::vector<std::optional<DexClassId>> type_cache;
        static constexpr std::size_t kInvokeKindCount = 6U;
        std::vector<std::array<std::optional<ResolvedCallSite>,
                               kInvokeKindCount>> method_cache;
        std::vector<std::optional<ResolvedFieldRef>> field_cache;
    };

    struct ClassExtras final {
        std::unordered_map<std::string, std::uint16_t> virtual_lookup;
        std::unordered_map<std::string, VmMethodId> direct_lookup;
        std::unordered_map<std::string, VmFieldId> field_lookup;
        std::optional<std::string> missing_super;
        std::vector<std::string> missing_interfaces;
        bool linked{};
        bool link_failed{};
        bool skeleton_merged{};
    };

    struct OverrideWarning final {
        std::string parent, child, name, descriptor;
    };
    core::Logger* logger{};
    std::vector<OverrideWarning> pending_override_warnings;

    void WarnInaccessibleOverride(OverrideWarning warning) {
        if (logger == nullptr) {
            pending_override_warnings.push_back(std::move(warning));
            return;
        }
        logger->Write(core::LogLevel::warn, "runtime.dexvm.override",
            warning.child + "->" + warning.name + warning.descriptor +
                " overrides inaccessible method in " + warning.parent,
            {}, {{"parent_class", warning.parent}, {"child_class", warning.child},
                 {"method", warning.name}, {"descriptor", warning.descriptor}},
            {.mode = core::RateLimitMode::first_and_count});
    }

    // Lazy arrays, primitive classes and survey members may be appended while
    // frames are active.  deque keeps all previously published references
    // stable across those additions.
    std::size_t linked_class_count{};
    std::deque<LinkedClass> classes;
    std::deque<ClassExtras> extras;
    std::deque<LinkedMethod> methods;
    std::deque<LinkedField> fields;
    std::unordered_map<std::string, std::uint32_t> class_by_descriptor;
    std::unordered_map<std::uint64_t, VmFieldId> intrinsic_field_bindings;

    std::deque<DexUnit> dex_units;
    std::optional<DexUnitId> boot_unit;
    std::optional<DexUnitId> application_unit;

    std::vector<std::uint8_t> skeleton_bytes;
    std::unordered_map<std::string, std::pair<std::size_t, std::size_t>> skeleton_index;
    void MergeSkeletonMembers(DexClassId id);
    [[nodiscard]] std::optional<std::uint32_t> SkeletonMethodFlags(const LinkedMethod& method) const;
    [[nodiscard]] DexClassId MaterializeSkeletonClass(const std::string& descriptor);
    bool link_complete{};
    std::vector<std::string> implicit_intrinsic_overrides;

    // Gap survey state; the map key is "owner" or "owner->member" so the
    // report order is deterministic.
    bool gap_survey{};
    std::function<std::uint64_t()> survey_uptime_ms;
    std::map<std::string, GapSurveyHit> survey_hits;
    std::uint64_t survey_total_hits{};
    std::mutex survey_mutex;
    std::uint32_t next_annotation_impl{};

    [[nodiscard]] DexUnit& UnitAt(const DexUnitId id) {
        if (!id.IsValid() || id.value > dex_units.size()) {
            Fail(DexVmErrorReason::invalid_image, "dex unit id is invalid");
        }
        return dex_units[id.value - 1U];
    }
    [[nodiscard]] const DexUnit& UnitAt(const DexUnitId id) const {
        if (!id.IsValid() || id.value > dex_units.size()) {
            Fail(DexVmErrorReason::invalid_image, "dex unit id is invalid");
        }
        return dex_units[id.value - 1U];
    }

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
            Fail(DexVmErrorReason::invalid_member,
                 "method id is invalid (" + std::to_string(id.Value()) +
                     " of " + std::to_string(methods.size()) + " methods, " +
                     std::to_string(classes.size()) + " classes)");
        }
        return methods[id.Value() - 1];
    }
    [[nodiscard]] LinkedField& FieldAt(const VmFieldId id) {
        if (!id.IsValid() || id.Value() > fields.size()) {
            Fail(DexVmErrorReason::invalid_member, "field id is invalid");
        }
        return fields[id.Value() - 1];
    }

    [[nodiscard]] bool SamePackage(DexClassId a, DexClassId b) {
        const auto package = [](std::string_view name) {
            const auto slash = name.rfind('/');
            return slash == std::string_view::npos ? std::string_view{} : name.substr(0, slash);
        };
        return ClassAt(a).defining_loader == ClassAt(b).defining_loader &&
            package(ClassAt(a).descriptor) == package(ClassAt(b).descriptor);
    }
    void CheckClassAccess(DexClassId target, std::optional<DexClassId> caller) {
        if (!caller || ClassAt(target).descriptor.size() == 1U) return;
        auto component = target;
        while (ClassAt(component).is_array) {
            const auto& descriptor = ClassAt(component).array_element_descriptor;
            if (!IsRefDescriptor(descriptor)) return;
            const auto found = class_by_descriptor.find(descriptor);
            if (found == class_by_descriptor.end()) Fail(DexVmErrorReason::internal_invariant, "array element class is missing");
            component = classes[found->second].id;
        }
        if ((ClassAt(component).access_flags & kAccPublic) == 0U &&
            !SamePackage(component, *caller)) {
            throw JavaLinkageError(JavaLinkageKind::illegal_access,
                ClassAt(*caller).descriptor + " cannot access " + ClassAt(component).descriptor);
        }
    }
    void CheckMemberAccess(DexClassId owner, std::uint32_t flags, std::optional<DexClassId> caller) {
        if (!caller || (flags & kAccPublic) != 0U || *caller == owner) return;
        if ((flags & kAccPrivate) == 0U) {
            if (SamePackage(owner, *caller)) return;
            if ((flags & kAccProtected) != 0U) {
                auto current = ClassAt(*caller).super;
                while (current) {
                    if (*current == owner) return;
                    current = ClassAt(*current).super;
                }
            }
        }
        throw JavaLinkageError(JavaLinkageKind::illegal_access,
            ClassAt(*caller).descriptor + " cannot access member of " + ClassAt(owner).descriptor);
    }
    [[noreturn]] void BadDefinition(DexClassId id, JavaLinkageKind kind,
                                    DexVmErrorReason internal_reason, std::string message) {
        if (ClassAt(id).is_intrinsic && !ClassAt(id).is_skeleton) Fail(internal_reason, std::move(message));
        throw JavaLinkageError(kind, std::move(message));
    }
    void LinkClass(const DexClassId id, std::set<std::uint32_t>& visiting) {
        auto& extra = ExtrasAt(id);
        if (extra.link_failed) throw JavaLinkageError(JavaLinkageKind::no_class, ClassAt(id).descriptor);
        try { LinkClassBody(id, visiting); }
        catch (const JavaLinkageError&) {
            extra.link_failed = true;
            visiting.erase(id.Value());
            throw;
        }
    }

    void LinkClassBody(const DexClassId id, std::set<std::uint32_t>& visiting) {
        auto& linked = ClassAt(id);
        auto& extra = ExtrasAt(id);
        if (extra.linked) return;
        MergeSkeletonMembers(id);
        if (!visiting.insert(id.Value()).second) {
            BadDefinition(id, JavaLinkageKind::circularity, DexVmErrorReason::invalid_hierarchy,
                 "class hierarchy contains a cycle at " + linked.descriptor);
        }

        if (!linked.super && linked.descriptor != "Ljava/lang/Object;" && !linked.is_intrinsic) {
            BadDefinition(id, JavaLinkageKind::linkage, DexVmErrorReason::invalid_hierarchy, "class without superclass: " + linked.descriptor);
        }
        std::uint16_t slot_cursor = 0;
        if (linked.super.has_value()) {
            LinkClass(*linked.super, visiting);
            const auto& super = ClassAt(*linked.super);
            const auto& super_extra = ExtrasAt(*linked.super);
            if ((super.access_flags & kAccFinal) != 0) {
                BadDefinition(id, JavaLinkageKind::incompatible_class_change, DexVmErrorReason::invalid_hierarchy,
                     "final class is extended: " + super.descriptor);
            }
            if (super.is_interface) {
                BadDefinition(id, JavaLinkageKind::incompatible_class_change, DexVmErrorReason::invalid_hierarchy,
                     "interface used as superclass of " + linked.descriptor);
            }
            CheckClassAccess(*linked.super, id);
            slot_cursor = super.instance_slots;
            linked.vtable = super.vtable;
            extra.virtual_lookup = super_extra.virtual_lookup;
        }

        // Keep declared interfaces separate from the flattened iftable used
        // by assignability and dispatch. Reflection consumes only the former.
        std::vector<DexClassId> flattened;
        if (linked.super.has_value()) {
            flattened = ClassAt(*linked.super).interfaces;
        }
        const auto append_unique = [&flattened](const DexClassId candidate) {
            if (std::find(flattened.begin(), flattened.end(), candidate) ==
                flattened.end()) {
                flattened.push_back(candidate);
            }
        };
        for (const auto interface_id : linked.direct_interfaces) {
            LinkClass(interface_id, visiting);
            if (!ClassAt(interface_id).is_interface) {
                BadDefinition(id, JavaLinkageKind::incompatible_class_change, DexVmErrorReason::invalid_hierarchy,
                     "non-interface implemented by " + linked.descriptor);
            }
            CheckClassAccess(interface_id, id);
            append_unique(interface_id);
            for (const auto inherited : ClassAt(interface_id).interfaces) {
                append_unique(inherited);
            }
        }
        linked.interfaces = std::move(flattened);

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
        linked.static_ref_slots.clear();
        for (const auto field_id : linked.own_static_fields) {
            auto& field = FieldAt(field_id);
            if (field.is_wide && (static_cursor % 2) != 0) ++static_cursor;
            field.slot = static_cursor;
            if (field.is_ref) linked.static_ref_slots.push_back(field.slot);
            static_cursor = static_cast<std::uint16_t>(
                static_cursor + (field.is_wide ? 2 : 1));
            extra.field_lookup.emplace(
                MemberKey(field.name, field.descriptor), field_id);
        }
        linked.static_storage.assign(static_cursor, 0U);

        // Dalvik 4.4 createVtable: match names/prototypes even when the
        // inherited method is inaccessible (b/7301030). Private methods are direct.
        for (auto& method_id : OwnVirtualMethods(linked)) {
            auto& method = MethodAt(method_id);
            const auto key = MemberKey(method.name, method.descriptor);
            const auto existing = extra.virtual_lookup.find(key);
            const auto override_slot = existing == extra.virtual_lookup.end()
                ? std::optional<std::uint16_t>{} : std::optional{existing->second};
            if (override_slot.has_value()) {
                const auto index = *override_slot;
                if (index >= linked.vtable.size() ||
                    !linked.vtable[index].IsValid()) {
                    Fail(DexVmErrorReason::invalid_hierarchy,
                         "vtable override slot is invalid at " +
                             linked.descriptor + "." + method.name);
                }
                const auto& overridden = MethodAt(linked.vtable[index]);
                if (linked.is_intrinsic && !method.is_skeleton && !overridden.is_skeleton && !method.must_override &&
                    ClassAt(overridden.owner).is_intrinsic) {
                    implicit_intrinsic_overrides.push_back(
                        linked.descriptor + "->" + method.name +
                        method.descriptor + " overrides " +
                        ClassAt(overridden.owner).descriptor);
                }
                // Generated bodies cannot execute guest code. Validate their
                // declarations against the pinned ROM parent's flags, while
                // keeping the actual handwritten parent/handler unchanged.
                // Application overrides still use the runtime contract.
                const auto rom_flags = method.is_skeleton && !overridden.is_skeleton
                    ? SkeletonMethodFlags(overridden) : std::nullopt;
                const auto inherited_flags = rom_flags.value_or(overridden.access_flags);
                if (overridden.kind == MethodKind::intrinsic &&
                    !overridden.overridable && !linked.is_intrinsic && !rom_flags) {
                    Fail(DexVmErrorReason::invalid_override,
                         "intrinsic method is not overridable: " +
                             ClassAt(overridden.owner).descriptor + "." +
                             overridden.name);
                }
                if ((inherited_flags & kAccFinal) != 0) {
                    BadDefinition(id, JavaLinkageKind::virtual_machine, DexVmErrorReason::invalid_override,
                         "final method is overridden: " +
                             ClassAt(overridden.owner).descriptor + "." +
                             overridden.name + " by " + linked.descriptor);
                }
                if ((inherited_flags & (kAccPublic | kAccProtected)) == 0U) {
                    const auto& parent = ClassAt(overridden.owner);
                    const auto package = [](std::string_view descriptor) {
                        const auto slash = descriptor.rfind('/');
                        return slash == std::string_view::npos ? std::string_view{} : descriptor.substr(0, slash);
                    };
                    if (parent.defining_loader != linked.defining_loader ||
                        package(parent.descriptor) != package(linked.descriptor)) {
                        WarnInaccessibleOverride({parent.descriptor, linked.descriptor,
                                                  method.name, method.descriptor});
                    }
                }
                method.vtable_index = static_cast<std::int32_t>(index);
                linked.vtable[index] = method_id;
                extra.virtual_lookup.insert_or_assign(key, index);
            } else {
                if (linked.is_intrinsic && method.must_override) {
                    Fail(DexVmErrorReason::invalid_override,
                         "intrinsic override has no inherited virtual method: " +
                             linked.descriptor + "." + method.name +
                             method.descriptor);
                }
                method.vtable_index =
                    static_cast<std::int32_t>(linked.vtable.size());
                extra.virtual_lookup.insert_or_assign(
                    key, static_cast<std::uint16_t>(linked.vtable.size()));
                linked.vtable.push_back(method_id);
            }
        }
        // Dalvik Miranda slots allow invoke-virtual on an abstract class to
        // resolve methods inherited only from its interfaces (AbstractQueue.poll).
        // These are dispatch slots, not new reflection-visible declarations.
        for (const auto interface_id : linked.interfaces) {
            for (const auto interface_method : ClassAt(interface_id).own_virtual_methods) {
                const auto& declaration = MethodAt(interface_method);
                const auto key = MemberKey(declaration.name, declaration.descriptor);
                if (extra.virtual_lookup.contains(key)) continue;
                auto miranda = declaration;
                miranda.kind = MethodKind::abstract;
                miranda.implementation = {};
                miranda.access_flags |= kAccAbstract;
                miranda.vtable_index = static_cast<std::int32_t>(linked.vtable.size());
                extra.virtual_lookup.emplace(key, static_cast<std::uint16_t>(linked.vtable.size()));
                linked.vtable.push_back(AddMethod(std::move(miranda)));
            }
        }
        extra.linked = true;
        ++linked_class_count;
        visiting.erase(id.Value());
    }

    [[nodiscard]] std::vector<VmMethodId> OwnVirtualMethods(
        const LinkedClass& linked) {
        return linked.own_virtual_methods;
    }
};

}  // namespace ogplay::runtime::dexvm
