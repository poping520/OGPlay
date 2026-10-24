#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
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
    std::map<std::uint32_t, Bag> projection_;
    std::map<std::uint32_t, std::string> registered_names_;
    std::map<std::string, std::string, std::less<>> unregistered_;
    std::map<ThemeToken, State> themes_;
    bool loaded_{}, stopped_{};
};
}  // namespace ogplay::runtime
