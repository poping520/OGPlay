#include "game_window.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <string>
#include <string_view>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d2d1.h>
#include <d2d1helper.h>
#include <dwrite.h>
#include <wrl/client.h>

#include "window_chrome.h"

namespace ogplay::hal::windows {
namespace {

using Microsoft::WRL::ComPtr;

// Layout in device-independent pixels; Direct2D scales it with the render target's DPI.
constexpr float kDialogWidth = 400.0F;
constexpr float kPadding = 24.0F;
constexpr float kBadgeSize = 36.0F;
constexpr float kBadgeGap = 14.0F;
constexpr float kTitleGap = 6.0F;
constexpr float kButtonsGap = 22.0F;
constexpr float kButtonHeight = 38.0F;
constexpr float kButtonMinWidth = 92.0F;
constexpr float kButtonPadding = 20.0F;
constexpr float kButtonSpacing = 10.0F;
constexpr float kBottomPadding = 22.0F;
constexpr float kUnbounded = 10000.0F;
constexpr BYTE kScrimAlpha = 140;

// Launcher palette (webui/packages/ui-kit/theme.css).
constexpr UINT32 kPanel = 0x141a2a;
constexpr UINT32 kLine = 0x262f45;
constexpr UINT32 kInk = 0xf2f4fa;
constexpr UINT32 kSoft = 0xc4cadb;
constexpr UINT32 kYellow = 0xffc21a;
constexpr UINT32 kYellowHover = 0xffd04d;
constexpr UINT32 kOnYellow = 0x2a2000;
constexpr UINT32 kWarning = 0xffb020;
constexpr UINT32 kWhite = 0xffffff;

constexpr int kNoButton = -1;
constexpr int kCloseButton = 0;
constexpr int kCancelButton = 1;

void Check(const HRESULT result, const char* const operation) {
    if (FAILED(result)) throw std::runtime_error(std::string(operation) + " failed: " + std::to_string(result));
}

std::wstring Wide(const std::string_view text) {
    if (text.empty()) return {};
    const auto size =
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (size == 0) throw std::invalid_argument("close confirmation text is not UTF-8");
    std::wstring result(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), result.data(), size);
    return result;
}

ATOM RegisterDialogClass(const wchar_t* const name, const WNDPROC procedure, const HBRUSH background,
                         const UINT style) {
    WNDCLASSEXW descriptor{};
    descriptor.cbSize = sizeof(descriptor);
    descriptor.style = style;
    descriptor.hInstance = GetModuleHandleW(nullptr);
    descriptor.lpfnWndProc = procedure;
    descriptor.lpszClassName = name;
    descriptor.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    descriptor.hbrBackground = background;
    const auto atom = RegisterClassExW(&descriptor);
    if (atom == 0) throw std::runtime_error("Cannot register close confirmation window class");
    return atom;
}

class CloseDialog final {
public:
    CloseDialog(const HWND owner, const CloseConfirmation& text) : owner_(owner), text_(text) {}

    CloseDialog(const CloseDialog&) = delete;
    CloseDialog& operator=(const CloseDialog&) = delete;

    ~CloseDialog() {
        // The owner must be enabled before the dialog goes, or the system activates another application.
        if (owner_disabled_) EnableWindow(owner_, TRUE);
        brush_.Reset();
        target_.Reset();
        if (dialog_ != nullptr) DestroyWindow(dialog_);
        if (scrim_ != nullptr) DestroyWindow(scrim_);
        if (owner_disabled_) SetForegroundWindow(owner_);
    }

