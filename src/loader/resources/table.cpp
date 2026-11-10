// Copyright (C) 2008 The Android Open Source Project
// SPDX-License-Identifier: Apache-2.0
// ResTable semantics from android-4.4.4_r2.0.1 libs/androidfw/ResourceTypes.cpp.
#include "binary.h"
#include "ogplay/core/text.h"
#include <algorithm>
#include <charconv>
#include <set>

namespace ogplay::loader::resources {
using namespace detail;

namespace {
void validateValue(Bytes b, std::size_t off, const ResStringPool& strings);

struct TypeChunk {
    Bytes bytes;
    ResTable_config config;
    std::shared_ptr<std::once_flag> validated = std::make_shared<std::once_flag>();

    std::optional<Bytes> entry(std::size_t index, const ResStringPool& keys, const ResStringPool& strings) const {
        std::call_once(*validated, [&] {
            const auto count = u32(bytes, 12), data = u32(bytes, 16);
            for (std::size_t i = 0; i < count; ++i) {
                const auto offset = u32(bytes, u16(bytes, 2) + i * 4);
                if (offset == NoIndex) continue;
                require(offset % 4 == 0 && offset <= bytes.size() - data, "invalid entry offset");
                const auto e = range(bytes, data + offset, bytes.size() - data - offset);
                const auto h = u16(e, 0);
                require(h >= 8, "invalid entry header");
                range(e, 0, h);
                stringIndex(keys, u32(e, 4));
                if ((u16(e, 2) & 1) != 0) {
                    require(h >= 16, "invalid complex entry");
                    const auto n = u32(e, 12);
                    range(e, h, static_cast<std::size_t>(n) * 12);
                    for (std::size_t j = 0; j < n; ++j)
                        validateValue(e, h + j * 12 + 4, strings);
                } else
                    validateValue(e, h, strings);
            }
        });
        if (index >= u32(bytes, 12)) return {};
        const auto offset = u32(bytes, u16(bytes, 2) + index * 4);
        if (offset == NoIndex) return {};
        const auto start = static_cast<std::size_t>(u32(bytes, 16)) + offset;
        return range(bytes, start, bytes.size() - start);
    }
};

struct Type {
    Bytes flags;
    std::uint32_t count{};
    bool hasSpec{};
    std::vector<TypeChunk> configs;
};

struct Package {
    std::uint32_t id;
    std::string name;
    ResStringPool types, keys;
    std::map<std::uint32_t, Type> entries;
};

std::uint32_t flagsFor(const Type& type, std::size_t index) {
    return type.flags.empty() ? NoIndex : u32(type.flags, index * 4);
}

bool complex(Bytes e) {
    return (u16(e, 2) & 1) != 0;
}

void validateValue(Bytes b, std::size_t off, const ResStringPool& strings) {
    auto v = value(b, off);
    if (v.type == 3) stringIndex(strings, v.data);
}
}  // namespace

struct ResTableData::Impl {
    Source source;
    ResStringPool strings;
    std::vector<Package> packages;

