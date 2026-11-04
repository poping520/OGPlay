#include "ogplay/app/running_icon.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>
#include <string>
#include <utility>

#include <stb_image_write.h>

#include "ogplay/runtime/integration/host_image_decode.h"

namespace ogplay::app {
namespace {

// Index arithmetic in size_t: every coordinate here is non-negative and inside its canvas.
[[nodiscard]] constexpr std::size_t U(const int value) {
    return static_cast<std::size_t>(value);
}

// Premultiplied float RGBA canvas; all compositing happens here and converts back once.
struct Canvas final {
    int width{};
    int height{};
    std::vector<float> pixels;  // r g b a, premultiplied, 0..1

    Canvas() = default;

    Canvas(const int w, const int h) : width(w), height(h), pixels(U(w) * U(h) * 4U, 0.0F) {}

    [[nodiscard]] float* At(const int x, const int y) { return &pixels[(U(y) * U(width) + U(x)) * 4U]; }

    [[nodiscard]] const float* At(const int x, const int y) const { return &pixels[(U(y) * U(width) + U(x)) * 4U]; }

    [[nodiscard]] float Alpha(const int x, const int y) const { return At(x, y)[3]; }
};

[[nodiscard]] Canvas Decode(const std::span<const std::byte> png, const char* what) {
    const auto decoded = runtime::DecodeImageToArgb(png);
    if (!decoded || decoded->width <= 0 || decoded->height <= 0)
        throw std::invalid_argument(std::string(what) + " is not a decodable image");
    Canvas canvas(decoded->width, decoded->height);
    for (int y = 0; y < canvas.height; ++y) {
        for (int x = 0; x < canvas.width; ++x) {
            const auto argb = decoded->argb[U(y) * U(canvas.width) + U(x)];
            const float a = static_cast<float>(argb >> 24U) / 255.0F;
            auto* p = canvas.At(x, y);
            p[0] = static_cast<float>((argb >> 16U) & 0xffU) / 255.0F * a;
            p[1] = static_cast<float>((argb >> 8U) & 0xffU) / 255.0F * a;
            p[2] = static_cast<float>(argb & 0xffU) / 255.0F * a;
            p[3] = a;
        }
    }
    return canvas;
}

[[nodiscard]] Canvas Crop(const Canvas& source, const int left, const int top, const int right, const int bottom) {
    Canvas out(right - left, bottom - top);
    for (int y = 0; y < out.height; ++y)
        std::copy_n(source.At(left, top + y), static_cast<std::size_t>(out.width) * 4U, out.At(0, y));
    return out;
}

[[nodiscard]] float Lanczos3(const float x) {
    if (x == 0.0F) return 1.0F;
    if (std::abs(x) >= 3.0F) return 0.0F;
    const float px = std::numbers::pi_v<float> * x;
    return 3.0F * std::sin(px) * std::sin(px / 3.0F) / (px * px);
}

// One separable Lanczos-3 pass along x (horizontal) or y; the kernel widens when reducing.
[[nodiscard]] Canvas ResamplePass(const Canvas& source, const int size, const bool horizontal) {
    const int from = horizontal ? source.width : source.height;
    Canvas out(horizontal ? size : source.width, horizontal ? source.height : size);
    const float scale = static_cast<float>(from) / static_cast<float>(size);
    const float stretch = std::max(scale, 1.0F);
    const float support = 3.0F * stretch;
    std::vector<float> weights;
    for (int i = 0; i < size; ++i) {
        const float center = (static_cast<float>(i) + 0.5F) * scale - 0.5F;
        const int first = static_cast<int>(std::floor(center - support)),
                  last = static_cast<int>(std::ceil(center + support));
        weights.assign(static_cast<std::size_t>(last - first + 1), 0.0F);
        float total = 0.0F;
        for (int k = first; k <= last; ++k)
            total += weights[static_cast<std::size_t>(k - first)] =
                Lanczos3((static_cast<float>(k) - center) / stretch);
        const int lines = horizontal ? source.height : source.width;
        for (int line = 0; line < lines; ++line) {
            float sum[4]{};
            for (int k = first; k <= last; ++k) {
                const int clamped = std::clamp(k, 0, from - 1);
                const float* p = horizontal ? source.At(clamped, line) : source.At(line, clamped);
                const float w = weights[static_cast<std::size_t>(k - first)] / total;
                for (int c = 0; c < 4; ++c)
                    sum[c] += p[c] * w;
            }
            float* q = horizontal ? out.At(i, line) : out.At(line, i);
            q[3] = std::clamp(sum[3], 0.0F, 1.0F);
            for (int c = 0; c < 3; ++c)
                q[c] = std::clamp(sum[c], 0.0F, q[3]);  // ringing stays premultiplied-valid
        }
    }
    return out;
}

[[nodiscard]] Canvas Resize(const Canvas& source, const int width, const int height) {
    return ResamplePass(ResamplePass(source, width, true), height, false);
}

// Coverage of a rounded rectangle [left, right) x [top, bottom) with circular corners, 4x4 supersampled.
[[nodiscard]] std::vector<float> RoundedRect(const int width, const int height, const float left, const float top,
                                             const float right, const float bottom, const float radius) {
    std::vector<float> mask(U(width) * U(height), 0.0F);
    const auto inside = [&](const float x, const float y) {
        if (x < left || x >= right || y < top || y >= bottom) return false;
        const float cx = std::clamp(x, left + radius, right - radius),
                    cy = std::clamp(y, top + radius, bottom - radius);
        return (x - cx) * (x - cx) + (y - cy) * (y - cy) <= radius * radius;
    };
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) {
            int hits = 0;
            for (int sy = 0; sy < 4; ++sy)
                for (int sx = 0; sx < 4; ++sx)
                    hits += inside(static_cast<float>(x) + (static_cast<float>(sx) + 0.5F) / 4.0F,
                                   static_cast<float>(y) + (static_cast<float>(sy) + 0.5F) / 4.0F);
            mask[U(y) * U(width) + U(x)] = static_cast<float>(hits) / 16.0F;
        }
    return mask;
}

void ApplyMask(Canvas& canvas, const std::vector<float>& mask) {
    for (std::size_t i = 0; i < mask.size(); ++i)
        for (int c = 0; c < 4; ++c)
            canvas.pixels[i * 4U + U(c)] *= mask[i];
}

// Source-over at an integer offset; both premultiplied.
void Over(Canvas& target, const Canvas& layer, const int left, const int top) {
    for (int y = std::max(0, -top); y < layer.height && top + y < target.height; ++y)
        for (int x = std::max(0, -left); x < layer.width && left + x < target.width; ++x) {
            const float* s = layer.At(x, y);
            float* d = target.At(left + x, top + y);
            for (int c = 0; c < 4; ++c)
                d[c] = s[c] + d[c] * (1.0F - s[3]);
        }
}

// Box blur of one channel buffer; three passes approximate a Gaussian of `sigma`.
void BoxBlur(std::vector<float>& values, const int width, const int height, const float sigma) {
    const int radius = std::max(1, static_cast<int>(std::lround(std::sqrt(4.0F * sigma * sigma + 1.0F) / 2.0F)));
    std::vector<float> line;
    for (int pass = 0; pass < 6; ++pass) {
        const bool horizontal = pass % 2 == 0;
        const int length = horizontal ? width : height, lines = horizontal ? height : width;
        line.resize(static_cast<std::size_t>(length));
        for (int l = 0; l < lines; ++l) {
            const auto at = [&](const int i) -> float& {
                return values[horizontal ? U(l) * U(width) + U(i) : U(i) * U(width) + U(l)];
            };
            for (int i = 0; i < length; ++i)
                line[static_cast<std::size_t>(i)] = at(i);
            float sum = 0.0F;
            for (int i = -radius; i <= radius; ++i)
                sum += (i >= 0 && i < length) ? line[static_cast<std::size_t>(i)] : 0.0F;
            for (int i = 0; i < length; ++i) {
                at(i) = sum / static_cast<float>(2 * radius + 1);
                const int add = i + radius + 1, drop = i - radius;
                if (add < length) sum += line[static_cast<std::size_t>(add)];
                if (drop >= 0) sum -= line[static_cast<std::size_t>(drop)];
            }
        }
    }
}

// Soft black drop shadow of the layer, drawn under it.
void DropShadow(Canvas& target, const Canvas& layer, const float sigma, const int dy, const float opacity) {
    std::vector<float> alpha(U(layer.width) * U(layer.height));
    for (std::size_t i = 0; i < alpha.size(); ++i)
        alpha[i] = layer.pixels[i * 4U + 3U];
    BoxBlur(alpha, layer.width, layer.height, sigma);
    Canvas shadow(layer.width, layer.height);
    for (std::size_t i = 0; i < alpha.size(); ++i)
        shadow.pixels[i * 4U + 3U] = alpha[i] * opacity;
    Over(target, shadow, 0, dy);
}

// Windows taskbar icons need less padding than the macOS Dock grid.
#if defined(_WIN32)
constexpr float kPlateInset = 44.0F / 1024.0F, kPlateRadius = 185.0F / 824.0F;
#else
// macOS icon grid: an 824/1024 body with a 185/1024 corner radius (the plate of the OGPlay icon itself).
constexpr float kPlateInset = 100.0F / 1024.0F, kPlateRadius = 185.0F / 824.0F;
#endif

// The part of the game icon that fills the square: margins and soft shadows trimmed, zoomed until no
// transparent pixel is left inside the squircle (checked at 128 px, up to 20%). Size independent, so every
// icon size resamples the same crop.
[[nodiscard]] Canvas SquareArt(const Canvas& icon) {
    int left = icon.width, top = icon.height, right = -1, bottom = -1;
    for (int y = 0; y < icon.height; ++y)
        for (int x = 0; x < icon.width; ++x)
            if (icon.Alpha(x, y) > 200.0F / 255.0F) {
                left = std::min(left, x);
                right = std::max(right, x);
                top = std::min(top, y);
                bottom = std::max(bottom, y);
            }
    if (right < 0) throw std::invalid_argument("game icon has no opaque pixels");
    const Canvas trimmed = Crop(icon, left, top, right + 1, bottom + 1);
    constexpr int kProbe = 128;
    const auto probe = RoundedRect(kProbe, kProbe, 0.0F, 0.0F, kProbe, kProbe, kProbe * kPlateRadius);
    Canvas art;
    for (int step = 0; step <= 20; ++step) {
        const int mx = trimmed.width * step / 100, my = trimmed.height * step / 100;
        art = Crop(trimmed, mx, my, trimmed.width - mx, trimmed.height - my);
        const Canvas small = Resize(art, kProbe, kProbe);
        bool holes = false;
        for (std::size_t i = 0; i < probe.size() && !holes; ++i)
            holes = probe[i] > 0.0F && small.pixels[i * 4U + 3U] < 200.0F / 255.0F;
        if (!holes) break;
    }
    return art;
}

[[nodiscard]] Canvas Plated(const Canvas& art, const int n) {
    const float inset = n * kPlateInset, body = n - 2.0F * inset;
    const int side = static_cast<int>(std::lround(body));
    Canvas layer(n, n);
    Over(layer, Resize(art, side, side), static_cast<int>(std::lround(inset)), static_cast<int>(std::lround(inset)));
    ApplyMask(layer, RoundedRect(n, n, inset, inset, n - inset, n - inset, body * kPlateRadius));
    Canvas canvas(n, n);
    DropShadow(canvas, layer, n * 10.0F / 512.0F, static_cast<int>(std::lround(n * 8.0F / 512.0F)), 0.35F);
    Over(canvas, layer, 0, 0);
    return canvas;
}

[[nodiscard]] Canvas Badge(const Canvas& art, const Canvas& cartridge, const int n) {
    Canvas canvas = Plated(art, n);
#if defined(_WIN32)
    const int size = static_cast<int>(std::lround(n * 0.42F));
#else
    const int size = static_cast<int>(std::lround(n * 0.40F));
#endif
    Canvas disc(size, size);
    const auto round = RoundedRect(size, size, 0.0F, 0.0F, static_cast<float>(size), static_cast<float>(size), size / 2.0F);
    for (std::size_t i = 0; i < round.size(); ++i)
        for (int c = 0; c < 4; ++c)
            disc.pixels[i * 4U + U(c)] = round[i];
    const int mark = static_cast<int>(std::lround(size * 0.72F));
    Over(disc, Resize(cartridge, mark, mark), (size - mark) / 2, (size - mark) / 2 + size / 40);
    const float inset = n * kPlateInset;
    Canvas layer(n, n);
#if defined(_WIN32)
    // Keep the full badge inside the canvas as the game plate grows towards the edges.
    const int offset = static_cast<int>(std::lround(std::min(n - inset - size * 0.80F, n - size - n / 64.0F)));
    Over(layer, disc, offset, offset);
#else
    Over(layer, disc, static_cast<int>(std::lround(n - inset - size * 0.80F)),
         static_cast<int>(std::lround(n - inset - size * 0.80F)));
#endif
    DropShadow(canvas, layer, n * 8.0F / 512.0F, static_cast<int>(std::lround(n * 5.0F / 512.0F)), 0.45F);
    Over(canvas, layer, 0, 0);
    return canvas;
}

// The blue base of the OGPlay cartridge with the yellow cartridge keyed out (rows 156..256 of the 256 mark).
[[nodiscard]] Canvas Cradle(const Canvas& cartridge) {
    const int top = cartridge.height * 156 / 256;
    Canvas base = Crop(cartridge, 0, top, cartridge.width, cartridge.height);
    const int keyed = cartridge.height * 40 / 256;
    for (int y = 0; y < std::min(keyed, base.height); ++y)
        for (int x = 0; x < base.width; ++x) {
            float* p = base.At(x, y);
            if (p[3] <= 0.0F) continue;
            const float r = p[0] / p[3], g = p[1] / p[3], b = p[2] / p[3];
            if (r > 150.0F / 255.0F && g > 90.0F / 255.0F && b < r * 0.62F) std::fill_n(p, 4, 0.0F);
        }
    return base;
}

[[nodiscard]] Canvas Inserted(const Canvas& art, const Canvas& cartridge, const int n) {
    const Canvas cradle = Cradle(cartridge);
#if defined(_WIN32)
    const int width = static_cast<int>(std::lround(n * 0.98F));
#else
    const int width = static_cast<int>(std::lround(n * 0.86F));
#endif
    const int height = static_cast<int>(std::lround(static_cast<float>(cradle.height) * width / cradle.width));
    const Canvas base = Resize(cradle, width, height);
    const int bx = (n - width) / 2, by = n - height - static_cast<int>(std::lround(n * 0.05F));
    const int side = static_cast<int>(std::lround(width * 0.74F));
    Canvas card = Resize(art, side, side);
    ApplyMask(card, RoundedRect(side, side, 0.0F, 0.0F, static_cast<float>(side), static_cast<float>(side), side * kPlateRadius));
    // Back rim behind the card, front face over its lower edge: the card reads as inserted into the base.
    const int front_top = static_cast<int>(std::lround(height * 0.30F));
    Canvas layer(n, n);
    Over(layer, Crop(base, 0, 0, width, front_top), bx, by);
    Over(layer, card, (n - side) / 2, by + static_cast<int>(std::lround(height * 0.55F)) - side);
    Over(layer, Crop(base, 0, front_top, width, height), bx, by + front_top);
    Canvas canvas(n, n);
    DropShadow(canvas, layer, n * 10.0F / 512.0F, static_cast<int>(std::lround(n * 8.0F / 512.0F)), 0.35F);
    Over(canvas, layer, 0, 0);
    return canvas;
}

void AppendPngBytes(void* context, void* data, const int size) noexcept {
    auto& output = *static_cast<std::vector<std::byte>*>(context);
    try {
        const auto* begin = static_cast<const std::byte*>(data);
        output.insert(output.end(), begin, begin + size);
    } catch (...) {
        output.clear();
    }
}

}  // namespace

