find_package(GTest CONFIG REQUIRED)

find_package(nlohmann_json CONFIG REQUIRED)
find_package(unofficial-sqlite3 CONFIG REQUIRED)

add_executable(lankey_tests
    main.cpp                      # registers the dynamic suites, then RUN_ALL_TESTS
    unit/engine_conformance.cpp   # one suite per registered engine adapter
    unit/engine_registry.cpp
    unit/typist_test.cpp
    unit/vietnamese_text_test.cpp
    unit/phrase_test.cpp
    unit/word_boundary_detector_test.cpp
    unit/input_pipeline_test.cpp
    unit/fakes_test.cpp
    unit/privacy_filter_test.cpp
    unit/learning_recorder_test.cpp
    unit/phrase_trie_test.cpp
    unit/suggestion_engine_test.cpp
    unit/pipeline_suggestions_test.cpp
    unit/lexicon_store_test.cpp
    unit/smart_worker_test.cpp
    unit/json_settings_store_test.cpp
    unit/toggle_hotkey_test.cpp
    replay/ReplayHarness.cpp
    replay/replay_test.cpp        # one test per directory in replay/fixtures/
    replay/replay_harness_test.cpp
    bench/pipeline_bench_test.cpp   # hot-path cost, prints numbers
)
target_link_libraries(lankey_tests
    PRIVATE
        lankey_core
        GTest::gtest      # NOT gtest_main: custom main() calls RegisterTest
        nlohmann_json::nlohmann_json
        unofficial::sqlite3::sqlite3   # lexicon_store_test pokes the schema directly
)
target_compile_definitions(lankey_tests PRIVATE
    LANKEY_REPLAY_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/replay/fixtures")

# Link whichever adapters are enabled so engine_registry.cpp can see them.
if(LANKEY_ENGINE_OPENKEY)
    target_link_libraries(lankey_tests PRIVATE lankey_engine_openkey)
endif()

include(GoogleTest)
# DISCOVERY_MODE PRE_TEST: tests are registered at runtime (RegisterTest), so discovery
# must happen when ctest runs, not at build time.
gtest_discover_tests(lankey_tests DISCOVERY_MODE PRE_TEST)
