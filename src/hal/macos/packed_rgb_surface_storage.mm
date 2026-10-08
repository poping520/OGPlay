#include "ogplay/hal/rgb_surface_storage.h"
#import <Metal/Metal.h>
#include <stdexcept>
#include <limits>
#include <climits>
namespace ogplay::hal {
namespace {
class PackedStorage final : public RgbSurfaceStorage {
public:
    explicit PackedStorage(id<MTLTexture> texture) : texture_(texture) {}
    ~PackedStorage() override { [texture_ release]; }
    void* NativeBuffer() const noexcept override { return (__bridge void*)texture_; }
private:
    id<MTLTexture> texture_;
};
}
bool HasPackedRgbSurfaceStorage() noexcept { return true; }
std::unique_ptr<RgbSurfaceStorage> CreatePackedRgbSurfaceStorage(void* native_device,
    std::uint32_t width, std::uint32_t height) {
    if (!native_device || !width || !height || width > INT_MAX || height > INT_MAX)
        throw std::invalid_argument("packed RGB storage requires a device and positive dimensions");
    @autoreleasepool {
        auto descriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatB5G6R5Unorm
            width:width height:height mipmapped:NO];
        descriptor.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
        descriptor.storageMode = MTLStorageModePrivate;
        auto texture = [(__bridge id<MTLDevice>)native_device newTextureWithDescriptor:descriptor];
        if (!texture) throw std::runtime_error("Metal packed RGB storage allocation failed");
        try { return std::make_unique<PackedStorage>(texture); }
        catch (...) { [texture release]; throw; }
    }
}
}
