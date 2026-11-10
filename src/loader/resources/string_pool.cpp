// Copyright (C) 2008 The Android Open Source Project
// SPDX-License-Identifier: Apache-2.0
// ResStringPool semantics: android-4.4.4_r2.0.1 libs/androidfw/ResourceTypes.cpp.
#include "binary.h"
#include "ogplay/core/text.h"
#include <algorithm>
#include <mutex>

namespace ogplay::loader::resources {
using namespace detail;

namespace {
// KitKat pools may encode individual UTF-16 surrogate code units as well as UTF-8 scalars.
std::u16string decodeUtf8(std::string_view value, std::size_t units) {
    std::u16string target;
    target.reserve(units);
    for (std::size_t i = 0; i < value.size();) {
        const auto c = static_cast<std::uint8_t>(value[i++]);
        std::uint32_t codePoint = c, minimum = 0;
        unsigned follow = 0;
        if (c >= 0xf0 && c <= 0xf4) {
            codePoint = c & 7;
            follow = 3;
            minimum = 0x10000;
        } else if (c >= 0xe0 && c <= 0xef) {
            codePoint = c & 15;
            follow = 2;
            minimum = 0x800;
        } else if (c >= 0xc0 && c <= 0xdf) {
            codePoint = c & 31;
            follow = 1;
            minimum = 0x80;
        } else {
            require(c < 0x80, "invalid UTF-8 pool lead byte");
        }
        require(follow <= value.size() - i, "truncated UTF-8 pool string");
        for (unsigned j = 0; j < follow; ++j) {
            const auto next = static_cast<std::uint8_t>(value[i++]);
            require((next & 0xc0) == 0x80, "invalid UTF-8 pool continuation");
            codePoint = (codePoint << 6) | (next & 63);
        }
        require((codePoint >= minimum && codePoint <= 0x10ffff) || (codePoint == 0 && c == 0xc0),
                "invalid UTF-8 pool code point");
        if (codePoint > 0xffff) {
            codePoint -= 0x10000;
            target.push_back(static_cast<char16_t>(0xd800 + (codePoint >> 10)));
            target.push_back(static_cast<char16_t>(0xdc00 + (codePoint & 1023)));
        } else {
            target.push_back(static_cast<char16_t>(codePoint));
        }
    }
    require(target.size() == units, "invalid UTF-8 pool string length");
    return target;
}
}  // namespace

struct ResStringPool::Impl {
    std::shared_ptr<const void> owner;
    Bytes bytes;
    std::uint32_t count{}, styles{}, flags{}, stringsStart{}, stylesStart{};
    mutable std::mutex mutex;
    mutable std::map<std::size_t, std::u16string> cache;

    std::pair<std::size_t, std::size_t> stringRange(std::size_t index) const {
        require(index < count, "string pool index outside pool");
        auto offset = u32(bytes, u16(bytes, 2) + index * 4);
        require((flags & 0x100) || offset % 2 == 0, "unaligned UTF-16 string offset");
        auto end = stylesStart ? stylesStart : bytes.size();
        require(stringsStart <= end && offset < end - stringsStart, "string pool offset outside string data");
        return {stringsStart + offset, end};
    }

