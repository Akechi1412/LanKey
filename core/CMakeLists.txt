# lankey_core - pure C++20, ZERO Win32. Must build on Linux (CI job linux-clang).
# Phase 0: header-only (model + interfaces), hence an INTERFACE library.
# Switch to STATIC as soon as the first .cpp lands (Phase 1a).

add_library(lankey_core INTERFACE)
target_link_libraries(lankey_core INTERFACE lankey_options)
target_sources(lankey_core INTERFACE
    FILE_SET HEADERS
    BASE_DIRS ${CMAKE_SOURCE_DIR}
    FILES
        model/KeyEvent.h
        model/EngineSettings.h
        model/ComposedText.h
        interfaces/IVietnameseEngine.h
)

# -- Engine adapters (Phase 0: enable each one to run the conformance tests) ---------
# Each adapter is its own target wrapping upstream code in third_party/. Upstream code is
# compiled with its own flags (no /WX); the adapter is compiled with lankey_options.

if(LANKEY_ENGINE_OPENKEY)
    add_subdirectory(${CMAKE_SOURCE_DIR}/third_party/engine-openkey engine-openkey)
    add_library(lankey_engine_openkey STATIC engine/OpenKeyEngineAdapter.cpp)
    target_link_libraries(lankey_engine_openkey PUBLIC lankey_core PRIVATE openkey_engine)
    target_compile_definitions(lankey_engine_openkey PUBLIC LANKEY_ENGINE_OPENKEY)
endif()
