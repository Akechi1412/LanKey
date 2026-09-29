#pragma once

#include <cstdint>
#include <optional>

#include "core/interfaces/ICaretResolver.h"

#include "platform/win32/Win32.h"

namespace lankey::platform::win32 {

// ICaretResolver fallback chain:
//   1. GetGUIThreadInfo -> rcCaret (classic Win32 controls; exact when present)
//   2. UI Automation: TextPattern2::GetCaretRange, then TextPattern::GetSelection - each
//      widened to the previous character / the line when the provider returns no
//      rectangles for the empty caret range (Chromium/Electron do that)
//   3. MSAA: AccessibleObjectFromWindow(OBJID_CARET) on the focused window - what older
//      toolkits (Java AWT, Qt 4, Delphi, Office pre-UIA) expose instead of UIA
//   4. nullopt: the UI shows the popup in the top-right corner of the monitor that holds
//      the foreground window (never at the mouse position)
//
// Threading: UI thread only (UIA is COM; the thread is initialised as an apartment by
// App). Called only when a popup is about to be shown, so its cost (tens of ms) never
// lands on a keystroke.
class CaretResolver final : public core::ICaretResolver {
public:
    // Which step of the chain answered last time. Kept because "the popup is in the
    // corner" is otherwise impossible to tell apart from "the popup is at the caret and
    // the caret is in the corner", and because which step answers is per-application.
    enum class Source : std::uint8_t {
        None,        // nobody could say; the popup falls back to a screen corner
        NoTextFocus, // the provider said the keyboard is NOT in a text element
        GuiThread,
        UiaCaretRange,
        UiaSelection,
        Msaa,
    };

    // Whether the keyboard is currently in something that takes text. Separate from
    // resolve() because "where does the caret go" and "is there anywhere for it to go"
    // are different questions, and only the second one can answer "definitely nowhere".
    //
    //   Editable - a text element with keyboard focus that is not read-only
    //   NotText  - keystrokes have nowhere to land. Either a text element that is
    //              read-only (in a browser, the page itself, which is where the focus
    //              falls back to when the user clicks away from an input) or a control
    //              that never takes text at all (a button, a tree item, a status bar).
    //   Unknown  - the element says nothing either way (terminals, panes, older
    //              toolkits). Nothing may be inferred from it.
    enum class TextFocus : std::uint8_t { Unknown, Editable, NotText };
    [[nodiscard]] TextFocus textFocus();
    // UIA control type id of the element textFocus() last looked at (50004 Edit, 50030
    // Document, 50033 Pane, 50023 TreeItem...). Diagnostic: which types show up in real
    // applications is the thing that decides what may safely be treated as "not text".
    [[nodiscard]] int lastControlType() const noexcept { return lastControlType_; }

    // Whether a UIA control type never accepts typed text. Public so the reasoning can be
    // tested without a window on screen.
    [[nodiscard]] static bool isNonTextControl(int controlType) noexcept;

    [[nodiscard]] std::optional<core::model::ScreenRect> resolve() override;
    [[nodiscard]] Source lastSource() const noexcept { return lastSource_; }
    [[nodiscard]] static const char* sourceName(Source s) noexcept;
    [[nodiscard]] static const char* focusName(TextFocus f) noexcept;

private:
    Source lastSource_ = Source::None;
    int lastControlType_ = 0;
};

} // namespace lankey::platform::win32
