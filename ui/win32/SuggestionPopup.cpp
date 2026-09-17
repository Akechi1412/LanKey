#include "ui/win32/SuggestionPopup.h"

#include <algorithm>
#include <shellscalingapi.h>

namespace lankey::ui::win32 {

namespace {

constexpr wchar_t kClassName[] = L"LanKeySuggestionPopup";
// Layout in 96-dpi pixels; everything is scaled by the window's DPI.
constexpr int kPaddingX = 14;
constexpr int kRowPaddingY = 7;
constexpr int kAccentWidth = 3;
constexpr int kGapFromCaret = 6;
// A one-word prediction ("là") must not collapse the popup into a button-sized box, so
// the floor is generous; long phrases still grow up to the ceiling.
constexpr int kMinWidth = 220;
constexpr int kMaxWidth = 520;
constexpr int kFontPt = 10;

std::wstring toWide(std::u32string_view s) {
    return platform::win32::toUtf16(s);
}

} // namespace

SuggestionPopup::SuggestionPopup() = default;

SuggestionPopup::~SuggestionPopup() {
    destroy();
}

bool SuggestionPopup::create(HINSTANCE instance) {
    WNDCLASSW wc{};
    wc.style = CS_DROPSHADOW; // the soft system shadow, same as menus and tooltips
    wc.lpfnWndProc = &SuggestionPopup::wndProc;
    wc.hInstance = instance;
    wc.lpszClassName = kClassName;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassW(&wc);

    hwnd_ = CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW | WS_EX_TOPMOST, kClassName, L"",
                            WS_POPUP, 0, 0, width_, rowHeight_, nullptr, nullptr, instance, this);
    if (hwnd_ == nullptr) return false;
    dwmBorder_ = applyRoundedCorners(hwnd_);
    if (dwmBorder_) setBorderColor(hwnd_, kPopupPalette.border);
    return true;
}

void SuggestionPopup::destroy() {
    if (hwnd_ != nullptr) DestroyWindow(hwnd_);
    hwnd_ = nullptr;
    if (font_ != nullptr) DeleteObject(font_);
    font_ = nullptr;
}

void SuggestionPopup::ensureFont(UINT dpi) {
    if (font_ != nullptr && fontDpi_ == dpi) return;
    if (font_ != nullptr) DeleteObject(font_);
    font_ = createUiFont(dpi, kFontPt, FW_NORMAL);
    fontDpi_ = dpi;
}

void SuggestionPopup::show(const core::model::SuggestionList& items, int selected,
                           const std::optional<core::model::ScreenRect>& caret) {
    if (hwnd_ == nullptr || items.empty()) {
        hide();
        return;
    }
    rows_.clear();
    for (const auto& s : items) {
        // The phrase is "<context> <insert>"; whatever precedes the insert is already on
        // screen and is shown dim so the row reads as the whole phrase.
        const std::u32string joined = s.phrase.joined();
        const std::size_t insertLen = s.insert.size();
        const std::size_t contextLen = joined.size() >= insertLen ? joined.size() - insertLen : 0;
        rows_.push_back(
            {toWide(std::u32string_view(joined).substr(0, contextLen)), toWide(s.insert)});
    }
    selected_ = std::clamp(selected, 0, static_cast<int>(rows_.size()) - 1);
    layout(caret);
    if (!visible_) {
        ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
        visible_ = true;
    }
    InvalidateRect(hwnd_, nullptr, TRUE);
}

void SuggestionPopup::hide() {
    if (hwnd_ != nullptr && visible_) ShowWindow(hwnd_, SW_HIDE);
    visible_ = false;
}

