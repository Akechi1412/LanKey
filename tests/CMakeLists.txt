find_package(GTest CONFIG REQUIRED)

add_executable(lankey_tests
    unit/engine_conformance.cpp   # contains main() - registers tests dynamically per adapter
    unit/engine_registry.cpp
    unit/typist_test.cpp
)
target_link_libraries(lankey_tests
    PRIVATE
        lankey_core
        GTest::gtest      # NOT gtest_main: custom main() calls RegisterTest
)

# Link whichever adapters are enabled so engine_registry.cpp can see them.
if(LANKEY_ENGINE_OPENKEY)
    target_link_libraries(lankey_tests PRIVATE lankey_engine_openkey)
endif()
if(LANKEY_ENGINE_VKEY)
    target_link_libraries(lankey_tests PRIVATE lankey_engine_vkey)
endif()

include(GoogleTest)
# DISCOVERY_MODE PRE_TEST: tests are registered at runtime (RegisterTest), so discovery
# must happen when ctest runs, not at build time.
gtest_discover_tests(lankey_tests DISCOVERY_MODE PRE_TEST)
