#include "bitmap_decode.h"

#include "SkAndroidCodec.h"
#include "SkPngChunkReader.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkData.h"
#include "include/core/SkImage.h"
#include "ogplay/runtime/raster/image_decode.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <limits>
#include <string_view>

namespace ogplay::runtime::raster {
namespace {

std::uint32_t BigWord(std::span<const std::byte> bytes, std::size_t offset) {
    std::uint32_t word{};
    for (std::size_t i = 0; i < 4; ++i)
        word = word << 8U | std::to_integer<std::uint32_t>(bytes[offset + i]);
    return word;
}

int Word(std::span<const std::byte> bytes, std::size_t offset) {
    int value;
    std::memcpy(&value, bytes.data() + offset, sizeof(value));
    return value;
}

void PutWord(std::span<std::byte> bytes, std::size_t offset, int value) {
    std::memcpy(bytes.data() + offset, &value, sizeof(value));
}

class PngChunks final : public SkPngChunkReader {
public:
    bool readChunk(const char tag[], const void* data, std::size_t size) override {
        const auto bytes = std::span(static_cast<const std::byte*>(data), size);
        if (std::string_view(tag) == "npLb" && size == 16) {
            layout = {Word(bytes, 0), Word(bytes, 4), Word(bytes, 8), Word(bytes, 12)};
        } else if (std::string_view(tag) == "npTc" && size >= 32) {
            const auto x = std::to_integer<std::size_t>(bytes[1]);
            const auto y = std::to_integer<std::size_t>(bytes[2]);
            const auto colors = std::to_integer<std::size_t>(bytes[3]);
            if (size != 32 + 4 * (x + y + colors) || x == 0 || y == 0 || x % 2 || y % 2) return false;
            patch.assign(bytes.begin(), bytes.end());
            patch[0] = std::byte{1};
            PutWord(patch, 4, 32);
            PutWord(patch, 8, static_cast<int>(32 + 4 * x));
            PutWord(patch, 28, static_cast<int>(32 + 4 * (x + y)));
            for (std::size_t i = 12; i < size; i += 4) {
                if (i == 28) continue;
                PutWord(patch, i, std::bit_cast<int>(BigWord(bytes, i)));
            }
        }
        return true;
    }

    std::optional<std::array<int, 4>> layout;
    std::vector<std::byte> patch;
};

int ScaleCoord(int x, double factor) {
    const auto scaled = std::trunc(static_cast<double>(x) * factor + 0.5);
    return static_cast<int>(std::clamp(scaled, static_cast<double>(std::numeric_limits<int>::min()),
                                       static_cast<double>(std::numeric_limits<int>::max())));
}

const char* Mime(SkEncodedImageFormat format) {
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
}  // namespace

std::optional<DecodedBitmap> DecodeBitmap(std::span<const std::byte> bytes, const DecodeOptions& options) {
    if (bytes.size() > kMaximumImageEncodedBytes) throw std::bad_alloc{};
    if (bytes.empty()) return std::nullopt;
    const auto chunks = sk_make_sp<PngChunks>();
    auto codec = SkAndroidCodec::MakeFromData(SkData::MakeWithoutCopy(bytes.data(), bytes.size()), chunks.get());
    if (!codec) return std::nullopt;
    const auto sample = std::max(1, options.sample);
    const auto dims = codec->getSampledDimensions(sample);
    const auto [width, height] = dims;
    const auto check = [](int w, int h) {
        if (!ImageDimensionsSupported(w, h)) throw std::bad_alloc{};
    };
    check(width, height);
    DecodedBitmap result;
    const auto scale = options.only_bounds ? 1.0 : options.scale;
    result.width = ScaleCoord(width, scale);
    result.height = ScaleCoord(height, scale);
    check(result.width, result.height);
    result.mime = Mime(codec->getEncodedFormat());
    if (options.only_bounds) return result;

    const auto alpha = codec->computeOutputAlphaType(!options.premultiplied);
    result.config = options.config;
    if (!chunks->patch.empty() || (result.config == 3 && alpha != kOpaque_SkAlphaType)) result.config = 5;
    auto color = kRGBA_8888_SkColorType;
    if (result.config == 1) color = kAlpha_8_SkColorType;
    if (result.config == 3) color = kRGB_565_SkColorType;
    if (result.config == 4) color = kARGB_4444_SkColorType;
    // Decode into the codec's supported 32-bit form before low-bit conversion.
    SkBitmap decoded;
    const auto base = SkImageInfo::Make(width, height, kRGBA_8888_SkColorType, alpha);
    if (!decoded.tryAllocPixels(base)) throw std::bad_alloc{};
    SkAndroidCodec::AndroidOptions request;
    request.fSampleSize = sample;
    const auto status = codec->getAndroidPixels(base, decoded.getPixels(), decoded.rowBytes(), &request);
    if (status != SkCodec::kSuccess && status != SkCodec::kIncompleteInput) return std::nullopt;

    if (width != result.width || height != result.height) {
        const auto dest = SkImageInfo::Make(result.width, result.height, kRGBA_8888_SkColorType,
                                            alpha == kOpaque_SkAlphaType ? alpha : kPremul_SkAlphaType);
        SkBitmap scaled;
        if (!scaled.tryAllocPixels(dest)) throw std::bad_alloc{};
        const auto image = SkImages::RasterFromBitmap(decoded);
        if (!image) return std::nullopt;
        SkCanvas canvas(scaled);
        SkPaint paint;
        paint.setBlendMode(SkBlendMode::kSrc);
        canvas.drawImageRect(image, SkRect::MakeWH(static_cast<float>(result.width), static_cast<float>(result.height)),
                             SkSamplingOptions(SkFilterMode::kLinear), &paint);
        decoded = std::move(scaled);
    }
    const auto final = SkImageInfo::Make(result.width, result.height, color, alpha);
    if (decoded.info() == final) {
        result.pixels = std::move(decoded);
    } else {
        if (!result.pixels.tryAllocPixels(final)) throw std::bad_alloc{};
        if (!decoded.readPixels(result.pixels.pixmap())) return std::nullopt;
    }
    result.nine_patch = std::move(chunks->patch);
    result.layout_bounds = chunks->layout;
    if (!result.nine_patch.empty()) {
        auto patch = std::span(result.nine_patch);
        for (const auto offset : {12U, 16U, 20U, 24U})
            PutWord(patch, offset, ScaleCoord(Word(patch, offset), scale));
        std::size_t offset = 32;
        for (const auto axis : {patch[1], patch[2]}) {
            int previous = -1;
            for (std::size_t i = 0; i < std::to_integer<std::size_t>(axis); ++i, offset += 4) {
                auto coord = ScaleCoord(Word(patch, offset), scale);
                if (i && coord == previous && coord < std::numeric_limits<int>::max()) ++coord;
                PutWord(patch, offset, coord);
                previous = coord;
            }
        }
        result.padding = {Word(patch, 12), Word(patch, 20), Word(patch, 16), Word(patch, 24)};
    }
    if (result.layout_bounds)
        for (auto& coord : *result.layout_bounds)
            coord = ScaleCoord(coord, scale);
    return result;
}
}  // namespace ogplay::runtime::raster
