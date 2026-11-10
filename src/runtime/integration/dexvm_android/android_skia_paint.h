#pragma once

#include "android_skia.h"

namespace ogplay::runtime::android_intrinsics::skia {

raster::Paint& PaintPeer(DexVmAndroidContext& context, int token);
raster::Paint CanvasPaintPeer(DexVmAndroidContext& context, const raster::Canvas& target, int token);
void CheckCanvasBitmap(const raster::Bitmap& bitmap);
void CheckCanvasTarget(const raster::Canvas& target);

enum class TextRange { count, end };
std::u16string ReadText(dx::Interpreter& vm, dx::VmObjectRef text, bool array);
std::u16string_view TextSlice(std::u16string_view text, int start, int limit, TextRange range);
std::u16string_view TextRunSlice(std::u16string_view text, int start, int limit, int context_start, int context_limit,
                                 TextRange range);
raster::TextRun LayoutText(dx::Interpreter& vm, const DexVmAndroidContext& context, const raster::Paint& paint,
                           std::u16string_view text, bool rtl = false);

void AddPaintTextMethods(dx::IntrinsicClassBuilder& builder, const Context& context);

}  // namespace ogplay::runtime::android_intrinsics::skia
