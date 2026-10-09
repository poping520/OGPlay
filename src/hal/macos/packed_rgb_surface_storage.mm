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
static std::unique_ptr<RgbSurfaceStorage> CreatePackedTextureStorage(void* native_device,
    std::uint32_t width, std::uint32_t height, MTLPixelFormat format) {
    if (!native_device || !width || !height || width > INT_MAX || height > INT_MAX)
        throw std::invalid_argument("packed RGB storage requires a device and positive dimensions");
    @autoreleasepool {
        auto descriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:format
            width:width height:height mipmapped:NO];
        descriptor.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
        descriptor.storageMode = MTLStorageModePrivate;
        auto texture = [(__bridge id<MTLDevice>)native_device newTextureWithDescriptor:descriptor];
        if (!texture) throw std::runtime_error("Metal packed RGB storage allocation failed");
        try { return std::make_unique<PackedStorage>(texture); }
        catch (...) { [texture release]; throw; }
    }
}
std::unique_ptr<RgbSurfaceStorage> CreatePackedRgbSurfaceStorage(void* device, std::uint32_t w, std::uint32_t h) {
    return CreatePackedTextureStorage(device,w,h,MTLPixelFormatB5G6R5Unorm);
}
std::unique_ptr<RgbSurfaceStorage> CreatePackedDepthStencilStorage(void* device, std::uint32_t w, std::uint32_t h) {
    return CreatePackedTextureStorage(device,w,h,MTLPixelFormatDepth32Float_Stencil8);
}

}
