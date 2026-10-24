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

void ThemeResources::Load(std::string_view projection) {
    if (loaded_ || stopped_) throw std::logic_error("theme projection cannot be replaced");
    core::JsonParseError error;
    const auto json = core::JsonDocument::ParseStrict(projection, error, 4U * 1024U * 1024U, 16);
    if (!json) throw std::runtime_error("invalid theme projection: " + error.message);
    const auto root = json->Root();
    if (Text(Member(root, "schema")) != "ogplay.bounded-theme" || Number(Member(root, "schema_version")) != 1 ||
        Number(Member(root, "api_level")) != 19 || Text(Member(root, "configuration")) != "values")
        throw std::runtime_error("unsupported theme projection schema/configuration");
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
    for (std::size_t i = 0; i < registered.Size(); ++i) {
        const auto row = *registered.Element(i);
        const auto id = Number(Member(row, "id"));
        if ((id >> 24U) != 1 || projected.contains(id)) throw std::runtime_error("invalid/duplicate theme ID");
        names.emplace(id, Text(Member(row, "name")));
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
    drawable_attributes_ = std::move(drawables);
    projection_ = std::move(projected);
    registered_names_ = std::move(names);
    unregistered_ = std::move(rejected);
    loaded_ = true;
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
    const auto* entry = table.FindById(style);
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
    if ((style >> 24U) == 1 || (table.FindById(style) && table.FindById(style)->parent)) {
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
        if (value.type != 1 || !value.data || !resources) return value;
        if (!seen.emplace(1, value.data).second) throw std::runtime_error("resource reference cycle");
        const auto id = value.data;
        if ((id >> 24U) == 1)
            throw std::runtime_error("unsupported projected resource reference " + std::to_string(id));
        const auto* entry = table.FindById(id);
        if (!entry) throw std::runtime_error("unknown resource reference");
        if (entry->is_complex) throw std::runtime_error("cannot resolve bag as a scalar");
        if (entry->type_name == "drawable" || entry->type_name == "anim" || entry->type_name == "animation" ||
            (entry->string_value && entry->string_value->starts_with("res/")))
            throw std::runtime_error("unsupported drawable/selector/animation/file value");
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
    const auto* entry = table.FindById(resource);
    if ((resource >> 24U) == 1 || !entry || entry->is_complex)
        throw std::runtime_error("unknown or non-scalar resource");
    ThemeValue value{
        entry->value_type, entry->value_data, 1, resource, entry->changing_configurations, entry->density, {}};
    if (entry->type_name == "drawable" || entry->type_name == "anim" ||
        (entry->string_value && entry->string_value->starts_with("res/")))
        throw std::runtime_error("unsupported resource file value");
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
                value.cookie = prior_cookie;
            }
        }
        value = Resolve(value, &theme, true, table);
        if (value.type == 1 && value.data == 0) {
            value.type = 0;
            value.cookie = -1;
        }
        if (drawable_attributes_.contains(attr) && value.type != 0)
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
    stopped_ = true;
}
}  // namespace ogplay::runtime
