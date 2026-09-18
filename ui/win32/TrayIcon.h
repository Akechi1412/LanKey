#pragma once

#include <functional>
#include <string>

#include "core/model/Settings.h"

#include "platform/win32/Win32.h"
#include "ui/win32/Theme.h"

namespace lankey::ui::win32 {

// System tray icon: "V" when Vietnamese is on, "E" when off. Left click toggles, right
// click opens the quick menu. Icons are drawn at runtime with GDI - no resource file.
//
// Threading: UI thread only. Owns a hidden message window; the caller's message loop
// dispatches to it.
class TrayIcon {
public:
    struct Callbacks {
        std::function<void()> onToggleVietnamese;
        std::function<void(core::model::InputMethod)> onInputMethod;
        std::function<void(bool)> onSuggestionsEnabled;
        std::function<void(bool)> onAutoCorrectEnabled;
        std::function<void()> onEraseAllData;
        std::function<void()> onShowData; // "Dữ liệu của bạn"
        std::function<void()> onQuit;
    };

    TrayIcon();
    ~TrayIcon();

    TrayIcon(const TrayIcon&) = delete;
    TrayIcon& operator=(const TrayIcon&) = delete;

    [[nodiscard]] bool create(HINSTANCE instance, Callbacks callbacks);
    void destroy();

    // Reflect state in the icon/tooltip/menu.
    void setState(bool vietnamese, core::model::InputMethod method, bool suggestions,
                  bool autoCorrect);
    void showBalloon(const std::wstring& title, const std::wstring& text);

    [[nodiscard]] HWND window() const noexcept { return hwnd_; }

private:
    static LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT handle(UINT msg, WPARAM wParam, LPARAM lParam);
    void showMenu();
    void updateIcon();
    [[nodiscard]] static HICON makeLetterIcon(wchar_t letter, COLORREF background);

    HINSTANCE instance_ = nullptr;
    HWND hwnd_ = nullptr;
    HICON iconVietnamese_ = nullptr;
    HICON iconEnglish_ = nullptr;
    UINT taskbarCreatedMessage_ = 0;
    Callbacks callbacks_;
    bool vietnamese_ = true;
    bool suggestions_ = true;
    bool autoCorrect_ = true;
    core::model::InputMethod method_ = core::model::InputMethod::Telex;
};

} // namespace lankey::ui::win32
