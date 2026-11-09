#pragma once

#include "android_skia.h"
#include "ogplay/runtime/raster/bitmap.h"

namespace ogplay::runtime::android_intrinsics::skia {

struct BitmapCreateInfo final {
    int width;
    int height;
    int config{5};
    int density{-1};
    bool mutable_pixels{true};
    bool premultiplied{true};
    bool alpha{true};
    dx::VmObjectRef nine_patch{};
    dx::VmObjectRef layout_bounds{};
};

dx::VmObjectRef CreateBitmap(dx::Interpreter& vm, DexVmAndroidContext& context, const BitmapCreateInfo& info,
                             const SkBitmap* source = nullptr);
raster::Bitmap& BitmapPeer(DexVmAndroidContext& context, int token);
void ReconfigureBitmap(dx::Interpreter& vm, DexVmAndroidContext::BitmapState& state, dx::VmObjectRef bitmap, int width,
                       int height, int config, bool alpha, bool premultiplied);

}  // namespace ogplay::runtime::android_intrinsics::skia
