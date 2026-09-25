#include "ui/win32/LanguageToast.h"

#include <algorithm>
#include <shellscalingapi.h>

namespace lankey::ui::win32 {

namespace {

constexpr wchar_t kClassName[] = L"LanKeyLanguageToast";
constexpr UINT_PTR kHideTimer = 1;
constexpr int kVisibleMs = 1400;
// Layout in 96-dpi pixels.
constexpr int kWidth = 232;
constexpr int kMaxWidth = 440;
constexpr int kHeight = 56;
constexpr int kPadding = 14;
constexpr int kTile = 28;
constexpr int kTileRadius = 7;
constexpr int kMarginFromEdge = 24;
constexpr int kFontPt = 11;

} // namespace

LanguageToast::~LanguageToast() {
    destroy();
}

void LanguageToast::show(HINSTANCE instance, bool vietnamese) {
    showText(instance, vietnamese ? L"V" : L"E", vietnamese ? kBrandVietnamese : kBrandEnglish,
             vietnamese ? L"Tiếng Việt" : L"English");
}

void LanguageToast::showText(HINSTANCE instance, const std::wstring& tile, COLORREF tileColour,
                             const std::wstring& text) {
    tile_ = tile;
    tileColour_ = tileColour;
    text_ = text;
    if (hwnd_ == nullptr) {
        WNDCLASSW wc{};
        wc.style = CS_DROPSHADOW;
        wc.lpfnWndProc = &LanguageToast::wndProc;
        wc.hInstance = instance;
        wc.lpszClassName = kClassName;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        RegisterClassW(&wc);
        hwnd_ =
            CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW | WS_EX_TOPMOST, kClassName, L"",
                            WS_POPUP, 0, 0, kWidth, kHeight, nullptr, nullptr, instance, this);
        if (hwnd_ == nullptr) return;
        if (applyRoundedCorners(hwnd_)) setBorderColor(hwnd_, kPopupPalette.border);
    }
    place();
    ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
    InvalidateRect(hwnd_, nullptr, TRUE);
    SetTimer(hwnd_, kHideTimer, kVisibleMs, nullptr); // restarts if already running
}

void LanguageToast::destroy() {
    if (hwnd_ != nullptr) DestroyWindow(hwnd_);
    hwnd_ = nullptr;
    if (font_ != nullptr) DeleteObject(font_);
    font_ = nullptr;
}

void LanguageToast::place() {
    // Bottom-right of the work area of the monitor holding the window being typed into -
    // where the user is looking, not necessarily the primary screen.
    HMONITOR monitor = nullptr;
    if (const HWND foreground = GetForegroundWindow(); foreground != nullptr) {
        monitor = MonitorFromWindow(foreground, MONITOR_DEFAULTTONEAREST);
    } else {
        monitor = MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
    }
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    RECT work{0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)};
    if (GetMonitorInfoW(monitor, &mi)) work = mi.rcWork;
    UINT dpi = 96;
    UINT unusedDpiY = 96;
    if (FAILED(GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpi, &unusedDpiY))) {
        dpi = GetDpiForWindow(hwnd_);
    }
    const auto px = [dpi](int v) { return MulDiv(v, static_cast<int>(dpi), 96); };
    if (font_ == nullptr || fontDpi_ != dpi) {
        if (font_ != nullptr) DeleteObject(font_);
        font_ = createUiFont(dpi, kFontPt, FW_NORMAL);
        fontDpi_ = dpi;
    }
    // Wide enough for the text: kWidth is the minimum (the language toast), longer
    // messages ("Hãy bôi đen văn bản trước") grow the card up to kMaxWidth.
    int w = px(kWidth);
    if (const HDC dc = GetDC(hwnd_); dc != nullptr) {
        const auto old = static_cast<HFONT>(SelectObject(dc, font_));
        SIZE size{};
        GetTextExtentPoint32W(dc, text_.c_str(), static_cast<int>(text_.size()), &size);
        SelectObject(dc, old);
        ReleaseDC(hwnd_, dc);
        const int needed = px(kPadding) * 3 + px(kTile) + size.cx;
        w = std::clamp(needed, px(kWidth), px(kMaxWidth));
    }
    const int h = px(kHeight);
    SetWindowPos(hwnd_, HWND_TOPMOST, work.right - w - px(kMarginFromEdge),
                 work.bottom - h - px(kMarginFromEdge), w, h, SWP_NOACTIVATE | SWP_NOOWNERZORDER);
}

void LanguageToast::paint() {
    PAINTSTRUCT ps{};
    const HDC dc = BeginPaint(hwnd_, &ps);
    RECT client{};
    GetClientRect(hwnd_, &client);
    const UINT dpi = fontDpi_ != 0 ? fontDpi_ : GetDpiForWindow(hwnd_);
    const auto px = [dpi](int v) { return MulDiv(v, static_cast<int>(dpi), 96); };

    const HBRUSH bg = CreateSolidBrush(kPopupPalette.background);
    FillRect(dc, &client, bg);
    DeleteObject(bg);

    // The brand tile, as on the tray icon: V on cobalt, E on graphite.
    const int tile = px(kTile);
    const int top = (client.bottom - tile) / 2;
    const RECT tileRect{px(kPadding), top, px(kPadding) + tile, top + tile};
    const HBRUSH fill = CreateSolidBrush(tileColour_);
    const HPEN noPen = static_cast<HPEN>(GetStockObject(NULL_PEN));
    const auto oldBrush = SelectObject(dc, fill);
    const auto oldPen = SelectObject(dc, noPen);
    RoundRect(dc, tileRect.left, tileRect.top, tileRect.right + 1, tileRect.bottom + 1,
              px(kTileRadius) * 2, px(kTileRadius) * 2);
    SelectObject(dc, oldPen);
    SelectObject(dc, oldBrush);
    DeleteObject(fill);

    SetBkMode(dc, TRANSPARENT);
    const HFONT bold = createUiFont(dpi, tile_.size() > 1 ? 10 : 12, FW_BOLD);
    const auto oldFont = static_cast<HFONT>(SelectObject(dc, bold));
    SetTextColor(dc, RGB(0xFF, 0xFF, 0xFF));
    RECT letter = tileRect;
    DrawTextW(dc, tile_.c_str(), -1, &letter, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(dc, oldFont);
    DeleteObject(bold);

    SelectObject(dc, font_);
    RECT text{tileRect.right + px(kPadding), client.top, client.right - px(kPadding),
              client.bottom};
    SetTextColor(dc, kPopupPalette.text);
    DrawTextW(dc, text_.c_str(), -1, &text,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
    EndPaint(hwnd_, &ps);
}

LRESULT CALLBACK LanguageToast::wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_NCCREATE) {
        const auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
    }
    auto* self = reinterpret_cast<LanguageToast*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
    case WM_PAINT:
        if (self != nullptr) self->paint();
        return 0;
    case WM_TIMER:
        if (wParam == kHideTimer) {
            KillTimer(hwnd, kHideTimer);
            ShowWindow(hwnd, SW_HIDE);
        }
        return 0;
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    case WM_ERASEBKGND:
        return 1;
    default:
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

} // namespace lankey::ui::win32
