cmake_minimum_required(VERSION 3.25)

project(LanKey
    VERSION 0.0.1
    DESCRIPTION "Open-source Vietnamese input method that learns the user's typing habits"
    LANGUAGES CXX)

# -- Options ----------------------------------------------------------------------
option(LANKEY_BUILD_TESTS "Build unit tests" ON)
option(LANKEY_ENGINE_OPENKEY "Build OpenKey engine adapter (default engine, ADR-002)" ON)

# -- C++ standard & common warnings for every LanKey target -----------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)   # for clang-tidy / IDE

# Interface target holding the compile flags; every LanKey target links to it.
# third_party does NOT link it, so /WX is never applied to upstream code.
add_library(lankey_options INTERFACE)
if(MSVC)
    target_compile_options(lankey_options INTERFACE
        /W4 /WX /permissive- /utf-8 /Zc:__cplusplus /Zc:preprocessor /EHsc)
else()
    target_compile_options(lankey_options INTERFACE
        -Wall -Wextra -Wpedantic -Werror -Wshadow -Wconversion -Wsign-conversion)
endif()
if(MINGW)
    # llvm-mingw ships libc++/libunwind as DLLs that live only in its own bin/ directory.
    # Link the runtime statically so lankey_tests.exe runs from any shell and from ctest.
    target_link_options(lankey_options INTERFACE -static)
endif()
if(WIN32)
    # Windows 10 API surface (GetDpiForWindow, SetProcessDpiAwarenessContext...).
    target_compile_definitions(lankey_options INTERFACE _WIN32_WINNT=0x0A00 WINVER=0x0A00 NTDDI_VERSION=0x0A000006)
endif()
# Every include in the repo is rooted at the top level: #include "core/model/KeyEvent.h"
target_include_directories(lankey_options INTERFACE ${CMAKE_SOURCE_DIR})

add_subdirectory(core)
if(WIN32)
    add_subdirectory(platform/win32)
    add_subdirectory(ui/win32)
    if(LANKEY_ENGINE_OPENKEY)
        add_subdirectory(app)
    endif()
endif()

if(LANKEY_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
