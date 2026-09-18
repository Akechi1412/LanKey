#pragma once

#include "platform/win32/Win32.h"
#include "ui/win32/Theme.h"

namespace lankey::ui::win32 {

// A quiet, wordless-enough confirmation that the input language changed: the brand tile
// (V or E) and one line, bottom-right of the monitor the user is working on, gone after
// a second and a half. Never takes focus, never makes a sound.
//
// Threading: UI thread only. One window, created on first use, shown/hidden after that.
class LanguageToast {
public:
    LanguageToast() = default;
    ~LanguageToast();

    LanguageToast(const LanguageToast&) = delete;
    LanguageToast& operator=(const LanguageToast&) = delete;

    void show(HINSTANCE instance, bool vietnamese);
    void destroy();

private:
    static LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    void paint();
    void place();

    HWND hwnd_ = nullptr;
    HFONT font_ = nullptr;
    UINT fontDpi_ = 0;
    bool vietnamese_ = true;
};

} // namespace lankey::ui::win32
