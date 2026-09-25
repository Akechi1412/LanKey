#pragma once

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
    [[nodiscard]] std::optional<core::model::ScreenRect> resolve() override;
};

} // namespace lankey::platform::win32
