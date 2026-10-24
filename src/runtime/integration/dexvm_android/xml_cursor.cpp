#include "ogplay/runtime/integration/xml_cursor.h"

#include <atomic>
#include <limits>
#include <stdexcept>

namespace ogplay::runtime {
namespace {
std::uint32_t NewToken() {
    // Never wrap or reuse IDs: a stale token cannot alias a later owner/parser.
    static std::atomic<std::uint64_t> next{1};
    auto current = next.load();
    do {
        if (current > std::numeric_limits<std::uint32_t>::max()) throw std::overflow_error("XML token space exhausted");
    } while (!next.compare_exchange_weak(current, current + 1));
    return static_cast<std::uint32_t>(current);
}
}  // namespace

XmlDocumentToken XmlCursorRegistry::CreateDocument(std::span<const std::byte> bytes) {
    if (stopped_) throw std::logic_error("XML registry is shut down");
    auto document = loader::ParseCompiledXmlDocument(bytes);
    const XmlDocumentToken token{NewToken()};
    documents_.emplace(token, std::move(document));
    return token;
}

std::shared_ptr<const loader::CompiledXmlDocument> XmlCursorRegistry::Document(XmlDocumentToken owner) const {
    const auto found = documents_.find(owner);
    if (found == documents_.end()) throw std::invalid_argument("XML document token is not live in this context");
    return found->second;
}

XmlStringPoolToken XmlCursorRegistry::CreateStringPool(std::span<const std::byte> bytes) {
    if (stopped_) throw std::logic_error("XML registry is shut down");
    auto document = loader::ParseCompiledStringPool(bytes);
    const XmlStringPoolToken token{NewToken()};
    pools_.emplace(token, Pool{{}, std::move(document)});
    return token;
}

XmlStringPoolToken XmlCursorRegistry::DocumentPool(XmlDocumentToken owner) {
    const auto document = Document(owner);
    for (const auto& [token, pool] : pools_)
        if (pool.owner == owner) return token;
    const XmlStringPoolToken token{NewToken()};
    pools_.emplace(token, Pool{owner, document});
    return token;
}

std::shared_ptr<const loader::CompiledXmlDocument> XmlCursorRegistry::StringPool(XmlStringPoolToken token) const {
    const auto found = pools_.find(token);
    if (found == pools_.end()) throw std::invalid_argument("string pool token is not live in this context");
    return found->second.document;
}

void XmlCursorRegistry::DestroyStringPool(XmlStringPoolToken token) {
    static_cast<void>(StringPool(token));
    if (pools_.at(token).owner) throw std::logic_error("borrowed XML string pool cannot be destroyed");
    pools_.erase(token);
}

XmlDocumentToken XmlCursorRegistry::CursorDocument(XmlCursorToken token) const {
    return Require(token).owner;
}

XmlCursorToken XmlCursorRegistry::CreateCursor(XmlDocumentToken owner) {
    static_cast<void>(Document(owner));
    const XmlCursorToken token{NewToken()};
    cursors_.emplace(token, Cursor{owner, 0, {}});
    return token;
}

const XmlCursorRegistry::Cursor& XmlCursorRegistry::Require(XmlCursorToken token) const {
    const auto found = cursors_.find(token);
    if (found == cursors_.end()) throw std::invalid_argument("XML cursor token is not live in this context");
    return found->second;
}

XmlCursorPosition XmlCursorRegistry::Current(XmlCursorToken token) const {
    return Require(token).position;
}

XmlCursorPosition XmlCursorRegistry::Next(XmlCursorToken token) {
    static_cast<void>(Require(token));
    auto& cursor = cursors_.at(token);
    const auto doc = Document(cursor.owner);
    while (cursor.next_node < doc->Nodes().size()) {
        const auto& node = doc->Nodes()[cursor.next_node++];
        loader::BinaryXmlPullEventType type;
        switch (node.type) {
        case 0x0100:
        case 0x0101: continue;  // XmlBlock's namespace events are internal
        case 0x0102: type = loader::BinaryXmlPullEventType::start_tag; break;
        case 0x0103: type = loader::BinaryXmlPullEventType::end_tag; break;
        case 0x0104: type = loader::BinaryXmlPullEventType::text; break;
        default: throw std::runtime_error("XML cursor node type is unsupported");
        }
        cursor.position = {type, node.depth, node};
        return cursor.position;
    }
    cursor.position = {loader::BinaryXmlPullEventType::end_document, 0, {}};
    return cursor.position;
}

loader::CompiledXmlNode XmlCursorRegistry::StartTag(XmlCursorToken token) const {
    const auto& position = Require(token).position;
    if (position.event != loader::BinaryXmlPullEventType::start_tag || !position.node)
        throw std::logic_error("XML cursor is not on a start tag");
    return *position.node;
}

loader::CompiledXmlAttribute XmlCursorRegistry::Attribute(XmlCursorToken token, std::size_t index) const {
    return StartTag(token).attributes.at(index);
}

std::optional<std::size_t> XmlCursorRegistry::FindAttribute(XmlCursorToken token,
                                                            std::optional<std::string_view> namespace_uri,
                                                            std::string_view name) const {
    const auto tag = StartTag(token);
    const auto doc = Document(Require(token).owner);
    for (std::size_t i = 0; i < tag.attributes.size(); ++i) {
        const auto& attr = tag.attributes[i];
        const auto ns = doc->String(attr.namespace_index);
        if (doc->String(attr.name_index) == name && ns.has_value() == namespace_uri.has_value() &&
            (!ns || *ns == *namespace_uri))
            return i;
    }
    return std::nullopt;
}

void XmlCursorRegistry::Close(XmlCursorToken token) {
    static_cast<void>(Require(token));
    cursors_.erase(token);
}

void XmlCursorRegistry::RetireOwner(XmlDocumentToken owner) {
    static_cast<void>(Document(owner));
    std::erase_if(cursors_, [owner](const auto& item) { return item.second.owner == owner; });
    std::erase_if(pools_, [owner](const auto& item) { return item.second.owner == owner; });
    documents_.erase(owner);
}

void XmlCursorRegistry::Shutdown() {
    cursors_.clear();
    documents_.clear();
    pools_.clear();
    stopped_ = true;
}
}  // namespace ogplay::runtime
