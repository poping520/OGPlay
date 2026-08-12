#pragma once

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "ogplay/runtime/dexvm/class_linker.h"

// Private glue shared by class_linker.cpp (registration and linking) and
// class_linker_resolve.cpp (constant-pool resolution and assignability).
// Not part of the public linker surface.

namespace ogplay::runtime::dexvm {

constexpr std::uint32_t kAccStatic = 0x0008;
constexpr std::uint32_t kAccFinal = 0x0010;
constexpr std::uint32_t kAccInterface = 0x0200;
constexpr std::uint32_t kAccNative = 0x0100;
constexpr std::uint32_t kAccAbstract = 0x0400;

[[noreturn]] void Fail(DexVmErrorReason reason, std::string message);
// Member and type names in the supported titles are ASCII; anything else
// is preserved via UTF-16 escape formatting.
[[nodiscard]] std::string Ascii(const loader::DexString& value);
[[nodiscard]] bool IsPlatformDescriptor(std::string_view descriptor);
[[nodiscard]] bool IsWideDescriptor(std::string_view descriptor);
[[nodiscard]] bool IsRefDescriptor(std::string_view descriptor);
[[nodiscard]] char ShortyOf(std::string_view descriptor);

struct DescriptorParts final {
    std::vector<std::string> parameters;
    std::string return_type;
};

[[nodiscard]] DescriptorParts SplitDescriptor(
    const std::string& descriptor);
[[nodiscard]] std::uint16_t ArgumentWords(const DescriptorParts& parts,
                                          bool is_static);
[[nodiscard]] std::string MemberKey(const std::string& name,
                                    const std::string& descriptor);

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

}  // namespace ogplay::runtime::dexvm
