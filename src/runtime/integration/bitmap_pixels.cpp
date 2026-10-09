#include "ogplay/runtime/integration/bitmap_pixels.h"
#include <algorithm>
#include <array>
#include <limits>
#include <stdexcept>

namespace ogplay::runtime {
BitmapPixels::BitmapPixels(std::int32_t w, std::int32_t h, std::int32_t config,
                           std::vector<std::uint32_t> colors)
    : width_(static_cast<std::uint32_t>(w)), height_(static_cast<std::uint32_t>(h)),
      stride_(0), bytes_per_pixel_(0), byte_count_(0), format_(0), count_(0), colors_(std::move(colors)) {
    if (w <= 0 || h <= 0) throw std::invalid_argument("invalid bitmap dimensions");
    switch (config) {
    case 1: format_ = 8; bytes_per_pixel_ = 1; break;
    case 3: format_ = 4; bytes_per_pixel_ = 2; break;
    case 4: format_ = 7; bytes_per_pixel_ = 2; break;
    case 5: format_ = 1; bytes_per_pixel_ = 4; break;
    default: throw std::invalid_argument("unsupported bitmap format");
    }
    const auto row = (static_cast<std::uint64_t>(width_) * bytes_per_pixel_ + 3U) & ~UINT64_C(3);
    const auto size = row * height_;
    if (size > (std::numeric_limits<std::uint32_t>::max)()) throw std::length_error("bitmap byte size overflows");
    stride_ = static_cast<std::uint32_t>(row); byte_count_ = static_cast<std::uint32_t>(size);
    count_ = static_cast<std::size_t>(width_) * height_;
    if (colors_.empty()) colors_.resize(count_);
    if (colors_.size() != count_) throw std::invalid_argument("bitmap pixel count mismatch");
}
std::uint32_t BitmapPixels::Offset(std::size_t i) const {
    if (i >= count_) throw std::out_of_range("bitmap pixel index");
    return static_cast<std::uint32_t>(i / width_) * stride_ + static_cast<std::uint32_t>(i % width_) * bytes_per_pixel_;
}
void BitmapPixels::Encode(std::uint32_t c, std::span<std::byte> out) const {
    const auto a = c >> 24U;
    auto r = (c >> 16U) & 255U, g = (c >> 8U) & 255U, b = c & 255U;
    if (format_ == 1 || format_ == 7) { r = (r * a + 127U) / 255U; g = (g * a + 127U) / 255U; b = (b * a + 127U) / 255U; }
    if (format_ == 1) {
        out[0] = static_cast<std::byte>(r); out[1] = static_cast<std::byte>(g);
        out[2] = static_cast<std::byte>(b); out[3] = static_cast<std::byte>(a);
    } else if (format_ == 8) out[0] = static_cast<std::byte>(a);
    else {
        const auto value = format_ == 4 ? ((r >> 3U) << 11U | (g >> 2U) << 5U | (b >> 3U))
            : ((r >> 4U) << 12U | (g >> 4U) << 8U | (b >> 4U) << 4U | (a >> 4U));
        out[0] = static_cast<std::byte>(value); out[1] = static_cast<std::byte>(value >> 8U);
    }
}
std::uint32_t BitmapPixels::Decode(std::span<const std::byte> in) const {
    const auto v = [](std::byte b) { return std::to_integer<std::uint32_t>(b); };
    std::uint32_t a = 255, r{}, g{}, b{};
    if (format_ == 1) { r = v(in[0]); g = v(in[1]); b = v(in[2]); a = v(in[3]); }
    else if (format_ == 8) return v(in[0]) << 24U;
    else {
        const auto packed = v(in[0]) | v(in[1]) << 8U;
        if (format_ == 4) {
            const auto r5 = packed >> 11U, g6 = (packed >> 5U) & 63U, b5 = packed & 31U;
            r = r5 << 3U | r5 >> 2U; g = g6 << 2U | g6 >> 4U; b = b5 << 3U | b5 >> 2U;
        } else { r = ((packed >> 12U) & 15U) * 17U; g = ((packed >> 8U) & 15U) * 17U; b = ((packed >> 4U) & 15U) * 17U; a = (packed & 15U) * 17U; }
    }
    if (format_ == 1 || format_ == 7) {
        if (a == 0) return 0;
        r = std::min(255U, (r * 255U + a / 2U) / a);
        g = std::min(255U, (g * 255U + a / 2U) / a);
        b = std::min(255U, (b * 255U + a / 2U) / a);
    }
    return a << 24U | r << 16U | g << 8U | b;
}
std::uint32_t BitmapPixels::Get(std::size_t i) const {
    const auto offset = Offset(i);
    if (address_.IsNull()) return colors_.at(i);
    std::array<std::byte, 4> bytes{};
    access_.read(address_.Add(offset), std::span(bytes).first(bytes_per_pixel_));
    return Decode(bytes);
}
void BitmapPixels::Set(std::size_t i, std::uint32_t c) {
    const auto offset = Offset(i);
    if (address_.IsNull()) { colors_.at(i) = c; ++revision_; return; }
    std::array<std::byte, 4> bytes{};
    Encode(c, bytes);
    access_.write(address_.Add(offset), std::span(bytes).first(bytes_per_pixel_));
    ++revision_;
}
std::vector<std::uint32_t> BitmapPixels::Snapshot() const {
    if (address_.IsNull()) return colors_;
    std::vector<std::byte> bytes(byte_count_); access_.read(address_, bytes);
    std::vector<std::uint32_t> result(count_);
    for (std::size_t i = 0; i < count_; ++i)
        result[i] = Decode(std::span(bytes).subspan(Offset(i), bytes_per_pixel_));
    return result;
}
memory::GuestAddress BitmapPixels::Lock(const dexvm::NioDirectMemoryAccess& access) {
    if (retired_) throw std::logic_error("bitmap is recycled");
    if (locks_ == (std::numeric_limits<std::uint32_t>::max)()) throw std::overflow_error("bitmap lock count exhausted");
    if (locks_ == 0) {
        auto retained_access = access;
        std::vector<std::byte> bytes(byte_count_);
        for (std::size_t i = 0; i < count_; ++i) Encode(colors_[i], std::span(bytes).subspan(Offset(i), bytes_per_pixel_));
        memory::GuestAddress address;
        try { address = retained_access.allocate(byte_count_); }
        catch (const std::exception&) { throw std::bad_alloc(); }
        if (address.IsNull()) throw std::bad_alloc();
        try { retained_access.write(address, bytes); } catch (...) { retained_access.release(address, byte_count_); throw; }
        access_ = std::move(retained_access); address_ = address; colors_.clear();
    }
    ++locks_; return address_;
}
void BitmapPixels::Unlock() {
    if (locks_ == 0) throw std::logic_error("bitmap has no active pixel lock");
    if (locks_ > 1) { --locks_; return; }
    auto colors = retired_ ? std::vector<std::uint32_t>{} : Snapshot();
    access_.release(address_, byte_count_);
    colors_ = std::move(colors); address_ = memory::GuestAddress{}; access_ = {}; locks_ = 0;
    ++revision_;
}
void BitmapPixels::Retire() { retired_ = true; if (locks_ == 0) colors_.clear(); }
void BitmapPixels::AbortLocks() {
    if (!address_.IsNull()) access_.release(address_, byte_count_);
    address_ = memory::GuestAddress{}; access_ = {}; locks_ = 0; colors_.clear(); retired_ = true;
}
}
