#include "ogplay/hal/rgb_surface_storage.h"

#include <CoreFoundation/CoreFoundation.h>
#include <IOSurface/IOSurface.h>

#include <limits>
#include <stdexcept>

namespace ogplay::hal {
namespace {
class Storage final : public RgbSurfaceStorage {
public:
    explicit Storage(IOSurfaceRef surface) noexcept : surface_(surface) {}
    ~Storage() override { CFRelease(surface_); }
    void* NativeBuffer() const noexcept override { return surface_; }
private:
    IOSurfaceRef surface_;
};
}

bool HasRgbSurfaceStorage() noexcept { return true; }

std::unique_ptr<RgbSurfaceStorage> CreateRgbSurfaceStorage(
    const std::uint32_t width, const std::uint32_t height) {
    if (width == 0U || height == 0U ||
        width > static_cast<std::uint32_t>((std::numeric_limits<int>::max)()) ||
        height > static_cast<std::uint32_t>((std::numeric_limits<int>::max)()))
        throw std::invalid_argument("RGB surface dimensions must fit a positive int");
    const auto properties = CFDictionaryCreateMutable(nullptr, 0,
        &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    if (!properties) throw std::bad_alloc{};
    const auto set = [&](CFStringRef key, const int value) {
        const auto number = CFNumberCreate(nullptr, kCFNumberIntType, &value);
        if (!number) { CFRelease(properties); throw std::bad_alloc{}; }
        CFDictionarySetValue(properties, key, number);
        CFRelease(number);
    };
    set(kIOSurfaceWidth, static_cast<int>(width));
    set(kIOSurfaceHeight, static_cast<int>(height));
    set(kIOSurfaceBytesPerElement, 4);
    // ANGLE interprets this backing as BGRX when imported with GL_RGB.
    set(kIOSurfacePixelFormat, 0x42475241);
    const auto surface = IOSurfaceCreate(properties);
    CFRelease(properties);
    if (!surface) throw std::runtime_error("IOSurface RGB storage allocation failed");
    try { return std::make_unique<Storage>(surface); }
    catch (...) { CFRelease(surface); throw; }
}
}  // namespace ogplay::hal
