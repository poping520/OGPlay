#include "window_icon.h"

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace ogplay::hal::windows {
namespace {

// The smallest size at least `wanted` pixels wide, else the largest: Windows then only ever shrinks.
const IconImage& Nearest(const std::span<const IconImage> sizes, const int wanted) {
    const IconImage* best = nullptr;
    for (const auto& image : sizes)
        if (static_cast<int>(image.width) >= wanted && (best == nullptr || image.width < best->width)) best = &image;
    if (best != nullptr) return *best;
    best = &sizes.front();
    for (const auto& image : sizes)
        if (image.width > best->width) best = &image;
    return *best;
}

// A 32-bit alpha icon: top-down BGRA colour bitmap plus an all-zero mask (the alpha channel decides).
HICON CreateRgbaIcon(const IconImage& image) {
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = static_cast<LONG>(image.width);
    info.bmiHeader.biHeight = -static_cast<LONG>(image.height);
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP color = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (color == nullptr) return nullptr;
    if (bits == nullptr) {
        DeleteObject(color);
        return nullptr;
    }
    auto* out = static_cast<std::uint8_t*>(bits);
    for (std::size_t i = 0; i < static_cast<std::size_t>(image.width) * image.height; ++i) {
        out[i * 4U + 0U] = image.rgba[i * 4U + 2U];
        out[i * 4U + 1U] = image.rgba[i * 4U + 1U];
        out[i * 4U + 2U] = image.rgba[i * 4U + 0U];
        out[i * 4U + 3U] = image.rgba[i * 4U + 3U];
    }
    // Monochrome rows are WORD aligned.
    const std::vector<std::uint8_t> zeros(((image.width + 15U) / 16U) * 2U * image.height, 0U);
    HBITMAP mask = CreateBitmap(static_cast<int>(image.width), static_cast<int>(image.height), 1, 1, zeros.data());
    HICON icon = nullptr;
    if (mask != nullptr) {
        ICONINFO parts{TRUE, 0, 0, mask, color};
        icon = CreateIconIndirect(&parts);
        DeleteObject(mask);
    }
    DeleteObject(color);
    return icon;
}

}  // namespace

void WindowIcons::Apply(void* const hwnd, const std::span<const IconImage> sizes) {
    if (hwnd == nullptr) throw std::runtime_error("window has no HWND for its icon");
    auto* const window = static_cast<HWND>(hwnd);
    const UINT dpi = GetDpiForWindow(window);
    HICON small_icon = CreateRgbaIcon(Nearest(sizes, GetSystemMetricsForDpi(SM_CXSMICON, dpi)));
    HICON big_icon = CreateRgbaIcon(Nearest(sizes, GetSystemMetricsForDpi(SM_CXICON, dpi)));
    if (small_icon == nullptr || big_icon == nullptr) {
        if (small_icon != nullptr) DestroyIcon(small_icon);
        if (big_icon != nullptr) DestroyIcon(big_icon);
        throw std::runtime_error("failed to create the window icon");
    }
    SendMessageW(window, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(small_icon));
    SendMessageW(window, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(big_icon));
    Release();
    small_ = small_icon;
    big_ = big_icon;
}

void WindowIcons::Release() noexcept {
    if (small_ != nullptr) DestroyIcon(static_cast<HICON>(small_));
    if (big_ != nullptr) DestroyIcon(static_cast<HICON>(big_));
    small_ = big_ = nullptr;
}

}  // namespace ogplay::hal::windows