    [[nodiscard]] bool Run() {
        // A window closed from the taskbar may be minimized and would have nothing to centre on.
        if (IsIconic(owner_)) ShowWindow(owner_, SW_RESTORE);
        dpi_ = GetDpiForWindow(owner_);
        if (dpi_ == 0) dpi_ = USER_DEFAULT_SCREEN_DPI;
        BuildText();
        const auto owner_area = OwnerClientArea();
        CreateScrim(owner_area);
        CreateDialogWindow(owner_area);
        Check(CreateTarget(), "ID2D1Factory::CreateHwndRenderTarget");

        EnableWindow(owner_, FALSE);
        owner_disabled_ = true;
        if (scrim_ != nullptr) ShowWindow(scrim_, SW_SHOWNOACTIVATE);
        ShowWindow(dialog_, SW_SHOW);
        SetForegroundWindow(dialog_);
        SetFocus(dialog_);

        MSG message{};
        while (!done_) {
            const auto status = GetMessageW(&message, nullptr, 0, 0);
            if (status <= 0) {
                // The application is quitting: leave the request for the owner's own event loop.
                if (status == 0) PostQuitMessage(static_cast<int>(message.wParam));
                break;
            }
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        if (failed_) throw std::runtime_error("Cannot redraw close confirmation");
        return chose_close_;
    }

private:
    struct Button final {
        ComPtr<IDWriteTextLayout> label;
        float label_width{};
        float label_height{};
        D2D1_RECT_F bounds{};
    };

    [[nodiscard]] float Scale() const noexcept { return static_cast<float>(dpi_) / 96.0F; }

    [[nodiscard]] int ToPixels(const float length) const noexcept {
        return static_cast<int>(std::ceil(length * Scale()));
    }

    [[nodiscard]] ComPtr<IDWriteTextFormat> CreateFormat(const DWRITE_FONT_WEIGHT weight, const float size) const {
        ComPtr<IDWriteTextFormat> format;
        // Segoe UI has no Han glyphs; the zh-CN locale makes DirectWrite fall back to Microsoft YaHei UI for them,
        // the launcher's Chinese face, and it does not affect Latin text.
        Check(write_factory_->CreateTextFormat(L"Segoe UI", nullptr, weight, DWRITE_FONT_STYLE_NORMAL,
                                               DWRITE_FONT_STRETCH_NORMAL, size, L"zh-CN", format.GetAddressOf()),
              "IDWriteFactory::CreateTextFormat");
        return format;
    }

    [[nodiscard]] ComPtr<IDWriteTextLayout> CreateLayout(const std::wstring& text, IDWriteTextFormat* const format,
                                                         const float max_width) const {
        ComPtr<IDWriteTextLayout> layout;
        Check(write_factory_->CreateTextLayout(text.c_str(), static_cast<UINT32>(text.size()), format, max_width,
                                               kUnbounded, layout.GetAddressOf()),
              "IDWriteFactory::CreateTextLayout");
        return layout;
    }

    void BuildText() {
        Check(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, factory_.GetAddressOf()), "D2D1CreateFactory");
        Check(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                  reinterpret_cast<IUnknown**>(write_factory_.GetAddressOf())),
              "DWriteCreateFactory");
        const auto title_format = CreateFormat(DWRITE_FONT_WEIGHT_SEMI_BOLD, 17.0F);
        const auto message_format = CreateFormat(DWRITE_FONT_WEIGHT_NORMAL, 14.0F);
        const auto button_format = CreateFormat(DWRITE_FONT_WEIGHT_SEMI_BOLD, 14.0F);
        Check(button_format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP), "IDWriteTextFormat::SetWordWrapping");

        title_wide_ = Wide(text_.title);
        const auto text_width = kDialogWidth - kPadding * 2.0F - kBadgeSize - kBadgeGap;
        title_ = CreateLayout(title_wide_, title_format.Get(), text_width);
        message_ = CreateLayout(Wide(text_.message), message_format.Get(), text_width);
        DWRITE_TEXT_METRICS metrics{};
        Check(title_->GetMetrics(&metrics), "IDWriteTextLayout::GetMetrics");
        title_height_ = metrics.height;
        Check(message_->GetMetrics(&metrics), "IDWriteTextLayout::GetMetrics");
        const auto text_height = title_height_ + kTitleGap + metrics.height;
        const auto buttons_top = kPadding + (std::max)(kBadgeSize, text_height) + kButtonsGap;
        dialog_height_ = buttons_top + kButtonHeight + kBottomPadding;

