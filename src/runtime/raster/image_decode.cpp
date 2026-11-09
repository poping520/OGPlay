#include "ogplay/runtime/raster/image_decode.h"

#include "include/codec/SkCodec.h"
#include "include/codec/SkEncodedImageFormat.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkBitmap.h"
#include "include/core/SkData.h"
#include "include/core/SkImage.h"

#include <memory>
#include <stdexcept>

namespace ogplay::runtime {
namespace {

std::unique_ptr<SkCodec> OpenCodec(std::span<const std::byte> bytes) {
    if (bytes.empty() || bytes.size() > kMaximumImageEncodedBytes) return {};
    return SkCodec::MakeFromData(SkData::MakeWithoutCopy(bytes.data(), bytes.size()));
}

const char* ImageMime(SkEncodedImageFormat format) {
    switch (format) {
    case SkEncodedImageFormat::kBMP: return "image/bmp";
    case SkEncodedImageFormat::kGIF: return "image/gif";
    case SkEncodedImageFormat::kICO: return "image/x-ico";
    case SkEncodedImageFormat::kJPEG: return "image/jpeg";
    case SkEncodedImageFormat::kPNG: return "image/png";
    case SkEncodedImageFormat::kWBMP: return "image/vnd.wap.wbmp";
    case SkEncodedImageFormat::kWEBP: return "image/webp";
    default: return nullptr;
    }
}

// A logical ARGB word in memory on the supported little-endian hosts.
SkImageInfo ArgbInfo(int width, int height) {
    return SkImageInfo::Make(width, height, kBGRA_8888_SkColorType, kUnpremul_SkAlphaType);
}

}  // namespace

bool ImageDimensionsSupported(std::int32_t width, std::int32_t height) noexcept {
    return width > 0 && height > 0 && width <= 8192 && height <= 8192 &&
           static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height) <= kMaximumImagePixels;
}

std::optional<ImageInfo> ProbeImage(std::span<const std::byte> bytes) {
    const auto codec = OpenCodec(bytes);
    if (!codec) return std::nullopt;
    return ImageInfo{codec->dimensions().width(), codec->dimensions().height(), ImageMime(codec->getEncodedFormat())};
}

DecodedImage ResizeArgbImage(DecodedImage source, std::int32_t width, std::int32_t height) {
    if (!ImageDimensionsSupported(width, height)) throw std::length_error("scaled image exceeds decode budget");
    if (source.width == width && source.height == height) return source;
    const auto input = ArgbInfo(source.width, source.height);
    const auto output = ArgbInfo(width, height);
    const auto image = SkImages::RasterFromPixmap({input, source.argb.data(), input.minRowBytes()}, nullptr, nullptr);
    // Filtering should interpolate premultiplied samples. Read back logical
    // ARGB for UI and legacy clients of this adapter.
    SkBitmap target;
    if (!image || !target.tryAllocPixels(output.makeAlphaType(kPremul_SkAlphaType))) throw std::bad_alloc{};
    SkCanvas canvas(target);
    canvas.clear(SK_ColorTRANSPARENT);
    canvas.drawImageRect(image, SkRect::MakeWH(static_cast<float>(width), static_cast<float>(height)),
                         SkSamplingOptions(SkFilterMode::kLinear));
    DecodedImage result{width, height,
                        std::vector<std::uint32_t>(static_cast<std::size_t>(width) * static_cast<std::size_t>(height))};
    if (!target.readPixels(output, result.argb.data(), output.minRowBytes(), 0, 0))
        throw std::logic_error("cannot read resized ARGB image");
    return result;
}

std::optional<DecodedImage> DecodeImageToArgb(std::span<const std::byte> bytes) {
    const auto codec = OpenCodec(bytes);
    if (!codec) return std::nullopt;
    const auto [width, height] = codec->dimensions();
    if (!ImageDimensionsSupported(width, height)) return std::nullopt;
    const auto info = ArgbInfo(width, height);
    DecodedImage result{width, height,
                        std::vector<std::uint32_t>(static_cast<std::size_t>(width) * static_cast<std::size_t>(height))};
    if (codec->getPixels(info, result.argb.data(), info.minRowBytes()) != SkCodec::kSuccess) return std::nullopt;
    return result;
}

}  // namespace ogplay::runtime
