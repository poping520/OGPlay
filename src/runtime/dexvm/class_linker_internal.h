#pragma once

#include <algorithm>
#include <array>
#include <deque>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

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
    struct ClassExtras final {
        std::unordered_map<std::string, std::uint16_t> virtual_lookup;
        std::unordered_map<std::string, VmMethodId> direct_lookup;
        std::unordered_map<std::string, VmFieldId> field_lookup;
        std::optional<std::string> missing_super;
        std::vector<std::string> missing_interfaces;
        bool linked{};
    };

    // Lazy arrays, primitive classes and survey members may be appended while
    // frames are active.  deque keeps all previously published references
    // stable across those additions.
    std::deque<LinkedClass> classes;
    std::deque<ClassExtras> extras;
    std::deque<LinkedMethod> methods;
    std::deque<LinkedField> fields;
    std::unordered_map<std::string, std::uint32_t> class_by_descriptor;
    std::unordered_map<std::uint64_t, VmFieldId> intrinsic_field_bindings;

    std::vector<std::uint8_t> dex_bytes;
    std::optional<loader::DexImage> image;
    std::vector<loader::DexClassData> class_data;

    std::vector<std::optional<DexClassId>> type_cache;
    static constexpr std::size_t kInvokeKindCount = 6U;
    std::vector<std::array<std::optional<ResolvedCallSite>,
                           kInvokeKindCount>> method_cache;
    std::vector<std::optional<ResolvedFieldRef>> field_cache;

    bool link_complete{};
    std::vector<std::string> implicit_intrinsic_overrides;

    // Gap survey state; the map key is "owner" or "owner->member" so the
    // report order is deterministic.
    bool gap_survey{};
    std::map<std::string, std::uint32_t> survey_hits;

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
                Fail(DexVmErrorReason::invalid_hierarchy,
                     "non-interface implemented by " + linked.descriptor);
            }
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

        // vtable: override by name+descriptor or append.
        for (auto& method_id : OwnVirtualMethods(linked)) {
            auto& method = MethodAt(method_id);
            const auto key = MemberKey(method.name, method.descriptor);
            const auto existing = extra.virtual_lookup.find(key);
            if (existing != extra.virtual_lookup.end()) {
                const auto index = existing->second;
                if (index >= linked.vtable.size() ||
                    !linked.vtable[index].IsValid()) {
                    Fail(DexVmErrorReason::invalid_hierarchy,
                         "vtable override slot is invalid at " +
                             linked.descriptor + "." + method.name);
                }
                const auto& overridden = MethodAt(linked.vtable[index]);
                if (linked.is_intrinsic && !method.must_override) {
                    implicit_intrinsic_overrides.push_back(
                        linked.descriptor + "->" + method.name +
                        method.descriptor + " overrides " +
                        ClassAt(overridden.owner).descriptor);
                }
                if (overridden.kind == MethodKind::intrinsic &&
                    !overridden.overridable && !linked.is_intrinsic) {
                    Fail(DexVmErrorReason::invalid_override,
                         "intrinsic method is not overridable: " +
                             ClassAt(overridden.owner).descriptor + "." +
                             overridden.name);
                }
                if ((overridden.access_flags & kAccFinal) != 0) {
                    Fail(DexVmErrorReason::invalid_override,
                         "final method is overridden: " +
                             ClassAt(overridden.owner).descriptor + "." +
                             overridden.name + " by " + linked.descriptor);
                }
                const auto visibility_rank = [](const std::uint32_t flags) {
                    if ((flags & kAccPublic) != 0U) return 3;
                    if ((flags & kAccProtected) != 0U) return 2;
                    if ((flags & kAccPrivate) != 0U) return 0;
                    return 1;  // package visibility
                };
                if (visibility_rank(method.access_flags) <
                    visibility_rank(overridden.access_flags)) {
                    Fail(DexVmErrorReason::invalid_override,
                         "override narrows visibility: " + linked.descriptor +
                             "." + method.name + method.descriptor);
                }
                method.vtable_index = static_cast<std::int32_t>(index);
                linked.vtable[index] = method_id;
            } else {
                if (linked.is_intrinsic && method.must_override) {
                    Fail(DexVmErrorReason::invalid_override,
                         "intrinsic override has no inherited virtual method: " +
                             linked.descriptor + "." + method.name +
                             method.descriptor);
                }
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
        return linked.own_virtual_methods;
    }
};

}  // namespace ogplay::runtime::dexvm
