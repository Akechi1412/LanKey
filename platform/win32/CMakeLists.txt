# lankey_platform_win32 - the only code that talks to Win32 on the input side.
# Depends on core (model + interfaces) and nothing else in the repo.

add_library(lankey_platform_win32 STATIC
    Win32.cpp
    KeyboardHook.cpp
    InputSender.cpp
    FocusWatcher.cpp
    CaretResolver.cpp
)
target_link_libraries(lankey_platform_win32
    PUBLIC lankey_core
    PRIVATE user32 ole32 oleaut32 uuid)
# uiautomationcore is loaded by COM at runtime; only the IIDs are linked (uuid).
