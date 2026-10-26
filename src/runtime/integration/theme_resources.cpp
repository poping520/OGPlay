#include "ogplay/runtime/integration/theme_resources.h"

#include <algorithm>
#include <atomic>
#include <limits>
#include <set>
#include <stdexcept>

#include "ogplay/core/json.h"

namespace ogplay::runtime {
namespace {
using core::JsonValue;

JsonValue Member(JsonValue object, std::string_view name) {
    const auto value = object.Member(name);
    if (!value) throw std::runtime_error("theme projection missing member: " + std::string(name));
    return *value;
}

std::uint32_t Number(JsonValue value) {
    const auto number = value.UnsignedInteger();
    if (!number || *number > UINT32_MAX) throw std::runtime_error("invalid theme projection integer");
    return static_cast<std::uint32_t>(*number);
}

std::string Text(JsonValue value) {
    if (!value.String()) throw std::runtime_error("invalid theme projection string");
    return std::string(*value.String());
}

ThemeValue XmlValue(const loader::CompiledXmlAttribute& attr) {
    ThemeValue result;
    result.type = attr.value.type;
    result.data = attr.value.data;
    result.cookie = -1;
    return result;
}

void Supported(const ThemeValue& value) {
    if (!value.unsupported.empty()) throw std::runtime_error(value.unsupported);
    if (value.type > 31 || (value.type > 6 && value.type < 16))
        throw std::runtime_error("unsupported resource value type");
}
}  // namespace

std::array<std::uint32_t, 6> ThemeValue::Slots() const {
    return {type, data, static_cast<std::uint32_t>(cookie), resource_id, flags, density};
}

const ThemeResources::RomColor* ThemeResources::Color(std::uint32_t id) const {
    const auto found = rom_colors_.find(id);
    return found == rom_colors_.end() ? nullptr : &found->second;
}

void ThemeResources::Load(std::string_view projection) {
    if (loaded_ || stopped_) throw std::logic_error("theme projection cannot be replaced");
    core::JsonParseError error;
    const auto json = core::JsonDocument::ParseStrict(projection, error, 4U * 1024U * 1024U, 16);
    if (!json) throw std::runtime_error("invalid theme projection: " + error.message);
    const auto root = json->Root();
    if (Text(Member(root, "schema")) != "ogplay.bounded-theme" || Number(Member(root, "schema_version")) != 1 ||
        Number(Member(root, "api_level")) != 19 || Text(Member(root, "configuration")) != "values")
        throw std::runtime_error("unsupported theme projection schema/configuration");
    std::vector<std::string> strings;
    if (const auto pool = root.Member("strings")) {
        if (!pool->IsArray() || pool->Size() > 4096) throw std::runtime_error("invalid ROM string pool");
        for (std::size_t i = 0; i < pool->Size(); ++i)
            strings.push_back(Text(*pool->Element(i)));
    }
    std::map<std::uint32_t, RomColor> colors;
    if (const auto rows = root.Member("rom_colors")) {
        if (!rows->IsArray() || rows->Size() > 1024) throw std::runtime_error("invalid ROM color bounds");
        for (std::size_t i = 0; i < rows->Size(); ++i) {
            const auto row = *rows->Element(i);
            const auto id = Number(Member(row, "id"));
            RomColor color;
            if (const auto unsupported = row.Member("unsupported"))
                color.value.unsupported = Text(*unsupported);
            else {
                color.value.type = Number(Member(row, "type"));
                color.value.data = Number(Member(row, "data"));
                color.value.resource_id = id;
                if (const auto xml = row.Member("xml")) {
                    const auto hex = Text(*xml);
                    if (hex.size() % 2 || hex.size() > 131072) throw std::runtime_error("invalid ROM color XML bounds");
                    const auto digit = [](char c) -> unsigned {
                        if (c >= '0' && c <= '9') return static_cast<unsigned>(c - '0');
                        if (c >= 'a' && c <= 'f') return static_cast<unsigned>(c - 'a' + 10);
                        throw std::runtime_error("invalid ROM color XML hex");
                    };
                    for (std::size_t n = 0; n < hex.size(); n += 2)
                        color.xml.push_back(static_cast<std::byte>((digit(hex[n]) << 4) | digit(hex[n + 1])));
                }
                if ((!color.xml.empty() && (color.value.type != 1 || color.value.data != id)) ||
                    (color.xml.empty() && (color.value.type < 28 || color.value.type > 31)))
                    throw std::runtime_error("invalid ROM color value");
                if (!color.xml.empty()) {
                    color.value.type = 3;
                    color.value.cookie = 2;
                    color.value.data = Number(Member(row, "string_index"));
                    if (color.value.data >= strings.size() || strings[color.value.data] != Text(Member(row, "path")))
                        throw std::runtime_error("invalid ROM color path string");
                }
            }
            if ((id >> 16) != 0x0106 || !colors.emplace(id, std::move(color)).second)
                throw std::runtime_error("invalid/duplicate ROM color ID");
        }
    }
    std::vector<RomDrawable> skins;
    if (const auto rows = root.Member("rom_drawables")) {
        if (!rows->IsArray() || rows->Size() > 64) throw std::runtime_error("invalid ROM skin bounds");
        for (std::size_t i = 0; i < rows->Size(); ++i) {
            const auto row = *rows->Element(i);
            const auto id = Number(Member(row, "id"));
            if ((id >> 16) != 0x0108) throw std::runtime_error("invalid ROM skin ID");
            const auto variants = Member(row, "variants");
            if (!variants.IsArray() || variants.Size() > 16) throw std::runtime_error("invalid skin variants");
            for (std::size_t j = 0; j < variants.Size(); ++j) {
                const auto variant = *variants.Element(j);
                RomDrawable skin;
                skin.id = id;
                skin.density = Number(Member(variant, "density"));
                const auto kind = Text(Member(variant, "kind"));
                if (kind == "selector") {
                    const auto items = Member(variant, "items");
                    if (!items.IsArray() || items.Size() > 64) throw std::runtime_error("invalid selector items");
                    for (std::size_t k = 0; k < items.Size(); ++k) {
                        const auto item = *items.Element(k);
                        RomDrawable::Item entry;
                        entry.drawable = Number(Member(item, "drawable"));
                        const auto states = Member(item, "states");
                        if (!states.IsArray() || states.Size() > 32)
                            throw std::runtime_error("invalid selector states");
                        for (std::size_t n = 0; n < states.Size(); ++n) {
                            const auto state = states.Element(n)->Integer();
                            if (!state || *state < INT32_MIN || *state > INT32_MAX)
                                throw std::runtime_error("invalid state ID");
                            entry.states.push_back(static_cast<std::int32_t>(*state));
                        }
                        skin.items.push_back(std::move(entry));
                    }
                } else if (kind == "ninepatch") {
                    skin.width = Number(Member(variant, "width"));
                    skin.height = Number(Member(variant, "height"));
                    for (const auto name : {"padding", "optical"}) {
                        const auto values = Member(variant, name);
                        if (!values.IsArray() || values.Size() != 4) throw std::runtime_error("invalid skin insets");
                        auto& target = std::string_view(name) == "padding" ? skin.padding : skin.optical;
                        for (std::size_t n = 0; n < 4; ++n) {
                            const auto value = Number(*values.Element(n));
                            if (value > INT32_MAX) throw std::runtime_error("invalid skin inset");
                            target[n] = static_cast<std::int32_t>(value);
                        }
                    }
                    for (const auto name : {"stretch_x", "stretch_y"}) {
                        const auto divisions = Member(variant, name);
                        if (!divisions.IsArray() || divisions.Size() != 2)
                            throw std::runtime_error("unsupported ROM ninepatch divisions");
                        auto& target = std::string_view(name) == "stretch_x" ? skin.stretch_x : skin.stretch_y;
                        const auto extent = std::string_view(name) == "stretch_x" ? skin.width : skin.height;
                        for (std::size_t n = 0; n < 2; ++n) {
                            const auto v = Number(*divisions.Element(n));
                            if (v > extent || v > INT32_MAX) throw std::runtime_error("invalid ninepatch division");
                            target[n] = static_cast<std::int32_t>(v);
                        }
                        if (target[0] >= target[1]) throw std::runtime_error("empty ninepatch stretch");
                    }
                    const auto hex = Text(Member(variant, "png"));
                    if (hex.size() % 2 || hex.size() > 1048576) throw std::runtime_error("invalid skin PNG bounds");
                    const auto digit = [](char c) -> unsigned {
                        if (c >= '0' && c <= '9') return static_cast<unsigned>(c - '0');
                        if (c >= 'a' && c <= 'f') return static_cast<unsigned>(c - 'a' + 10);
                        throw std::runtime_error("invalid PNG hex");
                    };
                    for (std::size_t n = 0; n < hex.size(); n += 2)
                        skin.png.push_back(static_cast<std::byte>((digit(hex[n]) << 4) | digit(hex[n + 1])));
                } else
                    throw std::runtime_error("unsupported ROM drawable kind");
                if (std::any_of(skins.begin(), skins.end(),
                                [&](const auto& other) { return other.id == id && other.density == skin.density; }))
                    throw std::runtime_error("duplicate ROM drawable variant");
                skins.push_back(std::move(skin));
            }
        }
        for (const auto& skin : skins)
            for (const auto& item : skin.items)
                if (std::none_of(skins.begin(), skins.end(),
                                 [&](const auto& leaf) { return leaf.id == item.drawable && !leaf.png.empty(); }))
                    throw std::runtime_error("unclosed ROM selector");
    }
    std::set<std::uint32_t> reference_ids;
    if (const auto references = root.Member("projected_references")) {
        if (!references->IsArray() || references->Size() > 1024)
            throw std::runtime_error("invalid projected reference bounds");
        for (std::size_t i = 0; i < references->Size(); ++i) {
            const auto row = *references->Element(i);
            const auto id = Number(Member(row, "resource_id"));
            if ((id >> 16U) != 0x0108U || Text(Member(row, "resource_type")) != "drawable" ||
                Text(Member(row, "name")).empty() || !reference_ids.insert(id).second)
                throw std::runtime_error("invalid projected drawable reference");
        }
    }
    std::set<std::uint32_t> drawables;
    const auto styleables = Member(root, "styleables");
    if (!styleables.IsArray() || styleables.Size() > 1024) throw std::runtime_error("invalid styleable bounds");
    for (std::size_t i = 0; i < styleables.Size(); ++i) {
        const auto row = *styleables.Element(i);
        const auto name = Text(Member(row, "name"));
        if (name == "background" || name == "foreground" || name == "divider" || name.starts_with("scrollbarThumb") ||
            name.starts_with("scrollbarTrack"))
            drawables.insert(Number(Member(row, "resource_id")));
    }
    const auto registered = Member(root, "registered");
    const auto unregistered = Member(root, "unregistered");
    if (!registered.IsArray() || registered.Size() > 512 || !unregistered.IsArray() || unregistered.Size() > 512)
        throw std::runtime_error("invalid theme projection bounds");
    decltype(projection_) projected;
    decltype(registered_names_) names;
    decltype(unregistered_) rejected;
    const auto closure = Member(root, "style_closure");
    if (!closure.IsArray() || closure.Size() > 1024) throw std::runtime_error("invalid style closure bounds");
    for (std::size_t i = 0; i < registered.Size() + closure.Size(); ++i) {
        const auto row = i < registered.Size() ? *registered.Element(i) : *closure.Element(i - registered.Size());
        const auto id = Number(Member(row, "id"));
        if ((id >> 24U) != 1 || projected.contains(id)) throw std::runtime_error("invalid/duplicate theme ID");
        if (i < registered.Size()) names.emplace(id, Text(Member(row, "name")));
        const auto attrs = Member(row, "attributes");
        if (!attrs.IsArray() || attrs.Size() > 1024) throw std::runtime_error("invalid theme attributes");
        Bag bag;
        for (std::size_t j = 0; j < attrs.Size(); ++j) {
            const auto attr = *attrs.Element(j);
            const auto attr_id = Number(Member(attr, "id"));
            ThemeValue value;
            const auto state = Text(Member(attr, "state"));
            if (state == "known") {
                value.type = Number(Member(attr, "type"));
                value.data = Number(Member(attr, "data"));
                if (const auto cookie = attr.Member("cookie"))
                    value.cookie = static_cast<std::int32_t>(Number(*cookie));
                if (value.type == 3 && (value.cookie != 2 || value.data >= strings.size()))
                    throw std::runtime_error("invalid projected string reference");
                if (value.type > 31) throw std::runtime_error("invalid projected value type");
            } else if (state == "unsupported") {
                value.unsupported = Text(Member(attr, "reason"));
            } else if (state != "absent") {
                throw std::runtime_error("invalid projected value state");
            }
            if ((attr_id >> 24U) != 1 || !bag.emplace(attr_id, value).second)
                throw std::runtime_error("invalid/duplicate projected attr ID");
        }
        projected.emplace(id, std::move(bag));
    }
    for (std::size_t i = 0; i < unregistered.Size(); ++i) {
        const auto row = *unregistered.Element(i);
        if (!rejected.emplace(Text(Member(row, "name")), Text(Member(row, "reason"))).second)
            throw std::runtime_error("duplicate unregistered theme");
    }
    std::vector<std::byte> string_pool;
    if (!strings.empty()) {
        const auto hex = Text(Member(root, "string_pool"));
        if (hex.size() % 2 || hex.size() > 1048576) throw std::runtime_error("invalid ROM string pool bounds");
        const auto digit = [](char c) -> unsigned {
            if (c >= '0' && c <= '9') return static_cast<unsigned>(c - '0');
            if (c >= 'a' && c <= 'f') return static_cast<unsigned>(c - 'a' + 10);
            throw std::runtime_error("invalid string pool hex");
        };
        for (std::size_t n = 0; n < hex.size(); n += 2)
            string_pool.push_back(static_cast<std::byte>((digit(hex[n]) << 4) | digit(hex[n + 1])));
    }
    string_pool_ = std::move(string_pool);
    rom_colors_ = std::move(colors);
    rom_drawables_ = std::move(skins);
    drawable_attributes_ = std::move(drawables);
    projected_reference_ids_ = std::move(reference_ids);
    projection_ = std::move(projected);
    registered_names_ = std::move(names);
    unregistered_ = std::move(rejected);
    loaded_ = true;
}

const ThemeResources::RomDrawable* ThemeResources::Drawable(std::uint32_t id, std::uint32_t density) const {
    const RomDrawable* best = nullptr;
    for (const auto& row : rom_drawables_) {
        if (row.id != id) continue;
        if (!best) {
            best = &row;
            continue;
        }
        int high = row.density ? static_cast<int>(row.density) : 160;
        int low = best->density ? static_cast<int>(best->density) : 160;
        bool candidate_high = true;
        if (low > high) {
            std::swap(low, high);
            candidate_high = false;
        }
        const int requested = density ? static_cast<int>(density) : 160;
        const bool prefer_high =
            requested >= high || (requested > low && ((2 * low - requested) * high <= requested * requested));
        if (high != low && prefer_high == candidate_high) best = &row;
    }
    return best;
}

void ThemeResources::SetResourceHooks(ResourceSelector select, FileValidator validate) {
    select_ = std::move(select);
    validate_file_ = std::move(validate);
}

const loader::ArscEntry* ThemeResources::Entry(std::uint32_t id, const loader::ArscTable& table) const {
    return select_ ? select_(id) : table.FindById(id);
}

void ThemeResources::ValidateFile(const ThemeValue& value, const loader::ArscTable& table) const {
    const auto* entry = value.resource_id ? Entry(value.resource_id, table) : nullptr;
    if (!entry || !entry->string_value || !entry->string_value->starts_with("res/")) return;
    const auto& path = *entry->string_value;
    if (entry->type_name != "drawable" || path.ends_with(".9.png") ||
        !(path.ends_with(".png") || path.ends_with(".jpg") || path.ends_with(".jpeg")))
        throw std::runtime_error("unsupported selector/ninepatch/XML/unknown resource file: " + path);
    if (!validate_file_) throw std::runtime_error("APK bitmap file validator unavailable");
    validate_file_(*entry);
}

ThemeToken ThemeResources::New() {
    if (!loaded_ || stopped_) throw std::logic_error("theme provider is unavailable");
    static std::atomic<std::uint64_t> next{1};
    auto current = next.load();
    do {
        if (current > UINT32_MAX) throw std::overflow_error("theme token space exhausted");
    } while (!next.compare_exchange_weak(current, current + 1));
    const ThemeToken token{static_cast<std::uint32_t>(current)};
    themes_.emplace(token, State{});
    return token;
}

const ThemeResources::State& ThemeResources::Require(ThemeToken token) const {
    const auto it = themes_.find(token);
    if (it == themes_.end()) throw std::invalid_argument("theme token is not live in this context");
    return it->second;
}

bool ThemeResources::Has(ThemeToken token) const {
    return themes_.contains(token);
}

void ThemeResources::Delete(ThemeToken token) {
    static_cast<void>(Require(token));
    themes_.erase(token);
}

void ThemeResources::Copy(ThemeToken destination, ThemeToken source) {
    static_cast<void>(Require(destination));
    themes_.at(destination) = Require(source);
}

std::uint32_t ThemeResources::ThemeId(std::string_view theme) const {
    RequireRegistered(theme);
    for (const auto& [id, name] : registered_names_)
        if (name == theme) return id;
    throw std::logic_error("registered theme ID missing");
}

void ThemeResources::RequireRegistered(std::string_view theme) const {
    for (const auto& [id, name] : registered_names_)
        if (name == theme) return;
    const auto it = unregistered_.find(theme);
    throw std::runtime_error("unregistered theme " + std::string(theme) + ": " +
                             (it == unregistered_.end() ? "unknown theme" : it->second));
}

ThemeResources::Bag ThemeResources::Style(std::uint32_t style, const loader::ArscTable& table,
                                          std::vector<std::uint32_t> seen) const {
    if (seen.size() >= 64 || std::find(seen.begin(), seen.end(), style) != seen.end())
        throw std::runtime_error("style parent/reference cycle or depth limit");
    seen.push_back(style);
    if ((style >> 24U) == 1) {
        const auto found = projection_.find(style);
        if (found == projection_.end())
            throw std::runtime_error("unregistered framework theme ID " + std::to_string(style));
        auto result = found->second;
        std::erase_if(result, [](const auto& row) { return row.second.type == 0 && row.second.unsupported.empty(); });
        return result;
    }
    const auto* entry = Entry(style, table);
    if (!entry) throw std::runtime_error("unknown APK style " + std::to_string(style));
    if (!entry->is_complex) {
        if (entry->value_type == 1 && entry->value_data) return Style(entry->value_data, table, std::move(seen));
        throw std::runtime_error("resource is not a style bag");
    }
    if (entry->type_name != "style") throw std::runtime_error("non-style bag unsupported");
    auto result = entry->parent ? Style(entry->parent, table, std::move(seen)) : Bag{};
    for (const auto& item : entry->bag) {
        result[item.name] = {
            item.value_type, item.value_data, 1, 0, entry->changing_configurations, entry->density, {}};
    }
    std::uint32_t flags = entry->changing_configurations;
    for (const auto& [id, value] : result)
        flags |= value.flags;
    for (auto& [id, value] : result)
        value.flags |= flags;
    return result;
}

void ThemeResources::Apply(ThemeToken token, std::uint32_t style, bool force, const loader::ArscTable& table) {
    const auto& current = Require(token);
    const auto bag = Style(style, table);
    auto result = current;
    for (const auto& [id, value] : bag) {
        const auto old = result.values.find(id);
        if (force || old == result.values.end() || (old->second.type == 0 && old->second.unsupported.empty())) {
            auto replacement = value;
            if (old != result.values.end()) replacement.flags |= old->second.flags;
            result.values[id] = std::move(replacement);
        }
    }
    if ((style >> 24U) == 1 || (Entry(style, table) && Entry(style, table)->parent)) {
        // Projected coverage is established only by actually merging a system
        // bag. APK-only themes still permit their own absent attributes.
        for (const auto& [id, value] : bag) {
            if ((id >> 24U) == 1 && !projection_.empty() && projection_.begin()->second.contains(id)) {
                result.projected = true;
                break;
            }
        }
    }
    themes_.at(token) = std::move(result);
}

std::optional<ThemeValue> ThemeResources::Lookup(const State& theme, std::uint32_t attr) const {
    if ((attr >> 24U) == 1 && !projection_.empty() && !projection_.begin()->second.contains(attr))
        throw std::runtime_error("unregistered framework theme attribute " + std::to_string(attr));
    const auto found = theme.values.find(attr);
    if (found != theme.values.end()) {
        Supported(found->second);
        if (found->second.type == 0) return {};
        return found->second;
    }
    if (theme.projected && (attr >> 24U) == 1 && !projection_.begin()->second.contains(attr))
        throw std::runtime_error("unregistered framework theme attribute " + std::to_string(attr));
    return {};
}

ThemeValue ThemeResources::Resolve(ThemeValue value, const State* theme, bool resources,
                                   const loader::ArscTable& table) const {
    std::set<std::pair<std::uint32_t, std::uint32_t>> seen;
    for (unsigned depth = 0; depth < 64; ++depth) {
        Supported(value);
        if (value.type == 2 && theme) {
            if (!seen.emplace(2, value.data).second) throw std::runtime_error("theme attribute cycle");
            const auto attr = Lookup(*theme, value.data);
            if (!attr) throw std::runtime_error("unresolved theme attribute");
            const auto flags = value.flags;
            value = *attr;
            value.flags |= flags;
            continue;
        }
        if (value.type != 1 || !value.data || !resources) {
            if (value.type == 3 && value.cookie != 2) ValidateFile(value, table);
            return value;
        }
        if (!seen.emplace(1, value.data).second) throw std::runtime_error("resource reference cycle");
        const auto id = value.data;
        if ((id >> 24U) == 1) {
            if (!projection_.contains(id) && !projected_reference_ids_.contains(id) && !Drawable(id, 160) && !Color(id))
                throw std::runtime_error("unsupported projected resource reference " + std::to_string(id));
            if (const auto color = Color(id)) {
                Supported(color->value);
                return color->value;
            }
            // Styles and reviewed View drawable IDs are terminal, lazy references.
            // No system drawable is loaded or rasterized by this provider.
            value.resource_id = id;
            value.cookie = 1;
            return value;
        }
        const auto* entry = Entry(id, table);
        if (!entry) throw std::runtime_error("unknown resource reference");
        if (entry->is_complex) {
            if (entry->type_name != "style") throw std::runtime_error("cannot resolve non-style bag as a scalar");
            value.resource_id = id;
            value.cookie = 1;
            value.flags |= entry->changing_configurations;
            value.density = entry->density;
            return value;
        }
        if (entry->type_name == "anim" || entry->type_name == "animation")
            throw std::runtime_error("unsupported animation value");
        value.type = entry->value_type;
        value.data = entry->value_data;
        value.cookie = 1;
        value.resource_id = id;
        value.flags |= entry->changing_configurations;
        value.density = entry->density;
    }
    throw std::runtime_error("resource/attribute reference depth limit");
}

std::optional<ThemeValue> ThemeResources::Attribute(ThemeToken token, std::uint32_t attribute, bool resolve,
                                                    const loader::ArscTable& table) const {
    const auto& theme = Require(token);
    const auto value = Lookup(theme, attribute);
    return value ? std::optional{Resolve(*value, &theme, resolve, table)} : std::nullopt;
}

ThemeValue ThemeResources::Resource(std::uint32_t resource, bool resolve, const loader::ArscTable& table) const {
    if ((resource >> 24U) == 1 && (Drawable(resource, 160) || Color(resource)))
        return Resolve(ThemeValue{1, resource, 1, resource, 0, 0, {}}, nullptr, resolve, table);
    const auto* entry = Entry(resource, table);
    if ((resource >> 24U) == 1 || !entry || entry->is_complex)
        throw std::runtime_error("unknown or non-scalar resource");
    ThemeValue value{
        entry->value_type, entry->value_data, 1, resource, entry->changing_configurations, entry->density, {}};
    if (entry->type_name == "anim" || entry->type_name == "animation")
        throw std::runtime_error("unsupported animation resource");
    return Resolve(value, nullptr, resolve, table);
}

std::optional<ThemeValue> ThemeResources::BagValue(std::uint32_t style, std::uint32_t attribute, bool resolve,
                                                   const loader::ArscTable& table) const {
    const auto bag = Style(style, table);
    const auto it = bag.find(attribute);
    return it == bag.end() ? std::nullopt : std::optional{Resolve(it->second, nullptr, resolve, table)};
}

std::vector<ThemeValue> ThemeResources::Styled(ThemeToken token, std::uint32_t def_attr, std::uint32_t def_res,
                                               const loader::CompiledXmlNode* xml, std::span<const std::uint32_t> attrs,
                                               const loader::ArscTable& table) const {
    const auto& theme = Require(token);
    std::uint32_t default_flags{};
    if (def_attr) {
        const auto value = Attribute(token, def_attr, false, table);
        if (value) default_flags = value->flags;
        if (value && value->type == 1) def_res = value->data;
    }
    std::uint32_t style{}, style_flags{};
    if (xml && xml->style_index) {
        auto value = XmlValue(xml->attributes.at(xml->style_index - 1));
        if (value.type == 2) value = Resolve(value, &theme, false, table);
        style_flags = value.flags;
        if (value.type == 1) style = value.data;
    }
    auto defaults = def_res ? Style(def_res, table) : Bag{};
    auto explicit_style = style ? Style(style, table) : Bag{};
    for (auto& [id, value] : defaults)
        value.flags |= default_flags;
    for (auto& [id, value] : explicit_style)
        value.flags |= style_flags;
    std::vector<ThemeValue> result;
    result.reserve(attrs.size());
    std::int32_t cookie = 1;
    for (const auto attr : attrs) {
        ThemeValue value;
        value.cookie = cookie;
        if (xml) {
            for (const auto& item : xml->attributes)
                if (item.resource_id == attr) {
                    value = XmlValue(item);
                    break;
                }
        }
        for (const auto* bag : {&explicit_style, &defaults}) {
            if (value.type == 0 && value.unsupported.empty()) {
                const auto it = bag->find(attr);
                if (it != bag->end()) value = it->second;
            }
        }
        if (value.type == 0 && value.unsupported.empty()) {
            if (const auto themed = Lookup(theme, attr)) {
                const auto prior_cookie = value.cookie;
                value = *themed;
                if (value.type != 3 || value.cookie != 2) value.cookie = prior_cookie;
            }
        }
        value = Resolve(value, &theme, true, table);
        if (value.type == 1 && value.data == 0) {
            value.type = 0;
            value.cookie = -1;
        }
        if (drawable_attributes_.contains(attr) && value.type != 0 &&
            !(value.type == 1 && projected_reference_ids_.contains(value.data)))
            throw std::runtime_error("requested drawable attribute is unsupported");
        cookie = value.cookie;
        result.push_back(value);
    }
    return result;
}

std::vector<ThemeValue> ThemeResources::Retrieve(const loader::CompiledXmlNode& xml,
                                                 std::span<const std::uint32_t> attrs,
                                                 const loader::ArscTable& table) const {
    std::vector<ThemeValue> result;
    std::int32_t cookie = 1;
    for (const auto attr : attrs) {
        ThemeValue value;
        value.cookie = cookie;
        for (const auto& item : xml.attributes)
            if (item.resource_id == attr) {
                value = XmlValue(item);
                break;
            }
        value = Resolve(value, nullptr, true, table);
        if (value.type == 1 && !value.data) value.type = 0;
        cookie = value.cookie;
        result.push_back(value);
    }
    return result;
}

void ThemeResources::Shutdown() {
    themes_.clear();
    select_ = {};
    validate_file_ = {};
    stopped_ = true;
}
}  // namespace ogplay::runtime
