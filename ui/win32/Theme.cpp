#include "ui/win32/Theme.h"

#include <algorithm>
#include <cmath>
#include <dwmapi.h>

// clang-format off
#include <objidl.h> // gdiplus.h needs PROPID and IStream declared first
#include <gdiplus.h>
// clang-format on

namespace lankey::ui::win32 {

HFONT createUiFont(UINT dpi, int pointSize, int weight) {
    const int height = -MulDiv(pointSize, static_cast<int>(dpi), 72);
    HFONT font = CreateFontW(height, 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                             OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                             DEFAULT_PITCH, L"Segoe UI Variable Text");
    // CreateFont happily returns a substitute when the face is missing; check what we got.
    if (font != nullptr) {
        LOGFONTW actual{};
        GetObjectW(font, sizeof(actual), &actual);
        const HDC dc = GetDC(nullptr);
        const auto old = static_cast<HFONT>(SelectObject(dc, font));
        wchar_t face[LF_FACESIZE] = {};
        GetTextFaceW(dc, LF_FACESIZE, face);
        SelectObject(dc, old);
        ReleaseDC(nullptr, dc);
        if (_wcsicmp(face, L"Segoe UI Variable Text") != 0) {
            DeleteObject(font);
            font = nullptr;
        }
    }
    if (font == nullptr) {
        font = CreateFontW(height, 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                           OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                           DEFAULT_PITCH, L"Segoe UI");
    }
    return font;
}

bool applyRoundedCorners(HWND hwnd) {
    // DWMWA_WINDOW_CORNER_PREFERENCE = 33, DWMWCP_ROUNDSMALL = 3 (Windows 11 22000+).
    // Older systems return E_INVALIDARG and keep square corners.
    const DWORD preference = 3;
    return SUCCEEDED(DwmSetWindowAttribute(hwnd, 33, &preference, sizeof(preference)));
}

void setBorderColor(HWND hwnd, COLORREF colour) {
    // DWMWA_BORDER_COLOR = 34 (Windows 11 22000+), takes a COLORREF.
    DwmSetWindowAttribute(hwnd, 34, &colour, sizeof(colour));
}

namespace {

void ensureGdiplus() {
    static const ULONG_PTR token = [] {
        Gdiplus::GdiplusStartupInput input;
        ULONG_PTR t = 0;
        Gdiplus::GdiplusStartup(&t, &input, nullptr);
        return t;
    }();
    (void)token;
}

Gdiplus::Color gp(COLORREF c, BYTE alpha = 255) {
    return Gdiplus::Color(alpha, GetRValue(c), GetGValue(c), GetBValue(c));
}

void addRoundedRect(Gdiplus::GraphicsPath& path, Gdiplus::REAL x, Gdiplus::REAL y, Gdiplus::REAL w,
                    Gdiplus::REAL h, Gdiplus::REAL r) {
    path.AddArc(x, y, 2 * r, 2 * r, 180.0f, 90.0f);
    path.AddArc(x + w - 2 * r, y, 2 * r, 2 * r, 270.0f, 90.0f);
    path.AddArc(x + w - 2 * r, y + h - 2 * r, 2 * r, 2 * r, 0.0f, 90.0f);
    path.AddArc(x, y + h - 2 * r, 2 * r, 2 * r, 90.0f, 90.0f);
    path.CloseFigure();
}

} // namespace

void drawToggle(HDC dc, const RECT& bounds, const wchar_t* label, const ToggleFace& face,
                COLORREF background, COLORREF text, UINT dpi) {
    ensureGdiplus();
    const auto scale = static_cast<float>(dpi) / 96.0f;
    const float box = std::round(18.0f * scale);
    const float x = static_cast<float>(bounds.left) + 1.0f;
    const float y = static_cast<float>(bounds.top) +
                    (static_cast<float>(bounds.bottom - bounds.top) - box) / 2.0f;

    {
        const RECT all = bounds;
        const HBRUSH bg = CreateSolidBrush(background);
        FillRect(dc, &all, bg);
        DeleteObject(bg);
    }

    // Idle: white well with a grey rim. Checked: filled with the brand colour. Pressed
    // darkens the rim / fill a step; disabled greys everything.
    constexpr COLORREF kRim = RGB(0x8A, 0x94, 0xA6);
    constexpr COLORREF kRimPressed = RGB(0x5B, 0x64, 0x70);
    constexpr COLORREF kRimDisabled = RGB(0xD5, 0xDA, 0xE1);
    constexpr COLORREF kWellDisabled = RGB(0xF0, 0xF2, 0xF5);
    constexpr COLORREF kAccentPressed = RGB(0x24, 0x57, 0xB8);
    constexpr COLORREF kAccentDisabled = RGB(0xA9, 0xC0, 0xEE);
    const COLORREF accent = face.disabled  ? kAccentDisabled
                            : face.pressed ? kAccentPressed
                                           : kBrandVietnamese;
    const COLORREF rim = face.disabled ? kRimDisabled : face.pressed ? kRimPressed : kRim;

    Gdiplus::Graphics g(dc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
    const float rimWidth = std::max(1.0f, std::round(1.0f * scale));
    if (face.radio) {
        const Gdiplus::RectF circle(x, y, box, box);
        if (face.checked) {
            Gdiplus::SolidBrush fill(gp(accent));
            g.FillEllipse(&fill, circle);
            const float dot = std::round(box * 0.38f);
            Gdiplus::SolidBrush ink(gp(RGB(0xFF, 0xFF, 0xFF)));
            g.FillEllipse(&ink, x + (box - dot) / 2, y + (box - dot) / 2, dot, dot);
        } else {
            Gdiplus::SolidBrush well(gp(face.disabled ? kWellDisabled : RGB(0xFF, 0xFF, 0xFF)));
            g.FillEllipse(&well, circle);
            Gdiplus::Pen pen(gp(rim), rimWidth);
            g.DrawEllipse(&pen, x + rimWidth / 2, y + rimWidth / 2, box - rimWidth, box - rimWidth);
        }
    } else {
        const float radius = std::round(box * 0.22f);
        Gdiplus::GraphicsPath path;
        if (face.checked) {
            addRoundedRect(path, x, y, box, box, radius);
            Gdiplus::SolidBrush fill(gp(accent));
            g.FillPath(&fill, &path);
            Gdiplus::Pen tick(gp(RGB(0xFF, 0xFF, 0xFF)), std::max(1.5f, std::round(2.0f * scale)));
            tick.SetStartCap(Gdiplus::LineCapRound);
            tick.SetEndCap(Gdiplus::LineCapRound);
            tick.SetLineJoin(Gdiplus::LineJoinRound);
            const Gdiplus::PointF pts[3] = {{x + box * 0.24f, y + box * 0.53f},
                                            {x + box * 0.42f, y + box * 0.71f},
                                            {x + box * 0.77f, y + box * 0.32f}};
            g.DrawLines(&tick, pts, 3);
        } else {
            addRoundedRect(path, x + rimWidth / 2, y + rimWidth / 2, box - rimWidth, box - rimWidth,
                           radius);
            Gdiplus::SolidBrush well(gp(face.disabled ? kWellDisabled : RGB(0xFF, 0xFF, 0xFF)));
            g.FillPath(&well, &path);
            Gdiplus::Pen pen(gp(rim), rimWidth);
            g.DrawPath(&pen, &path);
        }
    }

    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, face.disabled ? RGB(0x9A, 0xA2, 0xAE) : text);
    RECT textRect{bounds.left + static_cast<int>(box) + MulDiv(9, static_cast<int>(dpi), 96),
                  bounds.top, bounds.right, bounds.bottom};
    DrawTextW(dc, label, -1, &textRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    if (face.focused) {
        RECT ink = textRect;
        DrawTextW(dc, label, -1, &ink,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_CALCRECT);
        const int pad = MulDiv(3, static_cast<int>(dpi), 96);
        RECT ring{ink.left - pad, bounds.top, ink.right + pad, bounds.bottom};
        DrawFocusRect(dc, &ring);
    }
}

namespace {
constexpr wchar_t kDialogNavProp[] = L"LanKey.DialogNavigation";
}

void enableDialogNavigation(HWND top) {
    SetPropW(top, kDialogNavProp, reinterpret_cast<HANDLE>(1));
}

bool wantsDialogNavigation(HWND top) {
    return top != nullptr && GetPropW(top, kDialogNavProp) != nullptr;
}

void showHandCursor() {
    SetCursor(LoadCursorW(nullptr, IDC_HAND));
}

HICON logoIcon(HINSTANCE instance, int size) {
    return static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(1), IMAGE_ICON, size, size,
                                         LR_DEFAULTCOLOR | LR_SHARED));
}

void setWindowIcons(HWND hwnd, HINSTANCE instance) {
    const UINT dpi = GetDpiForWindow(hwnd);
    const HICON small =
        logoIcon(instance, static_cast<int>(GetSystemMetricsForDpi(SM_CXSMICON, dpi)));
    const HICON big = logoIcon(instance, static_cast<int>(GetSystemMetricsForDpi(SM_CXICON, dpi)));
    if (small != nullptr)
        SendMessageW(hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(small));
    if (big != nullptr) SendMessageW(hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(big));
}

} // namespace lankey::ui::win32
