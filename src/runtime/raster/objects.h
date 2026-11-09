#pragma once

#include "include/core/SkMatrix.h"
#include "include/core/SkPathBuilder.h"
#include "include/core/SkPathMeasure.h"
#include "include/core/SkRegion.h"

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <utility>

namespace ogplay::runtime::raster {

// The Java int is the wire representation. Keep type checking on either
// side of the registry and never put a native pointer in a guest field.
template <class T>
struct Token final {
    std::uint32_t bits{};
};

std::uint32_t AllocateToken();

template <class T>
class Objects final {
public:
    template <class... Args>
    Token<T> Create(Args&&... args) {
        const Token<T> token{AllocateToken()};
        values_.try_emplace(token.bits, std::forward<Args>(args)...);
        return token;
    }

    [[nodiscard]] T* Find(Token<T> token) {
        const auto value = values_.find(token.bits);
        return value == values_.end() ? nullptr : &value->second;
    }

    void Retire(Token<T> token) { values_.erase(token.bits); }

private:
    std::unordered_map<std::uint32_t, T> values_;
};

struct RegionIterator final {
    explicit RegionIterator(const SkRegion& source) : region(source), iterator(region) {}

    const SkRegion region;
    SkRegion::Iterator iterator;
};

class Bitmap;

struct GraphicsObjects final {
    Objects<std::shared_ptr<Bitmap>> bitmaps;
    Objects<SkMatrix> matrices;
    Objects<SkPathBuilder> paths;
    Objects<SkPathMeasure> measures;
    Objects<SkRegion> regions;
    Objects<RegionIterator> region_iterators;
};

}  // namespace ogplay::runtime::raster
