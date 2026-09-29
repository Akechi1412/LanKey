#pragma once

#include <functional>
#include <string>
#include <vector>

#include "platform/win32/Win32.h"

namespace lankey::ui::win32 {

// Ctrl+Alt+S: pick a snippet by name when the abbreviation is the thing you forgot.
//
// A palette, not a dialog: it opens over whatever is in front, filters as you type, and
// closes the moment you choose or press Esc. Taking the focus is unavoidable - it has a
// text box - so it remembers the window that had it and hands the focus back before the
// text is sent, otherwise the snippet would land in the picker's own search box.
//
// Threading: UI thread only.
class SnippetPicker {
public:
    struct Item {
        std::wstring abbr;
        std::wstring preview; // the first line of the body
    };
    struct Callbacks {
        // Called after the focus is back on the user's window: insert this abbreviation.
        std::function<void(const std::wstring& abbr)> onChoose;
    };

    SnippetPicker() = default;
    ~SnippetPicker();

    SnippetPicker(const SnippetPicker&) = delete;
    SnippetPicker& operator=(const SnippetPicker&) = delete;

    void show(HINSTANCE instance, std::vector<Item> items, Callbacks callbacks);
    void hide();
    void destroy();
    [[nodiscard]] bool visible() const noexcept;

private:
    static LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK searchProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
                                       UINT_PTR id, DWORD_PTR ref);
    LRESULT handle(UINT msg, WPARAM wParam, LPARAM lParam);
    // Sizes the window to the number of rows actually showing, up to a cap, and places
    // the field and the list inside it. A palette with a fixed height is mostly empty
    // space, which is what made this look unfinished.
    void resizeToContent();
    void paintChrome(HDC dc);
    void refilter();
    void move(int delta);
    void chooseSelected();
    void drawRow(const DRAWITEMSTRUCT& item);

    HWND hwnd_ = nullptr;
    HWND search_ = nullptr;
    HWND list_ = nullptr;
    HFONT font_ = nullptr;
    HFONT bold_ = nullptr;
    HFONT large_ = nullptr; // the search field, which is the thing you look at first
    HBRUSH background_ = nullptr;
    HWND target_ = nullptr; // the window that had the focus when we opened
    std::vector<Item> items_;
    std::vector<int> shown_; // rows on screen -> items_ index
    Callbacks callbacks_;
};

} // namespace lankey::ui::win32
