#include "ogplay/runtime/raster/bitmap.h"

#include "include/core/SkBitmap.h"

#include <limits>
#include <stdexcept>

namespace ogplay::runtime::raster {
namespace {
SkColorType ColorType(int config) {
    switch (config) {
    case 1: return kAlpha_8_SkColorType;
    case 3: return kRGB_565_SkColorType;
    case 4: return kARGB_4444_SkColorType;
    case 5: return kRGBA_8888_SkColorType;
    default: throw std::invalid_argument("unknown Bitmap.Config");
    }
}

SkAlphaType AlphaType(int config, bool alpha, bool premultiplied) {
    if (!alpha || config == 3) return kOpaque_SkAlphaType;
    return premultiplied ? kPremul_SkAlphaType : kUnpremul_SkAlphaType;
}

SkImageInfo Argb(int width, int height) {
    return SkImageInfo::Make(width, height, kBGRA_8888_SkColorType, kUnpremul_SkAlphaType);
}
}  // namespace

class Bitmap::Impl final {
public:
    SkBitmap bitmap;
    std::span<std::byte> allocation;
    std::shared_ptr<void> lease;
    memory::GuestAddress address;
    int config{};
    std::uint32_t locks{}, stride{};
    bool retired{};
    bool premultiplied{true};
};

Bitmap::Bitmap(int width, int height, int config, bool premultiplied, bool alpha, std::span<std::byte> pixels,
               std::shared_ptr<void> lease, memory::GuestAddress guest_address)
    : impl_(std::make_unique<Impl>()) {
    impl_->allocation = pixels;
    impl_->lease = std::move(lease);
    impl_->address = guest_address;
    Reconfigure(width, height, config, alpha, premultiplied);
}

Bitmap::~Bitmap() = default;

std::size_t Bitmap::AllocationSize(int width, int height, int config) {
    if (width <= 0 || height <= 0) throw std::invalid_argument("invalid Bitmap dimensions");
    const auto info = SkImageInfo::Make(width, height, ColorType(config), kPremul_SkAlphaType);
    const auto size = info.computeMinByteSize();
    if (size > static_cast<std::size_t>(std::numeric_limits<int>::max())) throw std::bad_alloc{};
    return size;
}

std::size_t Bitmap::Size() const noexcept {
    return static_cast<std::size_t>(impl_->bitmap.width()) * static_cast<std::size_t>(impl_->bitmap.height());
}

std::uint32_t Bitmap::Stride() const noexcept {
    return impl_->stride;
}

int Bitmap::Config() const noexcept {
    return impl_->config;
}

int Bitmap::Format() const noexcept {
    switch (impl_->config) {
    case 1: return 8;
    case 3: return 4;
    case 4: return 7;
    default: return 1;
    }
}

std::uint32_t Bitmap::Locks() const noexcept {
    return impl_->locks;
}

std::uint32_t Bitmap::Revision() const {
    return impl_->bitmap.getGenerationID();
}

SkBitmap& Bitmap::SkiaBitmap() noexcept {
    return impl_->bitmap;
}

std::uint32_t Bitmap::Get(std::size_t index) const {
    if (index >= Size()) throw std::out_of_range("invalid Bitmap pixel index");
    const auto width = static_cast<std::size_t>(impl_->bitmap.width());
    const auto color = impl_->bitmap.getColor(static_cast<int>(index % width), static_cast<int>(index / width));
    return impl_->bitmap.isOpaque() ? color | 0xff000000U : color;
}

void Bitmap::Set(std::size_t index, std::uint32_t argb) {
    if (index >= Size()) throw std::out_of_range("invalid Bitmap pixel index");
    const auto width = static_cast<std::size_t>(impl_->bitmap.width());
    Write(static_cast<int>(index % width), static_cast<int>(index / width), 1, 1, {&argb, 1});
}

void Bitmap::Read(int x, int y, int width, int height, std::span<std::uint32_t> argb) const {
    const auto info = Argb(width, height);
    if (info.computeMinByteSize() > argb.size_bytes() ||
        !impl_->bitmap.readPixels(info, argb.data(), info.minRowBytes(), x, y))
        throw std::invalid_argument("cannot read Bitmap pixel region");
    if (impl_->bitmap.isOpaque())
        for (auto& color : argb.first(info.computeMinByteSize() / sizeof(std::uint32_t)))
            color |= 0xff000000U;
}

void Bitmap::Write(int x, int y, int width, int height, std::span<const std::uint32_t> argb) {
    const auto info =
        Argb(width, height).makeAlphaType(impl_->bitmap.isOpaque() ? kOpaque_SkAlphaType : kUnpremul_SkAlphaType);
    if (info.computeMinByteSize() > argb.size_bytes() ||
        !impl_->bitmap.writePixels({info, argb.data(), info.minRowBytes()}, x, y))
        throw std::invalid_argument("cannot write Bitmap pixel region");
}

std::vector<std::uint32_t> Bitmap::Snapshot() const {
    std::vector<std::uint32_t> argb(Size());
    if (!argb.empty()) Read(0, 0, impl_->bitmap.width(), impl_->bitmap.height(), argb);
    return argb;
}

std::span<std::byte> Bitmap::RawPixels() const {
    return impl_->allocation.first(impl_->bitmap.computeByteSize());
}

bool Bitmap::HasAlpha() const {
    return !impl_->bitmap.isOpaque();
}

bool Bitmap::Premultiplied() const {
    return impl_->premultiplied;
}

void Bitmap::SetAlpha(bool alpha, bool premultiplied) {
    if (!impl_->bitmap.setAlphaType(AlphaType(impl_->config, alpha, premultiplied)))
        throw std::invalid_argument("invalid Bitmap alpha type");
    impl_->premultiplied = premultiplied;
}

void Bitmap::Reconfigure(int width, int height, int config, bool alpha, bool premultiplied) {
    if (impl_->retired || AllocationSize(width, height, config) > impl_->allocation.size())
        throw std::invalid_argument("Bitmap storage cannot fit the requested layout");
    const auto info = SkImageInfo::Make(width, height, ColorType(config), AlphaType(config, alpha, premultiplied));
    auto lifetime = std::make_unique<std::shared_ptr<void>>(impl_->lease);
    SkBitmap replacement;
    if (!replacement.installPixels(
            info, impl_->allocation.data(), info.minRowBytes(),
            +[](void*, void* owner) { delete static_cast<std::shared_ptr<void>*>(owner); }, lifetime.release()))
        throw std::bad_alloc{};
    impl_->bitmap = std::move(replacement);
    impl_->config = config;
    impl_->stride = static_cast<std::uint32_t>(info.minRowBytes());
    impl_->premultiplied = premultiplied;
}

bool Bitmap::Recycled() const noexcept {
    return impl_->retired;
}

void Bitmap::Retire() {
    impl_->retired = true;
    if (impl_->locks == 0) AbortLocks();
}

void Bitmap::AbortLocks() {
    impl_->bitmap.reset();
    impl_->lease.reset();
    impl_->allocation = {};
    impl_->address = memory::GuestAddress{};
    impl_->retired = true;
    impl_->locks = 0;
}

memory::GuestAddress Bitmap::Lock() {
    if (impl_->retired) throw std::logic_error("Bitmap is recycled");
    if (impl_->address.IsNull() || impl_->locks == std::numeric_limits<std::uint32_t>::max()) throw std::bad_alloc{};
    ++impl_->locks;
    return impl_->address;
}

void Bitmap::Unlock() {
    if (impl_->locks == 0) throw std::logic_error("Bitmap has no active pixel lock");
    if (--impl_->locks == 0) {
        if (impl_->retired)
            AbortLocks();
        else
            impl_->bitmap.notifyPixelsChanged();
    }
}

}  // namespace ogplay::runtime::raster
