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
#include "ogplay/loader/apk.h"
#include "ogplay/loader/resources.h"

namespace ogplay::runtime::resources {
using loader::resources::Bag;
using loader::resources::BagEntry;
using loader::resources::Bytes;
using loader::resources::NoIndex;
using loader::resources::ResourceError;
using loader::resources::ResourceName;
using loader::resources::ResourceVariant;
using loader::resources::ResStringPool;
using loader::resources::ResTable;
using loader::resources::ResTable_config;
using loader::resources::ResTableData;
using loader::resources::Source;
using loader::resources::Span;
using loader::resources::TypedValue;
using loader::resources::Value;

struct XmlAttribute {
    std::uint32_t namespaceIndex{NoIndex}, nameIndex{NoIndex}, rawValue{NoIndex}, resourceId{};
    Value value;
};

class XmlAttributes {
public:
    virtual ~XmlAttributes() = default;
    [[nodiscard]] virtual std::size_t attributeCount() const = 0;
    [[nodiscard]] virtual XmlAttribute attribute(std::size_t index) const = 0;
    [[nodiscard]] virtual std::optional<std::size_t> indexOfStyle() const = 0;
};
class ResXMLParser;

class ResXMLTree {
public:
    explicit ResXMLTree(Source source);
    [[nodiscard]] const ResStringPool& strings() const;

private:
    struct Impl;
    std::shared_ptr<const Impl> impl_;
    friend class ResXMLParser;
};

class ResXMLParser final : public XmlAttributes {
public:
    enum Event : std::int32_t {
        BadDocument = -1,
        StartDocument = 0,
        EndDocument = 1,
        StartNamespace = 0x100,
        EndNamespace = 0x101,
        StartTag = 0x102,
        EndTag = 0x103,
        Text = 0x104
    };