std::optional<RunningIconStyle> ParseRunningIconStyle(const std::string_view name) {
    if (name == "cradle") return RunningIconStyle::cradle;
    if (name == "badge") return RunningIconStyle::badge;
    return std::nullopt;
}

std::string_view RunningIconStyleName(const RunningIconStyle style) {
    return style == RunningIconStyle::badge ? "badge" : "cradle";
}

std::vector<RgbaImage> ComposeRunningIcons(const std::span<const std::byte> game_icon_png,
                                           const std::span<const std::byte> cartridge_png, const RunningIconStyle style,
                                           const std::span<const std::uint32_t> sizes) {
    if (sizes.empty()) throw std::invalid_argument("running icon needs at least one size");
    for (const auto size : sizes)
        if (size < 16 || size > 1024) throw std::invalid_argument("running icon size must be 16..1024");
    const Canvas art = SquareArt(Decode(game_icon_png, "game icon"));
    const Canvas cartridge = Decode(cartridge_png, "OGPlay cartridge");
    std::vector<RgbaImage> images;
    for (const auto size : sizes) {
        const int n = static_cast<int>(size);
        const Canvas canvas = style == RunningIconStyle::badge ? Badge(art, cartridge, n) : Inserted(art, cartridge, n);
        RgbaImage& image = images.emplace_back(
            RgbaImage{size, size, std::vector<std::uint8_t>(static_cast<std::size_t>(size) * size * 4U)});
        for (std::size_t i = 0; i < canvas.pixels.size() / 4U; ++i) {
            const float* p = &canvas.pixels[i * 4U];
            const float a = std::clamp(p[3], 0.0F, 1.0F);
            for (int c = 0; c < 3; ++c)
                image.pixels[i * 4U + U(c)] =
                    static_cast<std::uint8_t>(std::lround(a > 0.0F ? std::clamp(p[c] / a, 0.0F, 1.0F) * 255.0F : 0.0F));
            image.pixels[i * 4U + 3U] = static_cast<std::uint8_t>(std::lround(a * 255.0F));
        }
    }
    return images;
}

RgbaImage ComposeRunningIcon(const std::span<const std::byte> game_icon_png,
                             const std::span<const std::byte> cartridge_png, const RunningIconStyle style,
                             const std::uint32_t size) {
    const std::uint32_t sizes[]{size};
    return std::move(ComposeRunningIcons(game_icon_png, cartridge_png, style, sizes).front());
}

std::vector<std::byte> EncodeRgbaPng(const RgbaImage& image) {
    if (image.width == 0 || image.height == 0 ||
        image.pixels.size() != static_cast<std::size_t>(image.width) * image.height * 4U)
        return {};
    std::vector<std::byte> output;
    if (stbi_write_png_to_func(AppendPngBytes, &output, static_cast<int>(image.width), static_cast<int>(image.height),
                               4, image.pixels.data(), static_cast<int>(image.width * 4U)) == 0)
        return {};
    return output;
}

}  // namespace ogplay::app