    Impl(Source s, Bytes pool) : source(std::move(s)), strings(pool, source.owner) {}
};

ResTableData::ResTableData(Source source) {
    require(source.owner != nullptr, "resource table requires a byte owner");
    auto root = chunk(source.bytes, 0);
    require(u16(root, 0) == 2 && u16(root, 2) >= 12 && root.size() == source.bytes.size(),
            "invalid resource table header");
    auto pos = static_cast<std::size_t>(u16(root, 2));
    auto global = chunk(root, pos);
    auto p = std::make_shared<Impl>(std::move(source), global);
    pos += global.size();
    for (; pos < root.size();) {
        auto pkg = chunk(root, pos);
        pos += pkg.size();
        require(u16(pkg, 0) == 0x200 && u16(pkg, 2) >= 284, "invalid resource package header");
        auto id = u32(pkg, 8);
        require(id > 0 && id < 256, "invalid resource package id");
        std::u16string name;
        for (std::size_t i = 12; i < 268; i += 2) {
            auto c = u16(pkg, i);
            if (!c) break;
            name.push_back(static_cast<char16_t>(c));
        }
        auto utf = core::Utf16ToUtf8(name, core::InvalidUtf16Policy::reject);
        require(utf.has_value() && !utf->empty(), "invalid package name");
        if (u16(pkg, 2) >= 288) require(u32(pkg, 284) == 0, "type ID offsets are outside API19");
        auto typeOffset = u32(pkg, 268), keyOffset = u32(pkg, 276);
        require(typeOffset >= u16(pkg, 2) && keyOffset >= u16(pkg, 2), "package string pool overlaps header");
        Package package{id,
                        *utf,
                        ResStringPool(chunk(pkg, typeOffset), p->source.owner),
                        ResStringPool(chunk(pkg, keyOffset), p->source.owner),
                        {}};
        for (std::size_t off = u16(pkg, 2); off < pkg.size();) {
            auto c = chunk(pkg, off);
            auto kind = u16(c, 0);
            off += c.size();
            if (kind == 1) continue;
            require(kind == 0x202 || kind == 0x201, "unsupported resource package chunk");
            require(u16(c, 2) >= 16, "invalid type header");
            auto tid = u8(c, 8);
            require(tid > 0 && tid <= package.types.size() && u8(c, 9) == 0 && u16(c, 10) == 0,
                    "invalid type id/flags");
            auto count = u32(c, 12);
            require(count <= 65536, "too many type entries");
            auto& t = package.entries[tid];
            if (kind == 0x202) {
                require(!t.hasSpec, "duplicate type spec");
                t.hasSpec = true;
                t.count = count;
                t.flags = range(c, u16(c, 2), static_cast<std::size_t>(count) * 4);
            } else {
                if (!t.hasSpec) t.count = std::max(t.count, count);
                require(u16(c, 2) >= 48, "invalid type configuration header");
                auto config = ResTable_config::decode(range(c, 20, u16(c, 2) - 20));
                auto data = u32(c, 16);
                require(data >= u16(c, 2) + static_cast<std::size_t>(count) * 4 && data <= c.size(),
                        "invalid type entry data start");
                range(c, u16(c, 2), static_cast<std::size_t>(count) * 4);
                TypeChunk tc{c, config};
                t.configs.push_back(std::move(tc));
            }
        }
        for (const auto& [tid, t] : package.entries) {
            (void)tid;
            for (const auto& c : t.configs)
                require(t.flags.empty() || u32(c.bytes, 12) <= t.count, "entry count exceeds type spec");
        }
        require(std::none_of(p->packages.begin(), p->packages.end(), [&](const Package& q) { return q.id == id; }),
                "duplicate package id");
        p->packages.push_back(std::move(package));
    }
    require(p->packages.size() == u32(root, 8), "resource package count mismatch");
    impl_ = std::move(p);
}

const ResStringPool& ResTableData::strings() const {
    return impl_->strings;
}

struct ResTable::Selected {
    const TypeChunk* chunk;
    Bytes entry;
    std::int32_t cookie;
    std::uint32_t flags;
};

void ResTable::add(std::shared_ptr<const ResTableData> data, std::int32_t cookie) {
    require(data && cookie > 0, "invalid table/cookie");
    std::scoped_lock lock(bagCache_->mutex);
    for (const auto& t : tables_)
        require(t.cookie != cookie, "duplicate table cookie");
    tables_.push_back({std::move(data), cookie});
    bagCache_->values.clear();
}

void ResTable::setParameters(const ResTable_config& parameters) {
    std::scoped_lock lock(bagCache_->mutex);
    parameters_ = parameters;
    bagCache_->values.clear();
}

const ResTable_config& ResTable::parameters() const {
    return parameters_;
}

std::optional<ResTable::Selected> ResTable::select(std::uint32_t id, std::uint16_t density) const {
    auto requested = parameters_;
    if (density) requested.density = density;
    std::optional<Selected> best;
    auto index = id & 0xffff;
    for (auto table = tables_.rbegin(); table != tables_.rend(); ++table)
        for (const auto& pkg : table->data->impl_->packages) {
            if (pkg.id != (id >> 24)) continue;
            auto it = pkg.entries.find((id >> 16) & 255);
            if (it == pkg.entries.end()) continue;
            const auto& type = it->second;
            for (const auto& tc : type.configs) {
                if (!tc.config.match(requested)) continue;
                auto e = tc.entry(index, pkg.keys, table->data->strings());
                if (!e) continue;
                if (!best || tc.config.isBetterThan(best->chunk->config, &requested))
                    best = Selected{&tc, *e, table->cookie, flagsFor(type, index)};
            }
        }
    return best;
}

std::optional<TypedValue> ResTable::getResource(std::uint32_t id, std::uint16_t density,
                                                std::uint32_t* outSpecFlags) const {
    if (outSpecFlags) *outSpecFlags = 0;
    auto s = select(id, density);
    if (!s || complex(s->entry)) return {};
    if (outSpecFlags) *outSpecFlags = s->flags;
    return TypedValue{value(s->entry, u16(s->entry, 0)), s->cookie, 0, s->flags, s->chunk->config.density};
}

TypedValue ResTable::resolveReference(TypedValue v) const {
    for (unsigned i = 0; i < 20 && v.value.type == 1 && v.value.data; ++i) {
        v.resourceId = v.value.data;
        auto next = getResource(v.value.data);
        if (!next) {
            return v;  // Failed lookups, including bags, leave the reference in place.
        }
        next->resourceId = v.resourceId;
        next->changingConfigurations |= v.changingConfigurations;
        v = *next;
    }
    return v;
}

std::shared_ptr<const Bag> ResTable::getBagLocked(std::uint32_t id) const {
    std::scoped_lock lock(bagCache_->mutex);
    std::vector<std::uint32_t> parents;
    return bag(id, parents);
}

std::shared_ptr<const Bag> ResTable::bag(std::uint32_t id, std::vector<std::uint32_t>& parents) const {
    if (auto found = bagCache_->values.find(id); found != bagCache_->values.end()) return found->second;
    require(parents.size() < 256 && std::find(parents.begin(), parents.end(), id) == parents.end(),
            "cyclic resource bag parent");
    auto s = select(id);
    if (!s || !complex(s->entry)) return {};
    parents.push_back(id);
    Bag result;
    auto parent = u32(s->entry, 8);
    if (parent) {
        auto inherited = bag(parent, parents);
        if (inherited) result = *inherited;
    }
    result.typeSpecFlags |= s->flags;
    std::map<std::uint32_t, TypedValue> merged;
    for (const auto& e : result.entries)
        merged.emplace(e.name, e.value);
    auto h = u16(s->entry, 0);
    for (std::size_t i = 0; i < u32(s->entry, 12); ++i) {
        auto off = h + i * 12;
        merged[u32(s->entry, off)] = {value(s->entry, off + 4), s->cookie, 0, 0, 0};
    }
    result.entries.clear();
    result.entries.reserve(merged.size());
    for (auto& [name, v] : merged) {
        v.changingConfigurations = result.typeSpecFlags;
        result.entries.push_back({name, v});
    }
    parents.pop_back();
    auto mergedBag = std::make_shared<const Bag>(std::move(result));
    bagCache_->values.emplace(id, mergedBag);
    return mergedBag;
}

std::vector<std::string> ResTable::packageNames() const {
    std::vector<std::string> names;
    for (const auto& table : tables_)
        for (const auto& package : table.data->impl_->packages)
            names.push_back(package.name);
    return names;
}

std::string ResourceName::fullName() const {
    return package + ":" + type + "/" + name;
}

bool ResTable::validBagKey(std::uint32_t id) const {
    for (const auto& table : tables_)
        for (const auto& pkg : table.data->impl_->packages) {
            if (pkg.id != (id >> 24)) continue;
            const auto type = pkg.entries.find((id >> 16) & 255);
            if (type != pkg.entries.end() && (id & 0xffff) < type->second.count) return true;
        }
    return false;
}

std::optional<ResourceName> ResTable::getResourceName(std::uint32_t id) const {
    for (const auto& table : tables_)
        for (const auto& pkg : table.data->impl_->packages) {
            if (pkg.id != (id >> 24)) continue;
            auto tid = (id >> 16) & 255;
            auto t = pkg.entries.find(tid);
            if (t == pkg.entries.end()) continue;
            for (const auto& tc : t->second.configs)
                if (auto e = tc.entry(id & 0xffff, pkg.keys, table.data->strings()))
                    return ResourceName{pkg.name, pkg.types.stringAt(tid - 1), pkg.keys.stringAt(u32(*e, 4))};
        }
    return {};
}

std::uint32_t ResTable::identifierForName(std::string_view name, std::string_view type, std::string_view package,
                                          std::uint32_t* outSpecFlags) const {
    if (outSpecFlags) *outSpecFlags = 0;
    if (name.empty()) return 0;
    constexpr std::array<std::string_view, 10> internals = {"^type", "^min", "^max", "^l10n", "^other",
                                                            "^zero", "^one", "^two", "^few",  "^many"};
    for (std::size_t i = 0; i < internals.size(); ++i)
        if (name == internals[i]) {
            if (outSpecFlags) *outSpecFlags = 0x40000000;
            return 0x01000000U + static_cast<std::uint32_t>(i);
        }
    if (name.starts_with("^index_")) {
        std::uint32_t n{};
        auto tail = name.substr(7);
        auto parsed = std::from_chars(tail.data(), tail.data() + tail.size(), n);
        if (parsed.ec != std::errc{} || parsed.ptr != tail.data() + tail.size() || n > 65535) return 0;
        if (outSpecFlags) *outSpecFlags = 0x40000000;
        return 0x02000000 | n;
    }
    bool fakePublic = false;
    if (name.front() == '@') {
        name.remove_prefix(1);
        if (name.starts_with('*')) {
            fakePublic = true;
            name.remove_prefix(1);
        }
    }
    if (auto colon = name.rfind(':'); colon != std::string_view::npos) {
        package = name.substr(0, colon);
        name.remove_prefix(colon + 1);
    }
    if (auto slash = name.rfind('/'); slash != std::string_view::npos) {
        type = name.substr(0, slash);
        name.remove_prefix(slash + 1);
    }
    if (package.empty() || type.empty() || name.empty()) return 0;
    const auto lookup = [&](std::string_view wantedType) -> std::uint32_t {
        for (const auto& table : tables_)
            for (const auto& pkg : table.data->impl_->packages) {
                if (pkg.name != package) continue;
                auto tidx = pkg.types.indexOfString(wantedType);
                if (!tidx) continue;
                auto key = pkg.keys.indexOfString(name);
                if (!key) continue;
                auto it = pkg.entries.find(static_cast<std::uint32_t>(*tidx + 1));
                if (it == pkg.entries.end()) continue;
                for (const auto& tc : it->second.configs)
                    for (std::size_t i = 0; i < u32(tc.bytes, 12); ++i)
                        if (auto e = tc.entry(i, pkg.keys, table.data->strings()); e && u32(*e, 4) == *key) {
                            if (outSpecFlags) *outSpecFlags = flagsFor(it->second, i) | (fakePublic ? 0x40000000 : 0);
                            return (pkg.id << 24) | (static_cast<std::uint32_t>(*tidx + 1) << 16) |
                                   static_cast<std::uint32_t>(i);
                        }
            }
        return 0;
    };
    return lookup(type);
}

std::vector<std::string> ResTable::getLocales() const {
    std::set<std::string> seen;
    std::vector<std::string> locales;
    for (const auto& table : tables_)
        for (const auto& pkg : table.data->impl_->packages)
            for (const auto& [id, type] : pkg.entries) {
                (void)id;
                for (const auto& config : type.configs) {
                    auto locale = config.config.localeName();
                    if (seen.insert(locale).second) locales.push_back(std::move(locale));
                }
            }
    return locales;
}

const ResStringPool& ResTable::strings(std::int32_t cookie) const {
    for (const auto& t : tables_)
        if (t.cookie == cookie) return t.data->strings();
    throw ResourceError("unknown string pool cookie");
}

void ResTable::forEachVariant(const std::function<void(const ResourceVariant&)>& visitor) const {
    for (const auto& table : tables_)
        for (const auto& pkg : table.data->impl_->packages)
            for (const auto& [tid, t] : pkg.entries)
                for (const auto& tc : t.configs)
                    for (std::size_t i = 0; i < u32(tc.bytes, 12); ++i)
                        if (auto e = tc.entry(i, pkg.keys, table.data->strings())) {
                            ResourceVariant v;
                            v.id = (pkg.id << 24) | (tid << 16) | static_cast<std::uint32_t>(i);
                            v.config = tc.config;
                            v.complex = complex(*e);
                            auto h = u16(*e, 0);
                            if (v.complex) {
                                v.parent = u32(*e, 8);
                                for (std::size_t j = 0; j < u32(*e, 12); ++j)
                                    v.bag.emplace_back(u32(*e, h + j * 12), value(*e, h + j * 12 + 4));
                            } else
                                v.value = value(*e, h);
                            visitor(v);
                        }
}
}  // namespace ogplay::loader::resources

namespace ogplay::loader::resources {
std::size_t ResTable::tableCount() const {
    return tables_.size();
}

std::int32_t ResTable::stringBlock(std::int32_t cookie) const {
    for (std::size_t i = 0; i < tables_.size(); ++i)
        if (tables_[i].cookie == cookie) return static_cast<std::int32_t>(i);
    throw ResourceError("unknown string pool cookie");
}

std::int32_t ResTable::cookieForBlock(std::size_t block) const {
    require(block < tables_.size(), "string block outside table");
    return tables_[block].cookie;
}
}  // namespace ogplay::loader::resources
