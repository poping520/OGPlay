// Copyright (C) 2006 The Android Open Source Project
// SPDX-License-Identifier: Apache-2.0
// Theme and style resolution adapted from KitKat ResourceTypes.cpp and
// core/jni/android_util_AssetManager.cpp (android-4.4.4_r2.0.1).
#include "binary.h"
#include <algorithm>

namespace ogplay::runtime::resources {
using detail::require;

Theme::Theme(const ResTable& table) : table_(&table) {}

const ResTable& Theme::table() const {
    return *table_;
}

bool Theme::applyStyle(std::uint32_t id, bool force) {
    auto bag = table().getBagLocked(id);
    if (!bag) return false;
    for (const auto& e : bag->entries) {
        // AOSP skips invalid package/type/entry keys.
        if (!table().validBagKey(e.name)) continue;
        auto& old = attributes_[e.name];
        if (force || old.value.type == 0) {
            auto flags = old.changingConfigurations;
            old = e.value;
            old.changingConfigurations |= flags | bag->typeSpecFlags;
        }
    }
    return true;
}

void Theme::setTo(const Theme& other) {
    if (this == &other) return;
    attributes_.clear();
    for (const auto& [id, v] : other.attributes_)
        if (table_ == other.table_ || (id >> 24) == 1) attributes_.emplace(id, v);
}

std::optional<TypedValue> Theme::getAttribute(std::uint32_t id) const {
    std::uint32_t flags = 0;
    for (unsigned i = 0; i <= 20; ++i) {
        auto e = attributes_.find(id);
        if (e == attributes_.end()) return {};
        auto v = e->second;
        flags |= v.changingConfigurations;
        if (v.value.type == 2) {
            id = v.value.data;
            continue;
        }
        if (v.value.type == 0) return {};
        v.changingConfigurations = flags;
        return v;
    }
    return {};
}

TypedValue Theme::resolveAttributeReference(TypedValue v) const {
    if (v.value.type == 2) {
        auto attr = getAttribute(v.value.data);
        if (!attr) return v;
        attr->changingConfigurations |= v.changingConfigurations;
        v = *attr;
    }
    return table().resolveReference(v);
}

namespace {
std::optional<TypedValue> bagValue(const std::shared_ptr<const Bag>& bag, std::uint32_t attr, std::uint32_t flags = 0) {
    if (!bag) return {};
    auto e = std::lower_bound(bag->entries.begin(), bag->entries.end(), attr,
                              [](const BagEntry& e, std::uint32_t id) { return e.name < id; });
    if (e == bag->entries.end() || e->name != attr) return {};
    auto v = e->value;
    v.changingConfigurations = bag->typeSpecFlags | flags;
    v.density = 0;
    return v;
}

void append(StyleResult& out, TypedValue value, std::size_t index, bool nullCookie) {
    if (value.value.type == 1 && value.value.data == 0) {
        value.value.type = 0;
        if (nullCookie) value.cookie = -1;
    }
    auto slots = value.slots();
    out.values.insert(out.values.end(), slots.begin(), slots.end());
    if (value.value.type != 0) out.indices.push_back(static_cast<std::int32_t>(index));
}

std::optional<TypedValue> xmlValue(const XmlAttributes* xml, std::uint32_t id) {
    if (xml)
        for (std::size_t i = 0; i < xml->attributeCount(); ++i) {
            auto a = xml->attribute(i);
            if (a.resourceId == id) return TypedValue{a.value, -1, 0, 0, 0};
        }
    return {};
}

std::shared_ptr<const Bag> arrayBag(const ResTable& table, std::uint32_t id) {
    auto bag = table.getBagLocked(id);
    require(bag != nullptr, "resource is not an array/bag");
    return bag;
}
}  // namespace

StyleResult applyStyle(const Theme& theme, std::uint32_t defStyleAttr, std::uint32_t defStyleRes,
                       const XmlAttributes* xml, std::span<const std::uint32_t> attrs) {
    std::uint32_t defaultFlags = 0, styleFlags = 0, style = 0;
    if (defStyleAttr)
        if (auto v = theme.getAttribute(defStyleAttr)) {
            defaultFlags = v->changingConfigurations;
            if (v->value.type == 1) defStyleRes = v->value.data;
        }
    if (xml)
        if (auto index = xml->indexOfStyle()) {
            auto v = xml->attribute(*index).value;
            if (v.type == 2) {
                auto attr = theme.getAttribute(v.data);
                if (attr) {
                    v = attr->value;
                    styleFlags = attr->changingConfigurations;
                } else
                    v = {};
            }
            if (v.type == 1) style = v.data;
        }
    auto defaults = defStyleRes ? theme.table().getBagLocked(defStyleRes) : nullptr;
    auto styles = style ? theme.table().getBagLocked(style) : nullptr;
    StyleResult result;
    result.indices.push_back(0);
    std::int32_t lastCookie = 1;
    for (std::size_t i = 0; i < attrs.size(); ++i) {
        auto v = xmlValue(xml, attrs[i]);
        if (!v || v->value.type == 0) v = bagValue(styles, attrs[i], styleFlags);
        if (!v || v->value.type == 0) v = bagValue(defaults, attrs[i], defaultFlags);
        if (v && v->value.type != 0)
            *v = theme.resolveAttributeReference(*v);
        else {
            v = theme.getAttribute(attrs[i]);
            if (v) *v = theme.table().resolveReference(*v);
        }
        auto value = v.value_or(TypedValue{{}, lastCookie, 0, 0, 0});
        append(result, value, i, true);
        lastCookie = result.values[i * 6 + 2];
    }
    result.indices[0] = static_cast<std::int32_t>(result.indices.size() - 1);
    return result;
}

StyleResult retrieveAttributes(const ResTable& table, const XmlAttributes& xml, std::span<const std::uint32_t> attrs) {
    StyleResult result;
    result.indices.push_back(0);
    std::int32_t lastCookie = 1;
    for (std::size_t i = 0; i < attrs.size(); ++i) {
        auto v = xmlValue(&xml, attrs[i]);
        auto value = v ? table.resolveReference(*v) : TypedValue{{}, lastCookie, 0, 0, 0};
        append(result, value, i, false);
        lastCookie = result.values[i * 6 + 2];
    }
    result.indices[0] = static_cast<std::int32_t>(result.indices.size() - 1);
    return result;
}

std::optional<TypedValue> loadResourceValue(const ResTable& table, std::uint32_t id, std::uint16_t density,
                                            bool resolve) {
    auto v = table.getResource(id, density);
    if (v) {
        v->resourceId = id;
        if (resolve) *v = table.resolveReference(*v);
    }
    return v;
}

std::optional<TypedValue> loadThemeAttributeValue(const Theme& theme, std::uint32_t attr, bool resolve) {
    auto v = theme.getAttribute(attr);
    if (v && resolve) *v = theme.resolveAttributeReference(*v);
    return v;
}

std::optional<TypedValue> loadResourceBagValue(const ResTable& table, std::uint32_t id, std::uint32_t entry,
                                               bool resolve) {
    auto v = bagValue(table.getBagLocked(id), entry);
    if (v) {
        v->resourceId = id;
        if (resolve) *v = table.resolveReference(*v);
    }
    return v;
}

std::size_t getArraySize(const ResTable& table, std::uint32_t id) {
    return arrayBag(table, id)->entries.size();
}

std::vector<std::int32_t> retrieveArray(const ResTable& table, std::uint32_t id) {
    StyleResult result;
    for (const auto& e : arrayBag(table, id)->entries)
        append(result, table.resolveReference(e.value), 0, false);
    return result.values;
}

std::vector<std::array<std::int32_t, 2>> getArrayStringInfo(const ResTable& table, std::uint32_t id) {
    std::vector<std::array<std::int32_t, 2>> result;
    for (const auto& e : arrayBag(table, id)->entries) {
        auto v = table.resolveReference(e.value);
        result.push_back(
            {table.stringBlock(v.cookie), v.value.type == 3 ? static_cast<std::int32_t>(v.value.data) : -1});
    }
    return result;
}

std::vector<std::optional<std::u16string>> getArrayStringResource(const ResTable& table, std::uint32_t id) {
    std::vector<std::optional<std::u16string>> result;
    for (const auto& e : arrayBag(table, id)->entries) {
        auto v = table.resolveReference(e.value);
        if (v.value.type == 3)
            result.emplace_back(table.strings(v.cookie).string16At(v.value.data));
        else
            result.emplace_back(std::nullopt);
    }
    return result;
}

std::vector<std::int32_t> getArrayIntResource(const ResTable& table, std::uint32_t id) {
    std::vector<std::int32_t> result;
    for (const auto& e : arrayBag(table, id)->entries) {
        auto v = table.resolveReference(e.value);
        result.push_back(v.value.type >= 0x10 && v.value.type <= 0x1f ? static_cast<std::int32_t>(v.value.data) : 0);
    }
    return result;
}
}  // namespace ogplay::runtime::resources
