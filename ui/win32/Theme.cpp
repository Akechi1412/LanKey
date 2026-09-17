#include "ui/win32/Theme.h"

#include <dwmapi.h>

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

} // namespace lankey::ui::win32
