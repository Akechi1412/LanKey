#pragma once

#include "platform/win32/Win32.h"

namespace lankey::ui::win32 {

// The whole visual identity in one place. Two brand colours (cobalt blue for Vietnamese,
// graphite for English) and one popup palette.
//
// The popup does not follow the Windows theme: what matters is the background of the
// field being typed into, which is unknown, so the surface is chosen to stand off both
// extremes at once. A slate-navy card is clearly lighter and bluer than dark editors
// (~#1E1E1E) and obviously a raised object on white; the mid-grey edge keeps roughly 3.5:1
// contrast against both.
struct Palette {
    COLORREF background;
    COLORREF border;    // 1 px edge, drawn by DWM along the rounded corners
    COLORREF text;      // the part Tab will insert
    COLORREF textDim;   // context that is already on screen
    COLORREF selection; // selected row background
    COLORREF accent;    // selected row bar (brand blue, lifted for the dark surface)
};

inline constexpr COLORREF kBrandVietnamese = RGB(0x2D, 0x6C, 0xDF); // cobalt blue
inline constexpr COLORREF kBrandEnglish = RGB(0x5B, 0x64, 0x70);    // graphite

inline constexpr Palette kPopupPalette{
    .background = RGB(0x2B, 0x31, 0x40),
    .border = RGB(0x6E, 0x7A, 0x90),
    .text = RGB(0xF4, 0xF6, 0xFA),
    .textDim = RGB(0xA6, 0xAE, 0xBD),
    .selection = RGB(0x3A, 0x43, 0x58),
    .accent = RGB(0x6A, 0xA1, 0xFF),
};

// Segoe UI Variable Text (Windows 11) when present, Segoe UI otherwise. `pointSize` at
// 72 dpi; scaled to `dpi`. Caller owns the HFONT.
[[nodiscard]] HFONT createUiFont(UINT dpi, int pointSize, int weight);

// Windows 11 rounded corners for a popup window. Returns false on older Windows, where the
// caller must draw its own edge.
bool applyRoundedCorners(HWND hwnd);

// Windows 11 draws a 1 px border that follows the rounded corners; a GDI FrameRect would be
// square and get clipped. No-op on older Windows.
void setBorderColor(HWND hwnd, COLORREF colour);

} // namespace lankey::ui::win32
