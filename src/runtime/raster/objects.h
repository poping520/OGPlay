#pragma once

#include "include/core/SkMatrix.h"
#include "canvas.h"
#include "paint.h"
#include "effects.h"
#include "include/core/SkColorFilter.h"
#include "include/core/SkMaskFilter.h"
#include "include/core/SkPathEffect.h"
#include "include/utils/SkCamera.h"
#include "include/core/SkPathBuilder.h"
#include "include/core/SkPathMeasure.h"
#include "include/core/SkRegion.h"

#include <cstdint>
#include <map>
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
struct NinePatch;

struct Camera {
    Sk3DView view;
    SkCamera3D projection;
    // The standalone SDK omits Sk3DView's Android-only location methods.
    std::vector<SkM44> transforms{SkM44{}};

    void GetMatrix(SkMatrix& matrix) const;
};

struct GraphicsObjects final {
    Objects<Canvas> canvases;
    Objects<Camera> cameras;
    Objects<std::shared_ptr<const NinePatch>> nine_patches;
    Objects<std::shared_ptr<Shader>> shaders;
    Objects<sk_sp<SkColorFilter>> color_filters;
    Objects<sk_sp<SkMaskFilter>> mask_filters;
    Objects<sk_sp<SkPathEffect>> path_effects;
    Objects<SkBlendMode> xfermodes;
    Objects<Paint> paints;
    Objects<std::shared_ptr<const Typeface>> typefaces;
    // Built-in families are interned for Java Typeface.equals and its style cache.
    std::map<std::pair<int, int>, Token<std::shared_ptr<const Typeface>>> system_typefaces;
    Objects<std::shared_ptr<DrawFilterState>> draw_filters;
    Objects<std::shared_ptr<Bitmap>> bitmaps;
    Objects<SkMatrix> matrices;
    Objects<SkPathBuilder> paths;
    Objects<SkPathMeasure> measures;
    Objects<SkRegion> regions;
    Objects<RegionIterator> region_iterators;
};

}  // namespace ogplay::runtime::raster
