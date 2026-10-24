#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string_view>

#include "ogplay/loader/binary_xml.h"

namespace ogplay::runtime {

struct XmlDocumentToken final {
    std::uint32_t value{};
    auto operator<=>(const XmlDocumentToken&) const = default;
};

struct XmlCursorToken final {
    std::uint32_t value{};
    auto operator<=>(const XmlCursorToken&) const = default;
};

struct XmlStringPoolToken final {
    std::uint32_t value{};
    auto operator<=>(const XmlStringPoolToken&) const = default;
};

struct XmlCursorPosition final {
    loader::BinaryXmlPullEventType event{loader::BinaryXmlPullEventType::start_document};
    std::int32_t depth{};
    std::optional<loader::CompiledXmlNode> node;
};

// One registry per Android context, serialized by the VM lock. Tokens never
// contain pointers and are not reused, even across context lifetimes. ROM XML
// owners are bound by the experimental native seam and its owner GC hooks.
class XmlCursorRegistry final {
public:
    XmlCursorRegistry() = default;
    XmlCursorRegistry(const XmlCursorRegistry&) = delete;
    XmlCursorRegistry& operator=(const XmlCursorRegistry&) = delete;
    [[nodiscard]] XmlDocumentToken CreateDocument(std::span<const std::byte> bytes);
    [[nodiscard]] XmlStringPoolToken CreateStringPool(std::span<const std::byte> bytes);
    [[nodiscard]] XmlStringPoolToken DocumentPool(XmlDocumentToken owner);
    [[nodiscard]] std::shared_ptr<const loader::CompiledXmlDocument> StringPool(XmlStringPoolToken token) const;
    void DestroyStringPool(XmlStringPoolToken token);

    [[nodiscard]] bool HasStringPool(XmlStringPoolToken token) const { return pools_.contains(token); }

    [[nodiscard]] std::size_t LiveStringPools() const { return pools_.size(); }

    [[nodiscard]] XmlDocumentToken CursorDocument(XmlCursorToken token) const;

    [[nodiscard]] bool HasDocument(XmlDocumentToken token) const { return documents_.contains(token); }

    [[nodiscard]] bool HasCursor(XmlCursorToken token) const { return cursors_.contains(token); }
    [[nodiscard]] XmlCursorToken CreateCursor(XmlDocumentToken owner);
    [[nodiscard]] XmlCursorPosition Next(XmlCursorToken token);
    [[nodiscard]] XmlCursorPosition Current(XmlCursorToken token) const;
    [[nodiscard]] loader::CompiledXmlNode StartTag(XmlCursorToken token) const;
    [[nodiscard]] loader::CompiledXmlAttribute Attribute(XmlCursorToken token, std::size_t index) const;
    [[nodiscard]] std::optional<std::size_t>
    FindAttribute(XmlCursorToken token, std::optional<std::string_view> namespace_uri, std::string_view name) const;
    [[nodiscard]] std::shared_ptr<const loader::CompiledXmlDocument> Document(XmlDocumentToken owner) const;
    void Close(XmlCursorToken token);
    void RetireOwner(XmlDocumentToken owner);
    void Shutdown();

    [[nodiscard]] std::size_t LiveCursors() const { return cursors_.size(); }

    [[nodiscard]] std::size_t LiveDocuments() const { return documents_.size(); }

private:
    struct Cursor final {
        XmlDocumentToken owner;
        std::size_t next_node{};
        XmlCursorPosition position;
    };

    [[nodiscard]] const Cursor& Require(XmlCursorToken token) const;
    std::map<XmlDocumentToken, std::shared_ptr<const loader::CompiledXmlDocument>> documents_;
    std::map<XmlCursorToken, Cursor> cursors_;

    struct Pool final {
        std::optional<XmlDocumentToken> owner;
        std::shared_ptr<const loader::CompiledXmlDocument> document;
    };

    std::map<XmlStringPoolToken, Pool> pools_;
    bool stopped_{};
};
}  // namespace ogplay::runtime
