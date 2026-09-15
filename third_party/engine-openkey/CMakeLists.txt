# OpenKey engine (GPL-3.0) - upstream code, compiled with its own flags (no /WX, no -Werror).
# See UPSTREAM.md for provenance and patches.

add_library(openkey_engine STATIC
    src/Engine.cpp
    src/Vietnamese.cpp
    src/Macro.cpp
    src/SmartSwitchKey.cpp
    src/ConvertTool.cpp
)
# SYSTEM: the adapter is built with -Werror and must not fail on upstream header warnings.
target_include_directories(openkey_engine SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_definitions(openkey_engine PUBLIC OPENKEY_PORTABLE_KEYCODES)
target_compile_features(openkey_engine PUBLIC cxx_std_20)
if(MSVC)
    target_compile_options(openkey_engine PRIVATE /utf-8 /w)
else()
    target_compile_options(openkey_engine PRIVATE -w)
endif()
