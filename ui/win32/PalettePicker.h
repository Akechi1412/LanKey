#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

#include "platform/win32/Win32.h"

namespace lankey::ui::win32 {

// The palette behind Ctrl+Alt+S and Ctrl+Alt+V: a dark card over whatever is in front,
// with a search field, a list that filters as you type, and the keys along the bottom.
//
// One class for both because the hard part is not the list - it is taking the focus and
// giving it back. A palette has a text box, so it must take the focus; the text then has
// to go to the window that had it, which means handing the focus back BEFORE hiding (see
// choose()). Getting that wrong sends the text nowhere, and it is not worth getting wrong
// twice in two files.
//
// Threading: UI thread only.
class PalettePicker {
public:
    struct Row {
        std::wstring primary;   // what the row is called
        std::wstring secondary; // what it holds, shown dim
    };
    // The words that differ between one palette and the next. Everything else - the
    // colours, the sizes, the key handling - is the same on purpose.
    struct Labels {
        std::wstring cue;   // placeholder in the search box
        std::wstring empty; // when the filter matches nothing
        std::wstring hints; // the line along the bottom
        // Width of the primary column at 96 dpi, or 0 for "let the primary text use the
        // whole row and right-align the secondary".
        int primaryColumn = 0;
    };
    struct Callbacks {
        // The index into the rows that were handed to show(), not into what is on screen
        // after filtering. Called once the focus is back where it came from.
        std::function<void(std::size_t index)> onChoose;
    };

    PalettePicker() = default;
    ~PalettePicker();

    PalettePicker(const PalettePicker&) = delete;
    PalettePicker& operator=(const PalettePicker&) = delete;

    void show(HINSTANCE instance, Labels labels, std::vector<Row> rows, Callbacks callbacks);
    void hide();
    void destroy();
    [[nodiscard]] bool visible() const noexcept;

private:
    static LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK searchProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
                                       UINT_PTR id, DWORD_PTR ref);
    LRESULT handle(UINT msg, WPARAM wParam, LPARAM lParam);
    // Sizes the window to the number of rows actually showing, up to a cap, and places the
    // field and the list inside it. A palette with a fixed height is mostly empty space.
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
    Labels labels_;
    std::vector<Row> rows_;
    std::vector<int> shown_; // lines on screen -> rows_ index
    Callbacks callbacks_;
};

} // namespace lankey::ui::win32
