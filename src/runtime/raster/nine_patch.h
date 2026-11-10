#pragma once

#include "include/core/SkBitmap.h"
#include "include/core/SkPaint.h"
#include "include/core/SkRegion.h"
#include "include/core/SkSamplingOptions.h"

#include <span>
#include <vector>

class SkCanvas;

namespace ogplay::runtime::raster {

struct NinePatch {
    static NinePatch Decode(std::span<const std::byte> chunk, int width, int height);
    void Draw(SkCanvas* canvas, const SkRect& bounds, const SkBitmap& bitmap, const SkPaint& paint,
              const SkSamplingOptions& sampling, SkRegion* transparent = nullptr) const;

    std::vector<int> x, y;
    std::vector<SkColor> colors;
};

}  // namespace ogplay::runtime::raster
