# lankey_ui - tray icon and suggestion popup (Win32 + GDI in v0.1).
# May include platform/win32/Win32.h (the <windows.h> wrapper) but never the hook, sender,
# focus or caret classes: the UI only receives data from core.

add_library(lankey_ui STATIC
    Theme.cpp
    TrayIcon.cpp
    SuggestionPopup.cpp
    DataDialog.cpp
    LanguageToast.cpp
    SettingsWindow.cpp
    CustomMethodDialog.cpp
    HotkeyField.cpp
)
target_link_libraries(lankey_ui
    PUBLIC lankey_core
    PRIVATE user32 gdi32 gdiplus shell32 shcore dwmapi advapi32 comctl32 comdlg32)
