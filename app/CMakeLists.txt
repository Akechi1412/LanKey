# lankey.exe - wiring only (manual DI), no logic of its own.

# DATA-POLICY.md is the single source of truth for "Dữ liệu của bạn": compiled into the
# binary as one L"..." literal per line so the dialog can never drift from the document.
set(LANKEY_POLICY_MD ${CMAKE_SOURCE_DIR}/DATA-POLICY.md)
set(LANKEY_POLICY_INC ${CMAKE_CURRENT_BINARY_DIR}/generated/lankey/DataPolicy.inc)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${LANKEY_POLICY_MD})
file(STRINGS ${LANKEY_POLICY_MD} _lankey_policy_lines ENCODING UTF-8)
set(_lankey_policy_body "// Generated from DATA-POLICY.md by app/CMakeLists.txt. Do not edit.\n")
foreach(_line IN LISTS _lankey_policy_lines)
    string(REPLACE "\\" "\\\\" _line "${_line}")
    string(REPLACE "\"" "\\\"" _line "${_line}")
    string(APPEND _lankey_policy_body "L\"${_line}\\n\"\n")
endforeach()
file(WRITE ${LANKEY_POLICY_INC}.tmp "${_lankey_policy_body}")
execute_process(COMMAND ${CMAKE_COMMAND} -E copy_if_different
    ${LANKEY_POLICY_INC}.tmp ${LANKEY_POLICY_INC})
file(REMOVE ${LANKEY_POLICY_INC}.tmp)

enable_language(RC)
# The resource script pulls in the icon and the manifest; CMake's RC dependency scan does
# not see them, so a regenerated lankey.ico would otherwise never reach the executable.
set_source_files_properties(lankey.rc PROPERTIES OBJECT_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/lankey.ico;${CMAKE_CURRENT_SOURCE_DIR}/lankey.manifest")
add_executable(lankey WIN32
    main.cpp
    App.cpp
    Log.cpp
    lankey.rc
)
if(MSVC)
    # The manifest comes from lankey.rc; the linker must not generate a second one.
    target_link_options(lankey PRIVATE /MANIFEST:NO)
endif()
target_include_directories(lankey PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated)
target_link_libraries(lankey
    PRIVATE
        lankey_core
        lankey_engine_openkey
        lankey_platform_win32
        lankey_ui
        user32 shell32 ole32 advapi32)
set_target_properties(lankey PROPERTIES OUTPUT_NAME "lankey")
if(MINGW)
    # wWinMain (Unicode entry point) needs the unicode CRT startup on MinGW.
    target_link_options(lankey PRIVATE -municode)
endif()