void SuggestionPopup::layout(const std::optional<core::model::ScreenRect>& caret) {
    // The monitor that matters: the one holding the caret, else the one holding the
    // window being typed into (multi-monitor: never jump to the primary screen).
    HMONITOR monitor = nullptr;
    if (caret) {
        monitor = MonitorFromPoint(POINT{caret->x, caret->y}, MONITOR_DEFAULTTONEAREST);
    } else if (const HWND foreground = GetForegroundWindow(); foreground != nullptr) {
        monitor = MonitorFromWindow(foreground, MONITOR_DEFAULTTONEAREST);
    } else {
        monitor = MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
    }
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    RECT work{0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)};
    if (GetMonitorInfoW(monitor, &mi)) work = mi.rcWork;

    // Measure at the DPI of the monitor the popup is GOING to, not the one it was last
    // shown on: with mixed scaling the window still reports the old DPI at this point, and
    // the popup would land on the new monitor at the wrong size.
    UINT dpi = 96;
    UINT unusedDpiY = 96;
    if (FAILED(GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpi, &unusedDpiY))) {
        dpi = GetDpiForWindow(hwnd_);
    }
    const int scale = static_cast<int>(dpi);
    const auto px = [scale](int v) { return MulDiv(v, scale, 96); };
    ensureFont(dpi);

    const HDC dc = GetDC(hwnd_);
    const auto old = static_cast<HFONT>(SelectObject(dc, font_));
    TEXTMETRICW tm{};
    GetTextMetricsW(dc, &tm);
    rowHeight_ = tm.tmHeight + 2 * px(kRowPaddingY);
    int widest = 0;
    for (const auto& row : rows_) {
        SIZE a{};
        SIZE b{};
        GetTextExtentPoint32W(dc, row.context.c_str(), static_cast<int>(row.context.size()), &a);
        GetTextExtentPoint32W(dc, row.insert.c_str(), static_cast<int>(row.insert.size()), &b);
        widest = (std::max)(widest, static_cast<int>(a.cx + b.cx));
    }
    SelectObject(dc, old);
    ReleaseDC(hwnd_, dc);

    int width = std::clamp(widest + 2 * px(kPaddingX) + px(4) + px(kAccentWidth), px(kMinWidth),
                           px(kMaxWidth));
    // While open the popup only grows: the list changes with every keystroke and arrow
    // press, and a box that breathes in and out is more distracting than a little slack.
    if (visible_ && dpi == shownDpi_) width = (std::max)(width, width_);
    width_ = width;
    shownDpi_ = dpi;
    const int height = rowHeight_ * static_cast<int>(rows_.size()) + 2 * px(4);
    const int gap = px(kGapFromCaret);

    int x = 0;
    int y = 0;
    if (caret) {
        // Right of the caret; above it by default, below when there is no room above. The
        // popup never covers the line being typed.
        x = caret->x + caret->width + gap;
        y = caret->y - gap - height;
        if (y < work.top) y = caret->y + caret->height + gap;
        if (x + width_ > work.right) x = work.right - width_;
        x = (std::max)(x, static_cast<int>(work.left));
        y = std::clamp(y, static_cast<int>(work.top), static_cast<int>(work.bottom) - height);
    } else {
        // Caret unknown: top-right corner of that monitor's work area.
        x = work.right - width_ - gap;
        y = work.top + gap;
    }
    SetWindowPos(hwnd_, HWND_TOPMOST, x, y, width_, height, SWP_NOACTIVATE | SWP_NOOWNERZORDER);
}

void SuggestionPopup::paint() {
    PAINTSTRUCT ps{};
    const HDC dc = BeginPaint(hwnd_, &ps);
    RECT client{};
    GetClientRect(hwnd_, &client);
    // Same DPI layout() measured with; the window itself may still report the previous
    // monitor until DWM catches up.
    const UINT dpi = shownDpi_ != 0 ? shownDpi_ : GetDpiForWindow(hwnd_);
    const auto px = [dpi](int v) { return MulDiv(v, static_cast<int>(dpi), 96); };
    ensureFont(dpi);

    const HBRUSH bg = CreateSolidBrush(kPopupPalette.background);
    FillRect(dc, &client, bg);
    DeleteObject(bg);
    if (!dwmBorder_) {
        // Windows 10: square corners, so a GDI hairline is the right edge.
        const HBRUSH border = CreateSolidBrush(kPopupPalette.border);
        FrameRect(dc, &client, border);
        DeleteObject(border);
    }

    const auto old = static_cast<HFONT>(SelectObject(dc, font_));
    SetBkMode(dc, TRANSPARENT);
    const int pad = px(kPaddingX);
    const int top = client.top + px(4);

    for (std::size_t i = 0; i < rows_.size(); ++i) {
        const int edge = dwmBorder_ ? 0 : 1;
        RECT row{client.left + edge, top + rowHeight_ * static_cast<int>(i), client.right - edge,
                 top + rowHeight_ * static_cast<int>(i + 1)};
        const bool selected = static_cast<int>(i) == selected_;
        if (selected) {
            const HBRUSH sel = CreateSolidBrush(kPopupPalette.selection);
            FillRect(dc, &row, sel);
            DeleteObject(sel);
            // The one accent in the whole UI. Inset from the edge so it reads as a mark on
            // the row rather than as part of the window border.
            RECT bar{row.left + px(4), row.top + px(6), row.left + px(4) + px(kAccentWidth),
                     row.bottom - px(6)};
            const HBRUSH accent = CreateSolidBrush(kPopupPalette.accent);
            FillRect(dc, &bar, accent);
            DeleteObject(accent);
        }
        RECT text = row;
        text.left += pad;
        text.right -= pad;
        const auto& r = rows_[i];
        if (!r.context.empty()) {
            SetTextColor(dc, kPopupPalette.textDim);
            DrawTextW(dc, r.context.c_str(), static_cast<int>(r.context.size()), &text,
                      DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_NOCLIP);
            SIZE size{};
            GetTextExtentPoint32W(dc, r.context.c_str(), static_cast<int>(r.context.size()), &size);
            text.left += size.cx;
        }
        SetTextColor(dc, kPopupPalette.text);
        DrawTextW(dc, r.insert.c_str(), static_cast<int>(r.insert.size()), &text,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    }
    SelectObject(dc, old);
    EndPaint(hwnd_, &ps);
}

LRESULT CALLBACK SuggestionPopup::wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_NCCREATE) {
        const auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
    }
    auto* self = reinterpret_cast<SuggestionPopup*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
    case WM_PAINT:
        if (self != nullptr) self->paint();
        return 0;
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE; // clicks must not steal focus either
    case WM_ERASEBKGND:
        return 1;
    default:
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

} // namespace lankey::ui::win32
