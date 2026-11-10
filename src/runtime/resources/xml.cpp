// Copyright (C) 2008 The Android Open Source Project
// SPDX-License-Identifier: Apache-2.0
// ResXMLTree/ResXMLParser semantics from android-4.4.4_r2.0.1 ResourceTypes.cpp.
#include "binary.h"
#include <algorithm>

namespace ogplay::runtime::resources {
using namespace detail;

struct ResXMLTree::Impl {
    Source source;
    ResStringPool strings;
    std::vector<std::uint32_t> ids;
    std::vector<std::size_t> nodes;

    Impl(Source s, Bytes pool) : source(std::move(s)), strings(pool, source.owner) {}
};

ResXMLTree::ResXMLTree(Source source) {
    require(source.owner != nullptr, "XML tree requires a byte owner");
    auto root = chunk(source.bytes, 0);
    require(root.size() == source.bytes.size() && u16(root, 0) == 3, "invalid XML root");
    auto pos = static_cast<std::size_t>(u16(root, 2));
    auto pool = chunk(root, pos);
    auto p = std::make_shared<Impl>(std::move(source), pool);
    pos += pool.size();
    bool seenNodes = false;
    std::vector<std::pair<std::uint32_t, std::uint32_t>> elements, namespaces;
    for (; pos < root.size();) {
        auto c = chunk(root, pos);
        auto type = u16(c, 0);
        auto h = u16(c, 2);
        if (type == 0x180) {
            require(!seenNodes && p->ids.empty() && h == 8, "invalid XML resource map");
            for (std::size_t i = h; i < c.size(); i += 4)
                p->ids.push_back(u32(c, i));
            require(p->ids.size() <= p->strings.size(), "XML resource map outside pool");
        } else if (type >= 0x100 && type <= 0x104) {
            seenNodes = true;
            require(h >= 16, "invalid XML node header");
            range(c, 8, 8);
            stringIndex(p->strings, u32(c, 12), true);
            const auto si = [&](std::size_t o, bool optional = false) {
                auto x = u32(c, o);
                stringIndex(p->strings, x, optional);
                return x;
            };
            if (type == 0x100 || type == 0x101) {
                auto pair = std::pair{si(h, true), si(h + 4)};
                if (type == 0x100)
                    namespaces.push_back(pair);
                else {
                    require(!namespaces.empty() && namespaces.back() == pair, "unbalanced XML namespace");
                    namespaces.pop_back();
                }
            } else if (type == 0x102 || type == 0x103) {
                auto pair = std::pair{si(h, true), si(h + 4)};
                if (type == 0x103) {
                    require(!elements.empty() && elements.back() == pair, "unbalanced XML element");
                    elements.pop_back();
                } else {
                    elements.push_back(pair);
                    range(c, h, 20);
                    auto start = u16(c, h + 8), stride = u16(c, h + 10), count = u16(c, h + 12);
                    require(start >= 20 && stride >= 20, "invalid XML attribute stride/start");
                    range(c, h + start, static_cast<std::size_t>(stride) * count);
                    for (std::size_t i = 0; i < count; ++i) {
                        auto a = h + start + i * stride;
                        si(a, true);
                        si(a + 4);
                        si(a + 8, true);
                        auto v = value(c, a + 12);
                        if (v.type == 3) stringIndex(p->strings, v.data);
                    }
                    for (auto o : {14U, 16U, 18U})
                        require(u16(c, h + o) <= count, "XML special attribute index outside attributes");
                }
            } else {
                si(h);
                auto v = value(c, h + 4);
                if (v.type == 3) stringIndex(p->strings, v.data);
            }
            p->nodes.push_back(pos);
        } else
            throw ResourceError("unknown XML chunk");
        pos += c.size();
    }
    require(seenNodes && elements.empty() && namespaces.empty(), "incomplete XML document");
    impl_ = std::move(p);
}

const ResStringPool& ResXMLTree::strings() const {
    return impl_->strings;
}

ResXMLParser::ResXMLParser(const ResXMLTree& t) : tree_(t.impl_) {}

void ResXMLParser::restart() {
    cursor_ = 0;
    event_ = StartDocument;
}

ResXMLParser::Event ResXMLParser::next() {
    if (event_ == EndDocument) return event_;
    if (cursor_ >= tree_->nodes.size()) return event_ = EndDocument;
    auto c = chunk(tree_->source.bytes, tree_->nodes[cursor_++]);
    return event_ = static_cast<Event>(u16(c, 0));
}

std::int32_t ResXMLParser::nextXmlBlockEvent() {
    for (;;) {
        switch (next()) {
        case StartNamespace:
        case EndNamespace: continue;
        case StartTag: return 2;
        case EndTag: return 3;
        case Text: return 4;
        default: return event_;
        }
    }
}

ResXMLParser::Event ResXMLParser::event() const {
    return event_;
}

Bytes ResXMLParser::node() const {
    require(event_ >= StartNamespace && cursor_ && cursor_ <= tree_->nodes.size(), "XML parser is not on a node");
    return chunk(tree_->source.bytes, tree_->nodes[cursor_ - 1]);
}

std::uint32_t ResXMLParser::lineNumber() const {
    return event_ >= StartNamespace ? u32(node(), 8) : NoIndex;
}

std::uint32_t ResXMLParser::comment() const {
    return event_ >= StartNamespace ? u32(node(), 12) : NoIndex;
}

std::uint32_t ResXMLParser::namespacePrefix() const {
    if (event_ != StartNamespace && event_ != EndNamespace) return NoIndex;
    auto n = node();
    return u32(n, u16(n, 2));
}

std::uint32_t ResXMLParser::namespaceUri() const {
    if (event_ != StartNamespace && event_ != EndNamespace) return NoIndex;
    auto n = node();
    return u32(n, u16(n, 2) + 4);
}

std::uint32_t ResXMLParser::elementNamespace() const {
    if (event_ != StartTag && event_ != EndTag) return NoIndex;
    auto n = node();
    return u32(n, u16(n, 2));
}

std::uint32_t ResXMLParser::elementName() const {
    if (event_ != StartTag && event_ != EndTag) return NoIndex;
    auto n = node();
    return u32(n, u16(n, 2) + 4);
}

std::uint32_t ResXMLParser::text() const {
    if (event_ != Text) return NoIndex;
    auto n = node();
    return u32(n, u16(n, 2));
}

Value ResXMLParser::textValue() const {
    require(event_ == Text, "XML parser is not on text");
    auto n = node();
    return value(n, u16(n, 2) + 4);
}

std::size_t ResXMLParser::attributeCount() const {
    if (event_ != StartTag) return 0;
    auto n = node();
    return u16(n, u16(n, 2) + 12);
}

XmlAttribute ResXMLParser::attribute(std::size_t i) const {
    require(i < attributeCount(), "XML attribute index outside tag");
    auto n = node();
    auto h = u16(n, 2);
    auto a = h + u16(n, h + 8) + i * u16(n, h + 10);
    auto name = u32(n, a + 4);
    return {u32(n, a), name, u32(n, a + 8), name < tree_->ids.size() ? tree_->ids[name] : 0, value(n, a + 12)};
}

std::optional<std::size_t> ResXMLParser::indexOfAttribute(std::optional<std::string_view> ns,
                                                          std::string_view name) const {
    for (std::size_t i = 0; i < attributeCount(); ++i) {
        auto a = attribute(i);
        if (strings().stringAt(a.nameIndex) == name &&
            (ns ? a.namespaceIndex != NoIndex && strings().stringAt(a.namespaceIndex) == *ns
                : a.namespaceIndex == NoIndex))
            return i;
    }
    return {};
}

std::optional<std::size_t> ResXMLParser::indexOfId() const {
    if (event_ != StartTag) return {};
    auto n = node();
    auto i = u16(n, u16(n, 2) + 14);
    return i ? std::optional<std::size_t>(i - 1) : std::nullopt;
}

std::optional<std::size_t> ResXMLParser::indexOfClass() const {
    if (event_ != StartTag) return {};
    auto n = node();
    auto i = u16(n, u16(n, 2) + 16);
    return i ? std::optional<std::size_t>(i - 1) : std::nullopt;
}

std::optional<std::size_t> ResXMLParser::indexOfStyle() const {
    if (event_ != StartTag) return {};
    auto n = node();
    auto i = u16(n, u16(n, 2) + 18);
    return i ? std::optional<std::size_t>(i - 1) : std::nullopt;
}

std::uint32_t ResXMLParser::idAttribute() const {
    const auto index = indexOfId();
    return index ? attribute(*index).rawValue : NoIndex;
}

std::uint32_t ResXMLParser::classAttribute() const {
    const auto index = indexOfClass();
    return index ? attribute(*index).rawValue : NoIndex;
}

std::uint32_t ResXMLParser::styleAttribute() const {
    const auto index = indexOfStyle();
    if (!index) return 0;
    const auto typed = attribute(*index).value;
    return typed.type == 1 || typed.type == 2 ? typed.data : 0;
}

const ResStringPool& ResXMLParser::strings() const {
    return tree_->strings;
}
}  // namespace ogplay::runtime::resources