    explicit ResXMLParser(const ResXMLTree& tree);
    Event next();
    // ROM XmlBlock nativeNext event numbers, with namespace events skipped.
    [[nodiscard]] std::int32_t nextXmlBlockEvent();
    void restart();
    [[nodiscard]] Event event() const;
    [[nodiscard]] std::uint32_t lineNumber() const;
    [[nodiscard]] std::uint32_t comment() const;
    [[nodiscard]] std::uint32_t namespacePrefix() const;
    [[nodiscard]] std::uint32_t namespaceUri() const;
    [[nodiscard]] std::uint32_t elementNamespace() const;
    [[nodiscard]] std::uint32_t elementName() const;
    [[nodiscard]] std::uint32_t text() const;
    [[nodiscard]] Value textValue() const;
    [[nodiscard]] std::size_t attributeCount() const override;
    [[nodiscard]] XmlAttribute attribute(std::size_t index) const override;
    [[nodiscard]] std::optional<std::size_t> indexOfAttribute(std::optional<std::string_view> ns,
                                                              std::string_view name) const;
    [[nodiscard]] std::optional<std::size_t> indexOfId() const;
    [[nodiscard]] std::optional<std::size_t> indexOfClass() const;
    [[nodiscard]] std::optional<std::size_t> indexOfStyle() const override;
    [[nodiscard]] std::uint32_t idAttribute() const;
    [[nodiscard]] std::uint32_t classAttribute() const;
    [[nodiscard]] std::uint32_t styleAttribute() const;
    [[nodiscard]] const ResStringPool& strings() const;

private:
    std::shared_ptr<const ResXMLTree::Impl> tree_;
    std::size_t cursor_{};
    Event event_{StartDocument};
    [[nodiscard]] Bytes node() const;
};

class Theme {
public:
    explicit Theme(const ResTable& table);
    bool applyStyle(std::uint32_t id, bool force);
    void setTo(const Theme& other);
    [[nodiscard]] std::optional<TypedValue> getAttribute(std::uint32_t id) const;
    [[nodiscard]] TypedValue resolveAttributeReference(TypedValue value) const;
    [[nodiscard]] const ResTable& table() const;

private:
    const ResTable* table_;
    std::map<std::uint32_t, TypedValue> attributes_;
};

struct StyleResult {
    std::vector<std::int32_t> values, indices;
};

[[nodiscard]] StyleResult applyStyle(const Theme& theme, std::uint32_t defStyleAttr, std::uint32_t defStyleRes,
                                     const XmlAttributes* xml, std::span<const std::uint32_t> attrs);
[[nodiscard]] StyleResult retrieveAttributes(const ResTable& table, const XmlAttributes& xml,
                                             std::span<const std::uint32_t> attrs);
[[nodiscard]] std::vector<std::int32_t> retrieveArray(const ResTable& table, std::uint32_t id);
[[nodiscard]] std::size_t getArraySize(const ResTable& table, std::uint32_t id);
[[nodiscard]] std::optional<TypedValue> loadThemeAttributeValue(const Theme& theme, std::uint32_t attr, bool resolve);
[[nodiscard]] std::optional<TypedValue> loadResourceValue(const ResTable& table, std::uint32_t id,
                                                          std::uint16_t density, bool resolve);
[[nodiscard]] std::optional<TypedValue> loadResourceBagValue(const ResTable& table, std::uint32_t id,
                                                             std::uint32_t entry, bool resolve);
[[nodiscard]] std::vector<std::array<std::int32_t, 2>> getArrayStringInfo(const ResTable& table, std::uint32_t id);
[[nodiscard]] std::vector<std::optional<std::u16string>> getArrayStringResource(const ResTable& table,
                                                                                std::uint32_t id);
[[nodiscard]] std::vector<std::int32_t> getArrayIntResource(const ResTable& table, std::uint32_t id);

class AssetPath {
public:
    explicit AssetPath(Source source);
    [[nodiscard]] const Source& source() const;
    [[nodiscard]] const loader::ApkArchive& archive() const;
    [[nodiscard]] std::shared_ptr<const ResTableData> table() const;
    [[nodiscard]] const loader::ApkEntry* find(std::string_view name) const;
    AssetPath(const AssetPath&) = delete;
    AssetPath& operator=(const AssetPath&) = delete;

private:
    Source source_;
    loader::ApkArchive archive_;
    std::unordered_map<std::string_view, const loader::ApkEntry*> entries_;
    std::shared_ptr<const ResTableData> table_;
};

// Process-lifetime shared framework mapping/index. Provider runs once, retries on failure.
[[nodiscard]] std::shared_ptr<const AssetPath> sharedFramework(const std::function<Source()>& provider);

struct AssetFd {
    Source source;
    std::uint64_t offset{}, length{};
};

class Asset {
public:
    Asset(std::shared_ptr<const AssetPath> path, loader::ApkEntry entry, std::int32_t cookie);
    ~Asset();
    Asset(const Asset&) = delete;
    Asset& operator=(const Asset&) = delete;
    [[nodiscard]] std::size_t read(std::span<std::byte> destination);
    [[nodiscard]] std::uint64_t seek(std::int64_t offset, int whence);
    [[nodiscard]] std::uint64_t length() const;
    [[nodiscard]] std::uint64_t remaining() const;
    [[nodiscard]] std::int32_t cookie() const;
    [[nodiscard]] std::string_view name() const;
    [[nodiscard]] bool isStored() const;
    [[nodiscard]] Bytes storedBuffer() const;
    [[nodiscard]] AssetFd openFd() const;
    [[nodiscard]] static std::size_t globalCount();

private:
    std::shared_ptr<const AssetPath> path_;
    loader::ApkEntry entry_;
    std::int32_t cookie_;
    Bytes stored_;
    std::unique_ptr<loader::ApkDeflateStream> stream_;
    std::uint64_t position_{};
};

class AssetManager {
public:
    explicit AssetManager(std::shared_ptr<const AssetPath> framework);
    [[nodiscard]] std::int32_t addAssetPath(Source source);
    [[nodiscard]] std::unique_ptr<Asset> open(std::string_view name) const;
    [[nodiscard]] std::unique_ptr<Asset> openNonAsset(std::string_view name, std::int32_t cookie = 0) const;
    [[nodiscard]] std::vector<std::string> list(std::string_view directory) const;
    [[nodiscard]] std::string getCookieName(std::int32_t cookie) const;
    [[nodiscard]] bool isUpToDate() const;
    void setConfiguration(const ResTable_config& config, std::optional<std::string_view> locale = std::nullopt);
    [[nodiscard]] ResTable& resources();
    [[nodiscard]] const ResTable& resources() const;

private:
    std::vector<std::shared_ptr<const AssetPath>> paths_;
    ResTable table_;
    std::string locale_;
};
}  // namespace ogplay::runtime::resources
