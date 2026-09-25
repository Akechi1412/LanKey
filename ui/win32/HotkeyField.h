#pragma once

#include "core/model/Hotkey.h"

#include "platform/win32/Win32.h"

namespace lankey::ui::win32 {

// A read-only EDIT that records a chord: click it, press the keys, done. Shows
// "Nhấn tổ hợp phím..." while focused and empty, "Chưa gán" when unassigned. Backspace or
// Delete clears, Esc restores the previous value. Every accepted change is reported to the
// parent as WM_COMMAND(id, EN_CHANGE). Pure Win32 subclassing; no state outside the window.
namespace hotkeyField {

HWND create(HWND parent, int id, HINSTANCE instance);
void set(HWND field, const core::model::Hotkey& hotkey);
[[nodiscard]] core::model::Hotkey get(HWND field);

} // namespace hotkeyField

} // namespace lankey::ui::win32
