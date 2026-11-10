#include "canvas.h"

#include "include/core/SkImage.h"
#include "include/core/SkPathBuilder.h"
#include "ogplay/runtime/raster/bitmap.h"

#include <array>
#include <stdexcept>

namespace ogplay::runtime::raster {
namespace {
constexpr int kMatrixSave = 1;
constexpr int kClipSave = 2;
constexpr int kLayerAlpha = 4;
constexpr int kClipToLayer = 16;

SkColorType SurfaceColorType(int format) {
    switch (format) {
    case -3:
    case -2:
    case 1: return kRGBA_8888_SkColorType;
    case -1:
    case 2: return kRGB_888x_SkColorType;
    case 4: return kRGB_565_SkColorType;
    case 7: return kARGB_4444_SkColorType;
    default: throw std::invalid_argument("Surface format has no Skia raster device");
    }
}
}  // namespace

SkBlendMode PorterDuffBlend(int mode) {
    static constexpr std::array modes{
        SkBlendMode::kClear,   SkBlendMode::kSrc,     SkBlendMode::kDst,      SkBlendMode::kSrcOver,
        SkBlendMode::kDstOver, SkBlendMode::kSrcIn,   SkBlendMode::kDstIn,    SkBlendMode::kSrcOut,
        SkBlendMode::kDstOut,  SkBlendMode::kSrcATop, SkBlendMode::kDstATop,  SkBlendMode::kXor,
        SkBlendMode::kDarken,  SkBlendMode::kLighten, SkBlendMode::kModulate, SkBlendMode::kScreen,
        SkBlendMode::kPlus,    SkBlendMode::kOverlay,
    };
    return modes.at(static_cast<std::size_t>(mode));
}

Canvas::Canvas() = default;

Canvas::Canvas(std::shared_ptr<Bitmap> bitmap) : bitmap_(std::move(bitmap)) {}

Canvas::Canvas(int width, int height, int format) {
    const auto color_type = SurfaceColorType(format);
    const auto alpha_type = color_type == kRGB_888x_SkColorType || color_type == kRGB_565_SkColorType
                                ? kOpaque_SkAlphaType
                                : kPremul_SkAlphaType;
    const auto info = SkImageInfo::Make(width, height, color_type, alpha_type);
    // installPixels preserves the requested packed format, including ARGB4444.
    auto pixels = std::make_unique<std::vector<std::byte>>(info.computeMinByteSize());
    auto* data = pixels->data();
    if (!surface_.installPixels(
            info, data, info.minRowBytes(),
            +[](void*, void* owner) { delete static_cast<std::vector<std::byte>*>(owner); }, pixels.release()))
        throw std::bad_alloc{};
    SkCanvas canvas(surface_);
    canvas.clear(alpha_type == kOpaque_SkAlphaType ? SK_ColorBLACK : SK_ColorTRANSPARENT);
}

const SkBitmap& Canvas::Target() const {
    return bitmap_ ? bitmap_->SkiaBitmap() : surface_;
}

int Canvas::Width() const {
    return Target().width();
}

int Canvas::Height() const {
    return Target().height();
}

bool Canvas::IsOpaque() const {
    return !Target().empty() && Target().isOpaque();
}

int Canvas::SaveCount() const {
    return static_cast<int>(stack_.size()) + 1;
}

SkIRect Canvas::Extent() const {
    return SkIRect::MakeWH(Width(), Height());
}

void Canvas::ApplyClip(SkRegion& clip, const SkRegion& extent, const ClipCommand& command) {
    if (const auto* path = std::get_if<SkPath>(&command.shape)) {
        const auto limit = command.op == SkRegion::kIntersect_Op ? SkRegion(clip.getBounds()) : extent;
        SkRegion region;
        region.setPath(*path, limit);
        clip.op(region, command.op);
    } else {
        clip.op(std::get<SkRegion>(command.shape), command.op);
    }
}

const SkRegion& Canvas::DeviceClip() const {
    const auto bounds = Extent();
    if (!clip_current_ || clip_extent_ != bounds) {
        const SkRegion extent(bounds);
        SkRegion result(extent);
        for (const auto& command : clips_)
            ApplyClip(result, extent, command);
        device_clip_ = std::move(result);
        clip_extent_ = bounds;
        clip_current_ = true;
    }
    return device_clip_;
}

bool Canvas::Clip(Shape shape, SkRegion::Op op) {
    auto result = DeviceClip();
    const ClipCommand command{std::move(shape), op};
    ApplyClip(result, SkRegion(clip_extent_), command);
    if (op == SkRegion::kReplace_Op && !clips_.empty()) {
        clips_.front() = command;
        clips_.resize(1);
    } else {
        clips_.push_back(command);
    }
    device_clip_ = std::move(result);
    return !device_clip_.isEmpty();
}

bool Canvas::ClipRect(const SkRect& rect, SkRegion::Op op) {
    if (!rect.isFinite() || rect.isEmpty()) return Clip(SkRegion{}, op);
    if (matrix_.rectStaysRect()) {
        const auto mapped = matrix_.mapRect(rect);
        return Clip(mapped.isFinite() ? SkRegion(mapped.round()) : SkRegion{}, op);
    }
    return ClipPath(SkPath::Rect(rect), op);
}

bool Canvas::ClipPath(const SkPath& path, SkRegion::Op op) {
    const auto device = path.tryMakeTransform(matrix_);
    return Clip(device ? Shape(*device) : Shape(SkRegion{}), op);
}

bool Canvas::ClipRegion(const SkRegion& region, SkRegion::Op op) {
    return Clip(region, op);
}

bool Canvas::ClipBounds(SkIRect& bounds) const {
    const auto clip = DeviceClip();
    const auto inverse = matrix_.invert();
    if (clip.isEmpty() || !inverse) {
        bounds = SkIRect::MakeEmpty();
        return false;
    }
    bounds = inverse->mapRect(SkRect::Make(clip.getBounds())).round();
    return true;
}

bool Canvas::QuickReject(const SkRect& rect) const {
    const auto& clip = DeviceClip();
    if (!rect.isFinite() || clip.isEmpty()) return true;
    if (matrix_.hasPerspective()) {
        const auto mapped = matrix_.mapRect(rect);
        return !mapped.isFinite() || !SkIRect::Intersects(mapped.roundOut(), clip.getBounds());
    }
    // API19 compares against inverse-mapped local clip bounds with a one
    // device-pixel allowance for antialiasing.
    auto bounds = SkRect::Make(clip.getBounds());
    bounds.outset(1, 1);
    const auto inverse = matrix_.invert();
    const auto local = inverse ? inverse->mapRect(bounds) : SkRect::MakeEmpty();
    return rect.top() >= local.bottom() || rect.bottom() <= local.top() || rect.left() >= local.right() ||
           rect.right() <= local.left();
}

void Canvas::CopyState(const Canvas& source) {
    auto clips = source.clips_;
    matrix_ = source.matrix_;
    clips_ = std::move(clips);
    clip_current_ = false;
}

int Canvas::Save(int flags) {
    const auto count = SaveCount();
    stack_.push_back({flags, matrix_, flags & kClipSave ? clips_ : std::vector<ClipCommand>{}, nullptr, draw_filter_});
    return count;
}

int Canvas::SaveLayer(const SkRect* bounds, const SkPaint& paint, int flags) {
    auto device_bounds = DeviceClip().getBounds();
    if (bounds) {
        const auto mapped = matrix_.mapRect(*bounds);
        if (!mapped.isFinite() || !device_bounds.intersect(mapped.roundOut())) device_bounds = SkIRect::MakeEmpty();
    }
    std::unique_ptr<Layer> layer;
    auto raster_bounds = device_bounds;
    if (raster_bounds.intersect(Extent())) {
        layer = std::make_unique<Layer>(Layer{raster_bounds, {}, paint});
        const auto info = SkImageInfo::MakeN32(raster_bounds.width(), raster_bounds.height(),
                                               flags & kLayerAlpha ? kPremul_SkAlphaType : kOpaque_SkAlphaType);
        if (!layer->bitmap.tryAllocPixels(info)) throw std::bad_alloc{};
        SkCanvas canvas(layer->bitmap);
        canvas.clear(flags & kLayerAlpha ? SK_ColorTRANSPARENT : SK_ColorBLACK);
    }
    const auto count = Save(flags);
    try {
        if (flags & kClipToLayer) ClipRegion(SkRegion(device_bounds), SkRegion::kIntersect_Op);
    } catch (...) {
        stack_.pop_back();
        throw;
    }
    stack_.back().layer = std::move(layer);
    return count;
}

void Canvas::Restore() {
    auto frame = std::move(stack_.back());
    stack_.pop_back();
    draw_filter_ = std::move(frame.draw_filter);
    if (frame.flags & kMatrixSave) matrix_ = frame.matrix;
    if (frame.flags & kClipSave) {
        clips_ = std::move(frame.clips);
        clip_current_ = false;
    }
    if (!frame.layer) return;
    frame.layer->bitmap.setImmutable();
    const auto image = SkImages::RasterFromBitmap(frame.layer->bitmap);
    if (!image) throw std::bad_alloc{};
    DrawDevices(SkMatrix::I(), [&](SkCanvas& canvas) {
        canvas.drawImage(image, static_cast<float>(frame.layer->bounds.left()),
                         static_cast<float>(frame.layer->bounds.top()), {}, &frame.layer->paint);
    });
    if (bitmap_) bitmap_->SkiaBitmap().notifyPixelsChanged();
}

void Canvas::DrawDevices(const SkMatrix& matrix, const std::function<void(SkCanvas&)>& draw) {
    auto remaining = DeviceClip();
    if (!remaining.op(Extent(), SkRegion::kIntersect_Op)) {
        // Even an empty raster target receives the operation so native
        // argument validation stays independent of visibility.
        SkCanvas empty;
        draw(empty);
        return;
    }
    for (auto frame = stack_.rbegin(); frame != stack_.rend() && !remaining.isEmpty(); ++frame) {
        if (!frame->layer) continue;
        const auto& layer = *frame->layer;
        SkRegion clip;
        if (clip.op(remaining, layer.bounds, SkRegion::kIntersect_Op)) {
            const int x = -layer.bounds.left(), y = -layer.bounds.top();
            clip.translate(x, y);
            SkCanvas canvas(layer.bitmap);
            canvas.clipRegion(clip);
            auto local = matrix;
            local.postTranslate(static_cast<float>(x), static_cast<float>(y));
            canvas.setMatrix(local);
            draw(canvas);
        }
        remaining.op(layer.bounds, SkRegion::kDifference_Op);
    }
    if (!remaining.isEmpty()) {
        SkCanvas canvas(Target());
        canvas.clipRegion(remaining);
        canvas.setMatrix(matrix);
        draw(canvas);
    }
}

void Canvas::Draw(const std::function<void(SkCanvas&)>& draw) {
    DrawDevices(matrix_, draw);
    if (bitmap_) bitmap_->SkiaBitmap().notifyPixelsChanged();
}

std::vector<std::uint8_t> Canvas::ReadSurfacePixels() const {
    const auto info = SkImageInfo::Make(Width(), Height(), kRGBA_8888_SkColorType, kUnpremul_SkAlphaType);
    std::vector<std::uint8_t> rgba(info.computeMinByteSize());
    if (!surface_.readPixels(info, rgba.data(), info.minRowBytes(), 0, 0))
        throw std::logic_error("cannot read software Surface pixels");
    return rgba;
}

}  // namespace ogplay::runtime::raster
