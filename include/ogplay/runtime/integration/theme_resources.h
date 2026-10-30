#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <span>
#include <set>
#include <string>
#include <vector>

#include "ogplay/loader/arsc.h"
#include "ogplay/loader/binary_xml.h"

namespace ogplay::runtime {

struct ThemeToken final {
    std::uint32_t value{};
    auto operator<=>(const ThemeToken&) const = default;
};

// Native ABI value: the data belongs to Java after an atomic array publication.
struct ThemeValue final {
    std::uint32_t type{}, data{};
    std::int32_t cookie{1};
    std::uint32_t resource_id{}, flags{}, density{};
    std::string unsupported;
    [[nodiscard]] std::array<std::uint32_t, 6> Slots() const;
};

class ThemeResources final {
public:
    void Load(std::string_view projection);
    // Selection and file validation are injected by the APK owner, never by UI.
    using ResourceSelector = std::function<const loader::ArscEntry*(std::uint32_t)>;
    using FileValidator = std::function<void(const loader::ArscEntry&)>;
    void SetResourceHooks(ResourceSelector select, FileValidator validate);

    [[nodiscard]] ThemeToken New();
    void Delete(ThemeToken token);
    [[nodiscard]] bool Has(ThemeToken token) const;
    void Copy(ThemeToken destination, ThemeToken source);
    void Apply(ThemeToken token, std::uint32_t style, bool force, const loader::ArscTable& table);
    [[nodiscard]] std::optional<ThemeValue> Attribute(ThemeToken token, std::uint32_t attribute, bool resolve,
                                                      const loader::ArscTable& table) const;
    [[nodiscard]] ThemeValue Resource(std::uint32_t resource, bool resolve, const loader::ArscTable& table) const;
    [[nodiscard]] std::optional<ThemeValue> BagValue(std::uint32_t style, std::uint32_t attribute, bool resolve,
                                                     const loader::ArscTable& table) const;
    [[nodiscard]] std::vector<ThemeValue> Styled(ThemeToken token, std::uint32_t def_attr, std::uint32_t def_res,
                                                 const loader::CompiledXmlNode* xml,
                                                 std::span<const std::uint32_t> attrs,
                                                 const loader::ArscTable& table) const;
    [[nodiscard]] std::vector<ThemeValue> Retrieve(const loader::CompiledXmlNode& xml,
                                                   std::span<const std::uint32_t> attrs,
                                                   const loader::ArscTable& table) const;
    void RequireRegistered(std::string_view theme) const;
    [[nodiscard]] std::uint32_t ThemeId(std::string_view theme) const;

    struct RomDrawable final {
        struct Item final {
            std::vector<std::int32_t> states;
            std::uint32_t drawable{};
        };

        std::uint32_t id{}, density{}, width{}, height{};
        std::array<std::int32_t, 4> padding{}, optical{};
        std::array<std::int32_t, 2> stretch_x{}, stretch_y{};
        std::vector<std::byte> png;
        std::vector<Item> items;
    };

    [[nodiscard]] const RomDrawable* Drawable(std::uint32_t id, std::uint32_t density) const;

    [[nodiscard]] const std::vector<std::byte>& Strings() const { return string_pool_; }

    struct RomColor final {
        ThemeValue value;
        std::vector<std::byte> xml;
    };

    [[nodiscard]] const RomColor* Color(std::uint32_t id) const;
    void Shutdown();

    [[nodiscard]] std::size_t LiveThemes() const { return themes_.size(); }

private:
    using Bag = std::map<std::uint32_t, ThemeValue>;

    struct State final {
        Bag values;
        bool projected{};
    };

    [[nodiscard]] const State& Require(ThemeToken token) const;
    [[nodiscard]] Bag Style(std::uint32_t style, const loader::ArscTable& table,
                            std::vector<std::uint32_t> seen = {}) const;
    [[nodiscard]] ThemeValue Resolve(ThemeValue value, const State* theme, bool resources,
                                     const loader::ArscTable& table) const;
    [[nodiscard]] std::optional<ThemeValue> Lookup(const State& theme, std::uint32_t attr) const;
    [[nodiscard]] const loader::ArscEntry* Entry(std::uint32_t id, const loader::ArscTable& table) const;
    void ValidateFile(const ThemeValue& value, const loader::ArscTable& table) const;
    ResourceSelector select_;
    FileValidator validate_file_;
    std::map<std::uint32_t, Bag> projection_;
    std::map<std::uint32_t, std::string> registered_names_;
    std::map<std::string, std::string, std::less<>> unregistered_;
    std::map<ThemeToken, State> themes_;
    std::set<std::uint32_t> drawable_attributes_;
    std::set<std::uint32_t> projected_reference_ids_;
    std::vector<std::byte> string_pool_;
    std::vector<RomDrawable> rom_drawables_;
    std::map<std::uint32_t, RomColor> rom_colors_;
    std::map<std::uint32_t, ThemeValue> rom_configuration_;
    bool loaded_{}, stopped_{};
};
}  // namespace ogplay::runtime
