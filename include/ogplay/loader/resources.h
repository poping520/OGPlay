#pragma once
#include <mutex>
#include <unordered_map>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace ogplay::loader::resources {
using Bytes = std::span<const std::byte>;
constexpr std::uint32_t NoIndex = 0xffffffffU;

class ResourceError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// The owner must keep bytes immutable and alive; it may own a read-only mapping.
struct Source {
    std::shared_ptr<const void> owner;
    Bytes bytes;
    std::string name;
    std::function<bool()> isUpToDate;
};

struct Value {
    std::uint8_t type{};
    std::uint32_t data{};
    bool operator==(const Value&) const = default;
};

struct TypedValue {
    Value value;
    std::int32_t cookie{};
    std::uint32_t resourceId{}, changingConfigurations{};
    std::uint16_t density{};
    [[nodiscard]] std::array<std::int32_t, 6> slots() const;
};

struct Span {
    std::uint32_t name{}, firstChar{}, lastChar{};
    bool operator==(const Span&) const = default;
};

class ResStringPool {
public:
    // If no owner is supplied, the caller keeps bytes alive and immutable.
    explicit ResStringPool(Bytes bytes, std::shared_ptr<const void> owner = {});
    [[nodiscard]] std::size_t size() const;
    [[nodiscard]] bool sorted() const;
    [[nodiscard]] bool utf8() const;
    [[nodiscard]] std::string stringAt(std::size_t index) const;
    [[nodiscard]] std::u16string string16At(std::size_t index) const;
    [[nodiscard]] std::vector<Span> styleAt(std::size_t index) const;
    [[nodiscard]] std::optional<std::size_t> indexOfString(std::string_view value) const;

private:
    struct Impl;
    std::shared_ptr<Impl> impl_;
};

struct ResTable_config {
    std::uint16_t mcc{}, mnc{};
    char language[2]{}, country[2]{};
    std::uint8_t orientation{}, touchscreen{};
    std::uint16_t density{};
    std::uint8_t keyboard{}, navigation{}, inputFlags{};
    std::uint16_t screenWidth{}, screenHeight{}, sdkVersion{}, minorVersion{};
    std::uint8_t screenLayout{}, uiMode{};
    std::uint16_t smallestScreenWidthDp{}, screenWidthDp{}, screenHeightDp{};

    enum {
        MASK_LAYOUTDIR = 0xc0,
        MASK_SCREENSIZE = 0x0f,
        SCREENSIZE_NORMAL = 2,
        MASK_SCREENLONG = 0x30,
        MASK_UI_MODE_TYPE = 0x0f,
        MASK_UI_MODE_NIGHT = 0x30,
        MASK_KEYSHIDDEN = 3,
        MASK_NAVHIDDEN = 12,
        KEYSHIDDEN_NO = 1,
        KEYSHIDDEN_SOFT = 3,
        DENSITY_MEDIUM = 160,
        DENSITY_NONE = 0xffff
    };

    [[nodiscard]] bool match(const ResTable_config& settings) const;
    [[nodiscard]] bool isBetterThan(const ResTable_config& other, const ResTable_config* requested) const;
    [[nodiscard]] bool isMoreSpecificThan(const ResTable_config& other) const;
    [[nodiscard]] std::string localeName() const;
    [[nodiscard]] static ResTable_config decode(Bytes bytes);
};

class ResTableData {
public:
    explicit ResTableData(Source arsc);
    [[nodiscard]] const ResStringPool& strings() const;

private:
    struct Impl;
    std::shared_ptr<const Impl> impl_;
    friend class ResTable;
};

struct ResourceName {
    std::string package, type, name;
    [[nodiscard]] std::string fullName() const;
};

struct BagEntry {
    std::uint32_t name{};
    TypedValue value;
};

struct Bag {
    std::uint32_t typeSpecFlags{};
    std::vector<BagEntry> entries;
};

struct ResourceVariant {
    std::uint32_t id{};
    ResTable_config config;
    bool complex{};
    std::uint32_t parent{};
    Value value;
    std::vector<std::pair<std::uint32_t, Value>> bag;
};

class ResTable {
public:
    void add(std::shared_ptr<const ResTableData> data, std::int32_t cookie);
    void setParameters(const ResTable_config& parameters);
    [[nodiscard]] const ResTable_config& parameters() const;
    [[nodiscard]] std::optional<TypedValue> getResource(std::uint32_t id, std::uint16_t density = 0,
                                                        std::uint32_t* outSpecFlags = nullptr) const;
    [[nodiscard]] TypedValue resolveReference(TypedValue value) const;
    [[nodiscard]] std::shared_ptr<const Bag> getBagLocked(std::uint32_t id) const;
    [[nodiscard]] std::uint32_t identifierForName(std::string_view name, std::string_view type = {},
                                                  std::string_view package = {},
                                                  std::uint32_t* outSpecFlags = nullptr) const;
    [[nodiscard]] std::optional<ResourceName> getResourceName(std::uint32_t id) const;
    [[nodiscard]] std::vector<std::string> getLocales() const;
    [[nodiscard]] std::vector<std::string> packageNames() const;
    [[nodiscard]] const ResStringPool& strings(std::int32_t cookie) const;
    [[nodiscard]] std::size_t tableCount() const;
    [[nodiscard]] std::int32_t stringBlock(std::int32_t cookie) const;
    [[nodiscard]] std::int32_t cookieForBlock(std::size_t block) const;
    // A streaming inspection interface; materializes one entry at a time, never the entire table.
    void forEachVariant(const std::function<void(const ResourceVariant&)>& visitor) const;

    [[nodiscard]] bool validBagKey(std::uint32_t id) const;

private:
    struct Table {
        std::shared_ptr<const ResTableData> data;
        std::int32_t cookie;
    };

    struct Selected;

    struct BagCache {
        std::mutex mutex;
        std::unordered_map<std::uint32_t, std::shared_ptr<const Bag>> values;
    };

    std::unique_ptr<BagCache> bagCache_ = std::make_unique<BagCache>();
    std::vector<Table> tables_;
    ResTable_config parameters_;
    [[nodiscard]] std::optional<Selected> select(std::uint32_t id, std::uint16_t density = 0) const;
    [[nodiscard]] std::shared_ptr<const Bag> bag(std::uint32_t id, std::vector<std::uint32_t>& parents) const;
};

}  // namespace ogplay::loader::resources
