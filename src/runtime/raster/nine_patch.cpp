#include "nine_patch.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkImage.h"

#include <cstring>
#include <stdexcept>

namespace ogplay::runtime::raster {
namespace {

int Word(std::span<const std::byte> bytes, std::size_t offset) {
    int value;
    std::memcpy(&value, bytes.data() + offset, sizeof(value));
    return value;
}

struct Segment {
    int source_start, source_end;
    float start, end;
};

std::vector<Segment> Segments(const std::vector<int>& divisions, int size, float start, float end) {
    int stretchy = 0;
    for (std::size_t i = 0; i < divisions.size(); i += 2)
        stretchy += divisions[i + 1] - divisions[i];
    int fixed = size - stretchy;
    std::vector<Segment> result;
    int source = 0;
    bool stretch = divisions.front() == 0;
    for (std::size_t i = stretch ? 1 : 0; i <= divisions.size() && source < size; ++i, stretch = !stretch) {
        const int next = i == divisions.size() ? size : divisions[i];
        const int length = next - source;
        float limit = end;
        if (i != divisions.size()) {
            if (stretch) {
                limit = start + (end - start - static_cast<float>(fixed)) * static_cast<float>(length) /
                                    static_cast<float>(stretchy);
                stretchy -= length;
            } else {
                limit = start + static_cast<float>(length);
                fixed -= length;
            }
        }
        result.push_back({source, next, start, limit});
        source = next;
        start = limit;
    }
    return result;
}

}  // namespace

NinePatch NinePatch::Decode(std::span<const std::byte> chunk, int width, int height) {
    if (chunk.size() < 32) throw std::invalid_argument("Array too small for chunk.");
    const auto nx = std::to_integer<std::size_t>(chunk[1]);
    const auto ny = std::to_integer<std::size_t>(chunk[2]);
    const auto nc = std::to_integer<std::size_t>(chunk[3]);
    if (!nx || !ny || nx % 2 || ny % 2 || chunk.size() < 32 + 4 * (nx + ny + nc))
        throw std::invalid_argument("invalid NinePatch chunk dimensions");
    NinePatch patch;
    std::size_t offset = 32;
    const auto axis = [&](std::vector<int>& values, std::size_t count, int size) {
        for (std::size_t i = 0; i < count; ++i, offset += 4) {
            const auto value = Word(chunk, offset);
            if (value < 0 || value > size || (!values.empty() && value <= values.back()))
                throw std::invalid_argument("invalid NinePatch divisions");
            values.push_back(value);
        }
    };
    axis(patch.x, nx, width);
    axis(patch.y, ny, height);
    const auto columns = nx + 1 - (patch.x.front() == 0) - (patch.x.back() == width);
    const auto rows = ny + 1 - (patch.y.front() == 0) - (patch.y.back() == height);
    if (nc != columns * rows) throw std::invalid_argument("invalid NinePatch color count");
    for (std::size_t i = 0; i < nc; ++i, offset += 4)
        patch.colors.push_back(static_cast<SkColor>(Word(chunk, offset)));
    return patch;
}

void NinePatch::Draw(SkCanvas* canvas, const SkRect& bounds, const SkBitmap& bitmap, const SkPaint& paint,
                     const SkSamplingOptions& sampling, SkRegion* transparent) const {
    if (bounds.isEmpty() || !bitmap.getPixels()) return;
    if (paint.getBlendMode_or(SkBlendMode::kSrcOver) == SkBlendMode::kSrcOver && paint.getAlpha() == 0) return;
    const auto columns = Segments(x, bitmap.width(), bounds.left(), bounds.right());
    const auto rows = Segments(y, bitmap.height(), bounds.top(), bounds.bottom());
    const auto image = canvas ? SkImages::RasterFromBitmap(bitmap) : nullptr;
    if (canvas && !image) throw std::bad_alloc{};
    std::size_t index = 0;
    for (const auto& row : rows) {
        for (const auto& column : columns) {
            const auto color = colors[index++];
            const auto dest = SkRect::MakeLTRB(column.start, row.start, column.end, row.end);
            if (dest.isEmpty() || column.source_start == column.source_end || row.source_start == row.source_end)
                continue;
            if (color == 0 && paint.getBlendMode_or(SkBlendMode::kSrcOver) == SkBlendMode::kSrcOver) {
                if (transparent) transparent->op(dest.round(), SkRegion::kUnion_Op);
                continue;
            }
            if (!canvas) continue;
            if (color != 1) {
                auto fill = paint;
                const auto scale = static_cast<unsigned>(paint.getAlpha() + (paint.getAlpha() >> 7));
                fill.setColor(SkColorSetA(color, (SkColorGetA(color) * scale) >> 8));
                canvas->drawRect(dest, fill);
            } else if (column.source_end - column.source_start == 1 && row.source_end - row.source_start == 1) {
                auto fill = paint;
                fill.setColor(bitmap.getColor(column.source_start, row.source_start));
                canvas->drawRect(dest, fill);
            } else {
                const auto source = SkRect::Make(
                    SkIRect::MakeLTRB(column.source_start, row.source_start, column.source_end, row.source_end));
                canvas->drawImageRect(image, source, dest, sampling, &paint, SkCanvas::kStrict_SrcRectConstraint);
            }
        }
    }
}

}  // namespace ogplay::runtime::raster
