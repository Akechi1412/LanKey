# lankey.exe - wiring only (manual DI), no logic of its own.

add_executable(lankey WIN32
    main.cpp
    App.cpp
    Log.cpp
)
target_link_libraries(lankey
    PRIVATE
        lankey_core
        lankey_engine_openkey
        lankey_platform_win32
        lankey_ui
        user32 shell32 ole32)
set_target_properties(lankey PROPERTIES OUTPUT_NAME "lankey")
if(MINGW)
    # wWinMain (Unicode entry point) needs the unicode CRT startup on MinGW.
    target_link_options(lankey PRIVATE -municode)
endif()