    std::u16string decode(std::size_t index) const {
        auto [p, end] = stringRange(index);
        auto data = bytes.first(end);
        const auto len8 = [&]() {
            auto n = static_cast<std::uint32_t>(u8(data, p++));
            if (n & 0x80) n = ((n & 0x7f) << 8) | u8(data, p++);
            return n;
        };
        const auto len16 = [&]() {
            auto n = static_cast<std::uint32_t>(u16(data, p));
            p += 2;
            if (n & 0x8000) {
                n = ((n & 0x7fff) << 16) | u16(data, p);
                p += 2;
            }
            return n;
        };
        std::u16string result;
        if (flags & 0x100) {
            auto units = len8();
            auto len = len8();
            auto raw = range(data, p, len);
            require(u8(data, p + len) == 0, "unterminated UTF-8 pool string");
            std::string_view s(reinterpret_cast<const char*>(raw.data()), raw.size());
            return decodeUtf8(s, units);
        } else {
            auto len = len16();
            range(data, p, static_cast<std::size_t>(len) * 2);
            require(u16(data, p + static_cast<std::size_t>(len) * 2) == 0, "unterminated UTF-16 pool string");
            result.reserve(len);
            for (std::size_t i = 0; i < len; ++i)
                result.push_back(static_cast<char16_t>(u16(data, p + i * 2)));
        }
        return result;
    }
};

ResStringPool::ResStringPool(Bytes bytes, std::shared_ptr<const void> owner) : impl_(std::make_shared<Impl>()) {
    auto b = chunk(bytes, 0);
    require(u16(b, 0) == 1 && u16(b, 2) >= 28, "invalid string pool header");
    auto& p = *impl_;
    p.owner = std::move(owner);
    p.bytes = b;
    p.count = u32(b, 8);
    p.styles = u32(b, 12);
    p.flags = u32(b, 16);
    p.stringsStart = u32(b, 20);
    p.stylesStart = u32(b, 24);
    auto header = u16(b, 2);
    auto indices = static_cast<std::size_t>(p.count) + p.styles;
    range(b, header, indices * 4);
    require(p.stringsStart >= header + indices * 4 && p.stringsStart <= b.size(), "invalid string data start");
    require(!p.stylesStart || (p.stylesStart >= p.stringsStart && p.stylesStart <= b.size()),
            "invalid style data start");
    require(p.styles <= p.count && (!p.styles || p.stylesStart), "invalid style offset table");
    // Validate every offset without decoding or caching the pool.
    for (std::size_t i = 0; i < p.count; ++i)
        (void)p.stringRange(i);
    for (std::size_t i = 0; i < p.styles; ++i) {
        auto off = u32(b, header + (static_cast<std::size_t>(p.count) + i) * 4);
        if (off != NoIndex) {
            require(off % 4 == 0, "unaligned style span");
            range(b, p.stylesStart + static_cast<std::size_t>(off), 4);
        }
    }
}

std::size_t ResStringPool::size() const {
    return impl_->count;
}

bool ResStringPool::sorted() const {
    return (impl_->flags & 1) != 0;
}

bool ResStringPool::utf8() const {
    return (impl_->flags & 0x100) != 0;
}

std::u16string ResStringPool::string16At(std::size_t index) const {
    std::lock_guard lock(impl_->mutex);
    if (auto it = impl_->cache.find(index); it != impl_->cache.end()) return it->second;
    auto result = impl_->decode(index);
    impl_->cache.emplace(index, result);
    return result;
}

std::string ResStringPool::stringAt(std::size_t index) const {
    auto s = string16At(index);
    auto result = core::Utf16ToUtf8(s, core::InvalidUtf16Policy::reject);
    require(result.has_value(), "invalid UTF-16 pool string");
    return *result;
}

std::vector<Span> ResStringPool::styleAt(std::size_t index) const {
    require(index < size(), "style string index outside pool");
    const auto& p = *impl_;
    if (index >= p.styles) return {};
    auto off = u32(p.bytes, u16(p.bytes, 2) + (static_cast<std::size_t>(p.count) + index) * 4);
    if (off == NoIndex) return {};
    std::vector<Span> result;
    auto length = string16At(index).size();
    for (std::size_t pos = p.stylesStart + static_cast<std::size_t>(off);; pos += 12) {
        auto name = u32(p.bytes, pos);
        if (name == NoIndex) break;
        range(p.bytes, pos, 12);
        Span span{name, u32(p.bytes, pos + 4), u32(p.bytes, pos + 8)};
        stringIndex(*this, name);
        require(span.firstChar <= span.lastChar && span.lastChar < length, "style span outside string");
        result.push_back(span);
    }
    return result;
}

std::optional<std::size_t> ResStringPool::indexOfString(std::string_view value) const {
    if (!sorted()) {
        for (std::size_t i = size(); i > 0; --i)
            if (stringAt(i - 1) == value) return i - 1;
        return {};
    }
    const auto validation = core::ValidateUtf8(value);
    require(validation.IsValid(), "invalid UTF-8 lookup string");
    const auto target = decodeUtf8(value, validation.utf16_code_units);
    std::size_t lo = 0, hi = size();
    while (lo < hi) {
        auto mid = lo + (hi - lo) / 2;
        auto candidate = string16At(mid);
        if (candidate < target)
            lo = mid + 1;
        else
            hi = mid;
    }
    if (lo < size() && string16At(lo) == target) return lo;
    return {};
}

std::array<std::int32_t, 6> TypedValue::slots() const {
    return {value.type,
            static_cast<std::int32_t>(value.data),
            cookie,
            static_cast<std::int32_t>(resourceId),
            static_cast<std::int32_t>(changingConfigurations),
            density};
}
}  // namespace ogplay::loader::resources
