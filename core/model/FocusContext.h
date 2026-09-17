#pragma once

#include <cstdint>
#include <string>

namespace lankey::core::model {

// Where the keystrokes are going. Published by the platform as an immutable snapshot on
// every focus change; read on the hook thread through IFocusObserver::current().
struct FocusContext {
    std::string appName;          // executable name, e.g. "notepad.exe"; empty = unknown
    bool isPasswordField = false; // UIA IsPassword / ES_PASSWORD; true = never learn
    std::uintptr_t windowId = 0;  // opaque platform handle for caching (HWND on win32)

    friend bool operator==(const FocusContext&, const FocusContext&) = default;
};

} // namespace lankey::core::model
