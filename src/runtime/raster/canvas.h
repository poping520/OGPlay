#pragma once

#include "include/core/SkBitmap.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkPaint.h"
#include "include/core/SkPath.h"
#include "include/core/SkRegion.h"

#include <functional>
#include <memory>
#include <variant>
#include <vector>

namespace ogplay::runtime::raster {

class Bitmap;

struct DrawFilterState {
    int clear_bits;
    int set_bits;
};

// The API19 state is independent of SkCanvas's intersect-only clip stack.
// Every raster device sees its portion of the current hard clip on each draw.
class Canvas final {
public:
    Canvas();
    explicit Canvas(std::shared_ptr<Bitmap> bitmap);
    Canvas(int width, int height, int format);

    [[nodiscard]] const std::shared_ptr<Bitmap>& Backing() const { return bitmap_; }

    [[nodiscard]] int Width() const;
    [[nodiscard]] int Height() const;
    [[nodiscard]] bool IsOpaque() const;
    [[nodiscard]] int SaveCount() const;

    [[nodiscard]] SkMatrix& Matrix() { return matrix_; }

    [[nodiscard]] const DrawFilterState* DrawFilter() const { return draw_filter_.get(); }

    void SetDrawFilter(std::shared_ptr<DrawFilterState> filter) { draw_filter_ = std::move(filter); }

    int Save(int flags);
    int SaveLayer(const SkRect* bounds, const SkPaint& paint, int flags);
    void Restore();
    void CopyState(const Canvas& source);
    bool ClipRect(const SkRect& rect, SkRegion::Op op);
    bool ClipPath(const SkPath& path, SkRegion::Op op);
    bool ClipRegion(const SkRegion& region, SkRegion::Op op);
    [[nodiscard]] bool ClipBounds(SkIRect& bounds) const;
    [[nodiscard]] bool QuickReject(const SkRect& rect) const;

    void Draw(const std::function<void(SkCanvas&)>& draw);
    [[nodiscard]] std::vector<std::uint8_t> ReadSurfacePixels() const;

private:
    using Shape = std::variant<SkRegion, SkPath>;

    struct ClipCommand {
        Shape shape;
        SkRegion::Op op;
    };

    struct Layer {
        SkIRect bounds;
        SkBitmap bitmap;
        SkPaint paint;
    };

    struct Frame {
        int flags;
        SkMatrix matrix;
        std::vector<ClipCommand> clips;
        std::unique_ptr<Layer> layer;
        std::shared_ptr<DrawFilterState> draw_filter;
    };

    [[nodiscard]] const SkBitmap& Target() const;
    [[nodiscard]] SkIRect Extent() const;
    [[nodiscard]] const SkRegion& DeviceClip() const;
    static void ApplyClip(SkRegion& clip, const SkRegion& extent, const ClipCommand& command);
    bool Clip(Shape shape, SkRegion::Op op);
    void DrawDevices(const SkMatrix& matrix, const std::function<void(SkCanvas&)>& draw);

    std::shared_ptr<Bitmap> bitmap_;
    SkBitmap surface_;
    SkMatrix matrix_ = SkMatrix::I();
    std::shared_ptr<DrawFilterState> draw_filter_;
    std::vector<ClipCommand> clips_;
    std::vector<Frame> stack_;
    mutable SkIRect clip_extent_ = SkIRect::MakeEmpty();
    mutable SkRegion device_clip_;
    mutable bool clip_current_{};
};

[[nodiscard]] SkBlendMode PorterDuffBlend(int mode);

}  // namespace ogplay::runtime::raster
