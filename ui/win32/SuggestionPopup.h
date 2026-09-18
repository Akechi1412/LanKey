#pragma once

#include <optional>
#include <string>
#include <vector>

#include "core/model/Geometry.h"
#include "core/model/Suggestion.h"

#include "platform/win32/Win32.h"
#include "ui/win32/Theme.h"

namespace lankey::ui::win32 {

// The suggestion list, drawn to the right of the caret - above it by default, below when
// there is no room above; top-right corner of the current monitor when the caret is
// unknown. Never covers the line being typed. Created once; show()/hide() only move and
// toggle it.
//
// Each row reads as the finished phrase: the context already on screen in a dim tone, the
// part Tab/Enter will insert in the text colour. The selected row carries the one accent
// of the whole UI, a 3 px bar in the brand blue. One fixed palette (see Theme.h) that
// stands off both dark and light fields; corners and the 1 px edge are drawn by DWM on
// Windows 11.
//
// WS_EX_NOACTIVATE is mandatory: the popup must never take focus from the application the
// user is typing into. WS_EX_TOOLWINDOW keeps it out of Alt+Tab, WS_EX_TOPMOST above the
// app.
//
// Threading: UI thread only.
class SuggestionPopup {
public:
    SuggestionPopup();
    ~SuggestionPopup();

    SuggestionPopup(const SuggestionPopup&) = delete;
    SuggestionPopup& operator=(const SuggestionPopup&) = delete;

    [[nodiscard]] bool create(HINSTANCE instance);
    void destroy();

    // `caret` unknown (nullopt) -> top-right corner of the foreground monitor.
    void show(const core::model::SuggestionList& items, int selected,
              const std::optional<core::model::ScreenRect>& caret);
    void hide();
    [[nodiscard]] bool visible() const noexcept { return visible_; }

    // A one-line transient note ("đưởng → đường") in the dim colour, same placement as the
    // list, no selection. hideNotice() hides only if a notice is what is showing.
    void showNotice(const std::u32string& text,
                    const std::optional<core::model::ScreenRect>& caret);
    void hideNotice();

private:
    struct Row {
        std::wstring context; // already on screen, drawn dim ("hệ ")
        std::wstring insert;  // what Tab/Enter inserts, drawn in the text colour
    };

    static LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    void paint();
    void layout(const std::optional<core::model::ScreenRect>& caret);
    void ensureFont(UINT dpi);

    HWND hwnd_ = nullptr;
    HFONT font_ = nullptr;
    UINT fontDpi_ = 0;
    std::vector<Row> rows_;
    int selected_ = 0;
    int rowHeight_ = 28;
    int width_ = 220;
    UINT shownDpi_ = 0; // DPI width_ was computed at; a monitor change resets the sticky width
    bool visible_ = false;
    bool notice_ = false;    // the single row is a note, not a selectable item
    bool dwmBorder_ = false; // DWM draws the edge (Windows 11); else paint() does
};

} // namespace lankey::ui::win32