        const std::array labels{Wide(text_.confirm_label), Wide(text_.cancel_label)};
        auto right = kDialogWidth - kPadding;
        // Right to left, so the cancel button sits at the edge whatever the labels' widths are.
        for (int index = kCancelButton; index >= kCloseButton; --index) {
            auto& button = buttons_[static_cast<std::size_t>(index)];
            button.label = CreateLayout(labels[static_cast<std::size_t>(index)], button_format.Get(), kUnbounded);
            Check(button.label->GetMetrics(&metrics), "IDWriteTextLayout::GetMetrics");
            button.label_width = metrics.width;
            button.label_height = metrics.height;
            const auto width = (std::max)(kButtonMinWidth, metrics.width + kButtonPadding * 2.0F);
            button.bounds = D2D1::RectF(right - width, buttons_top, right, buttons_top + kButtonHeight);
            right -= width + kButtonSpacing;
        }
    }

    [[nodiscard]] RECT OwnerClientArea() const {
        RECT area{};
        POINT origin{};
        if (!GetClientRect(owner_, &area) || !ClientToScreen(owner_, &origin))
            throw std::runtime_error("Cannot locate game window");
        OffsetRect(&area, origin.x, origin.y);
        return area;
    }

    void CreateScrim(const RECT& area) {
        if (area.right <= area.left || area.bottom <= area.top) return;
        static const auto scrim_class = RegisterDialogClass(L"OGPlayDialogScrim", &DefWindowProcW,
                                                            static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)), 0);
        // Disabled and never activated: it only dims the game, and clicks on it do nothing.
        scrim_ = CreateWindowExW(WS_EX_LAYERED | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, MAKEINTRESOURCEW(scrim_class),
                                 L"", WS_POPUP | WS_DISABLED, area.left, area.top, area.right - area.left,
                                 area.bottom - area.top, owner_, nullptr, GetModuleHandleW(nullptr), nullptr);
        if (scrim_ == nullptr) throw std::runtime_error("Cannot create close confirmation backdrop");
        SetLayeredWindowAttributes(scrim_, 0, kScrimAlpha, LWA_ALPHA);
    }

    void CreateDialogWindow(const RECT& area) {
        static const auto dialog_class =
            RegisterDialogClass(L"OGPlayCloseDialog", &CloseDialog::WindowProc, nullptr, CS_DROPSHADOW);
        const auto width = ToPixels(kDialogWidth);
        const auto height = ToPixels(dialog_height_);
        MONITORINFO monitor{};
        monitor.cbSize = sizeof(monitor);
        if (!GetMonitorInfoW(MonitorFromWindow(owner_, MONITOR_DEFAULTTONEAREST), &monitor))
            throw std::runtime_error("Cannot query game window monitor");
        const auto& work = monitor.rcWork;
        // A small game window may be narrower than the dialog: stay on screen rather than on the window.
        const auto x =
            (std::clamp)((area.left + area.right - width) / 2, work.left, (std::max)(work.left, work.right - width));
        const auto y =
            (std::clamp)((area.top + area.bottom - height) / 2, work.top, (std::max)(work.top, work.bottom - height));
        dialog_ = CreateWindowExW(0, MAKEINTRESOURCEW(dialog_class), title_wide_.c_str(), WS_POPUP, x, y, width, height,
                                  owner_, nullptr, GetModuleHandleW(nullptr), this);
        if (dialog_ == nullptr) throw std::runtime_error("Cannot create close confirmation window");
        const BOOL dark = TRUE;
        const DWORD corners = 2;  // DWMWCP_ROUND
        static_cast<void>(DwmSetWindowAttribute(dialog_, kDwmImmersiveDarkMode, &dark, sizeof(dark)));
        static_cast<void>(DwmSetWindowAttribute(dialog_, kDwmCornerPreference, &corners, sizeof(corners)));
        // Before Windows 11 there is no system border to colour; Paint draws one instead.
        system_border_ =
            SUCCEEDED(DwmSetWindowAttribute(dialog_, kDwmBorderColor, &kChromeBorder, sizeof(kChromeBorder)));
    }

    [[nodiscard]] HRESULT CreateTarget() {
        RECT client{};
        GetClientRect(dialog_, &client);
        const auto dpi = static_cast<float>(dpi_);
        const auto properties =
            D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT, D2D1::PixelFormat(), dpi, dpi);
        const auto window_properties = D2D1::HwndRenderTargetProperties(
            dialog_, D2D1::SizeU(static_cast<UINT32>(client.right), static_cast<UINT32>(client.bottom)));
        auto result = factory_->CreateHwndRenderTarget(properties, window_properties, target_.GetAddressOf());
        if (SUCCEEDED(result)) result = target_->CreateSolidColorBrush(D2D1::ColorF(kInk), brush_.GetAddressOf());
        if (FAILED(result)) {
            brush_.Reset();
            target_.Reset();
        }
        return result;
    }

    void Fill(const D2D1_ROUNDED_RECT& shape, const D2D1_COLOR_F& color) {
        brush_->SetColor(color);
        target_->FillRoundedRectangle(shape, brush_.Get());
    }

    void Fill(const D2D1_ELLIPSE& shape, const D2D1_COLOR_F& color) {
        brush_->SetColor(color);
        target_->FillEllipse(shape, brush_.Get());
    }

    void DrawLayout(const float x, const float y, IDWriteTextLayout* const layout, const UINT32 color) {
        brush_->SetColor(D2D1::ColorF(color));
        target_->DrawTextLayout(D2D1::Point2F(x, y), layout, brush_.Get());
    }

    void DrawButton(const int index) {
        const auto& button = buttons_[static_cast<std::size_t>(index)];
        const auto focused = index == focus_;
        const auto hovered = index == hover_;
        const auto pressed = index == pressed_ && hovered;
        const auto color = focused ? D2D1::ColorF(hovered && !pressed ? kYellowHover : kYellow)
                                   : D2D1::ColorF(kWhite, pressed   ? 0.05F
                                                          : hovered ? 0.14F
                                                                    : 0.08F);
        const auto radius = kButtonHeight / 2.0F;
        Fill(D2D1::RoundedRect(button.bounds, radius, radius), color);
        const auto& box = button.bounds;
        DrawLayout((box.left + box.right - button.label_width) / 2.0F,
                   (box.top + box.bottom - button.label_height) / 2.0F, button.label.Get(), focused ? kOnYellow : kInk);
    }

    void Draw() {
        target_->BeginDraw();
        target_->Clear(D2D1::ColorF(kPanel));
        if (!system_border_) {
            brush_->SetColor(D2D1::ColorF(kLine));
            target_->DrawRectangle(D2D1::RectF(0.5F, 0.5F, kDialogWidth - 0.5F, dialog_height_ - 0.5F), brush_.Get());
        }
        // Warning badge, drawn rather than typed so its "!" does not depend on a font's metrics.
        const auto badge_x = kPadding + kBadgeSize / 2.0F;
        const auto badge_y = kPadding + kBadgeSize / 2.0F;
        Fill(D2D1::Ellipse(D2D1::Point2F(badge_x, badge_y), kBadgeSize / 2.0F, kBadgeSize / 2.0F),
             D2D1::ColorF(kWarning, 0.16F));
        Fill(
            D2D1::RoundedRect(D2D1::RectF(badge_x - 1.5F, badge_y - 10.0F, badge_x + 1.5F, badge_y + 3.0F), 1.5F, 1.5F),
            D2D1::ColorF(kWarning));
        Fill(D2D1::Ellipse(D2D1::Point2F(badge_x, badge_y + 8.0F), 1.9F, 1.9F), D2D1::ColorF(kWarning));

        const auto text_x = kPadding + kBadgeSize + kBadgeGap;
        DrawLayout(text_x, kPadding, title_.Get(), kInk);
        DrawLayout(text_x, kPadding + title_height_ + kTitleGap, message_.Get(), kSoft);
        DrawButton(kCloseButton);
        DrawButton(kCancelButton);
        if (target_->EndDraw() == D2DERR_RECREATE_TARGET) {
            // The display device changed; the next paint rebuilds on the new one.
            brush_.Reset();
            target_.Reset();
            Invalidate();
        }
    }

    void Paint() {
        PAINTSTRUCT paint{};
        BeginPaint(dialog_, &paint);
        if (target_ == nullptr && FAILED(CreateTarget())) {
            // An invisible modal dialog would trap the user; report it to the caller instead.
            failed_ = true;
            Finish(false);
        }
        if (target_ != nullptr) Draw();
        EndPaint(dialog_, &paint);
    }

    void Invalidate() const noexcept { InvalidateRect(dialog_, nullptr, FALSE); }

    void Finish(const bool chose_close) noexcept {
        chose_close_ = chose_close;
        done_ = true;
        // A close sent from outside arrives inside GetMessageW, which would otherwise keep waiting.
        PostMessageW(dialog_, WM_NULL, 0, 0);
    }

    [[nodiscard]] int ButtonAt(const LPARAM position) const noexcept {
        const auto x = static_cast<float>(static_cast<short>(LOWORD(position))) / Scale();
        const auto y = static_cast<float>(static_cast<short>(HIWORD(position))) / Scale();
        for (int index = kCloseButton; index <= kCancelButton; ++index) {
            const auto& box = buttons_[static_cast<std::size_t>(index)].bounds;
            if (x >= box.left && x < box.right && y >= box.top && y < box.bottom) return index;
        }
        return kNoButton;
    }

    void SetHover(const int index) noexcept {
        if (hover_ == index) return;
        hover_ = index;
        Invalidate();
    }

    void OnKeyDown(const WPARAM key) noexcept {
        switch (key) {
        case VK_LEFT:
        case VK_RIGHT:
        case VK_TAB:
            focus_ = focus_ == kCloseButton ? kCancelButton : kCloseButton;
            Invalidate();
            break;
        case VK_RETURN:
        case VK_SPACE: Finish(focus_ == kCloseButton); break;
        case VK_ESCAPE: Finish(false); break;
        default: break;
        }
    }

    static LRESULT CALLBACK WindowProc(const HWND window, const UINT message, const WPARAM wp,
                                       const LPARAM lp) noexcept {
        if (message == WM_NCCREATE) {
            const auto* const create = reinterpret_cast<const CREATESTRUCTW*>(lp);
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
        }
        auto* const self = reinterpret_cast<CloseDialog*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (self == nullptr || self->dialog_ != window) return DefWindowProcW(window, message, wp, lp);
        switch (message) {
        case WM_ERASEBKGND: return 1;  // Direct2D covers the whole client area.
        case WM_PAINT: self->Paint(); return 0;
        case WM_KEYDOWN:
            // A key still held from play repeats here; only a fresh press may answer the dialog.
            if ((HIWORD(lp) & KF_REPEAT) == 0) self->OnKeyDown(wp);
            return 0;
        case WM_MOUSEMOVE:
            if (!self->tracking_mouse_) {
                TRACKMOUSEEVENT track{};
                track.cbSize = sizeof(track);
                track.dwFlags = TME_LEAVE;
                track.hwndTrack = window;
                self->tracking_mouse_ = TrackMouseEvent(&track) != FALSE;
            }
            self->SetHover(self->ButtonAt(lp));
            return 0;
        case WM_MOUSELEAVE:
            self->tracking_mouse_ = false;
            self->SetHover(kNoButton);
            return 0;
        case WM_LBUTTONDOWN:
            self->pressed_ = self->ButtonAt(lp);
            if (self->pressed_ != kNoButton) {
                SetCapture(window);
                self->Invalidate();
            }
            return 0;
        case WM_LBUTTONUP: {
            const auto pressed = self->pressed_;
            self->pressed_ = kNoButton;
            if (GetCapture() == window) ReleaseCapture();
            self->Invalidate();
            if (pressed != kNoButton && pressed == self->ButtonAt(lp)) self->Finish(pressed == kCloseButton);
            return 0;
        }
        case WM_CAPTURECHANGED:
            self->pressed_ = kNoButton;
            self->Invalidate();
            return 0;
        case WM_CLOSE: self->Finish(false); return 0;  // Alt+F4 answers "keep playing", like Esc.
        case WM_NCDESTROY:
            SetWindowLongPtrW(window, GWLP_USERDATA, 0);
            self->dialog_ = nullptr;
            break;
        default: break;
        }
        return DefWindowProcW(window, message, wp, lp);
    }

    HWND owner_;
    const CloseConfirmation& text_;
    HWND scrim_{};
    HWND dialog_{};
    UINT dpi_{USER_DEFAULT_SCREEN_DPI};
    ComPtr<ID2D1Factory> factory_;
    ComPtr<IDWriteFactory> write_factory_;
    ComPtr<ID2D1HwndRenderTarget> target_;
    ComPtr<ID2D1SolidColorBrush> brush_;
    ComPtr<IDWriteTextLayout> title_;
    ComPtr<IDWriteTextLayout> message_;
    std::array<Button, 2> buttons_;
    std::wstring title_wide_;
    float title_height_{};
    float dialog_height_{};
    int focus_{kCancelButton};  // Enter keeps the game running, so keys still pressed in play cannot close it.
    int hover_{kNoButton};
    int pressed_{kNoButton};
    bool system_border_{};
    bool tracking_mouse_{};
    bool owner_disabled_{};
    bool done_{};
    bool failed_{};
    bool chose_close_{};
};

}  // namespace

void ApplyGameWindowChrome(void* const hwnd) noexcept {
    if (hwnd != nullptr) ApplyWindowChrome(static_cast<HWND>(hwnd));
}

bool ShowCloseConfirmation(void* const hwnd, const CloseConfirmation& text) {
    if (hwnd == nullptr) throw std::runtime_error("The window has no Win32 handle for a close confirmation");
    CloseDialog dialog(static_cast<HWND>(hwnd), text);
    return dialog.Run();
}

}  // namespace ogplay::hal::windows
