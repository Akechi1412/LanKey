# lankey_core - pure C++20, ZERO Win32. Must build on Linux (CI job linux-clang).

find_package(tl-expected CONFIG REQUIRED)
find_package(unofficial-sqlite3 CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

# The base syllable dictionary is compiled into the binary: data/vi_base_syllables.txt ->
# one U"..." literal per syllable. No file to find at runtime, no decoding, and the exe
# stays self-contained. Regenerated whenever the data file changes (CMAKE_CONFIGURE_DEPENDS).
set(LANKEY_SYLLABLES_TXT ${CMAKE_SOURCE_DIR}/data/vi_base_syllables.txt)
set(LANKEY_SYLLABLES_INC ${CMAKE_CURRENT_BINARY_DIR}/generated/lankey/BaseSyllablesData.inc)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${LANKEY_SYLLABLES_TXT})
file(STRINGS ${LANKEY_SYLLABLES_TXT} _lankey_syllable_lines ENCODING UTF-8)
set(_lankey_syllables_body "// Generated from data/vi_base_syllables.txt by core/CMakeLists.txt. Do not edit.
")
set(_lankey_syllable_count 0)
foreach(_line IN LISTS _lankey_syllable_lines)
    if(_line MATCHES "^#" OR _line STREQUAL "")
        continue()
    endif()
    string(APPEND _lankey_syllables_body "U\"${_line}\",
")
    math(EXPR _lankey_syllable_count "${_lankey_syllable_count} + 1")
endforeach()
file(WRITE ${LANKEY_SYLLABLES_INC}.tmp "${_lankey_syllables_body}")
execute_process(COMMAND ${CMAKE_COMMAND} -E copy_if_different
    ${LANKEY_SYLLABLES_INC}.tmp ${LANKEY_SYLLABLES_INC})
file(REMOVE ${LANKEY_SYLLABLES_INC}.tmp)
message(STATUS "LanKey: embedded ${_lankey_syllable_count} base syllables")

add_library(lankey_core STATIC
    text/VietnameseText.cpp
    text/VietnameseDistance.cpp
    text/Utf.cpp
    text/WidthConverter.cpp
    model/Phrase.cpp
    pipeline/WordBoundaryDetector.cpp
    pipeline/InputPipeline.cpp
    smart/privacy/PrivacyFilter.cpp
    smart/learn/LearningRecorder.cpp
    smart/suggest/Scorer.cpp
    smart/suggest/PhraseTrie.cpp
    smart/suggest/SuggestionEngine.cpp
    smart/correct/BaseSyllableSet.cpp
    smart/correct/FuzzyIndex.cpp
    smart/correct/AutoCorrectEngine.cpp
    smart/correct/ManualCorrectionDetector.cpp
    smart/SmartWorker.cpp
    storage/Migrations.cpp
    storage/SqliteLexiconStore.cpp
    storage/JsonSettingsStore.cpp
)
target_link_libraries(lankey_core
    PUBLIC lankey_options tl::expected
    PRIVATE unofficial::sqlite3::sqlite3 nlohmann_json::nlohmann_json)
target_include_directories(lankey_core PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated)
target_sources(lankey_core PUBLIC
    FILE_SET HEADERS
    BASE_DIRS ${CMAKE_SOURCE_DIR}
    FILES
        model/AtomicSnapshot.h
        model/ComposedText.h
        model/Correction.h
        model/EngineSettings.h
        model/Error.h
        model/FocusContext.h
        model/Geometry.h
        model/KeyEvent.h
        model/Lexicon.h
        model/Phrase.h
        model/Settings.h
        model/Suggestion.h
        model/SyllableCommitted.h
        model/TextReplacement.h
        model/Thresholds.h
        interfaces/ICaretResolver.h
        interfaces/IClock.h
        interfaces/ICorrector.h
        interfaces/IFocusObserver.h
        interfaces/IKeySource.h
        interfaces/IDataProtector.h
        interfaces/ILexiconStore.h
        interfaces/IPrivacyRule.h
        interfaces/ISettingsChannel.h
        interfaces/ISuggestionProvider.h
        interfaces/ITextSink.h
        interfaces/IVietnameseEngine.h
        pipeline/InputBuffer.h
        pipeline/InputPipeline.h
        pipeline/ToggleHotkey.h
        pipeline/WordBoundaryDetector.h
        smart/privacy/PrivacyFilter.h
        smart/learn/LearningRecorder.h
        smart/suggest/Scorer.h
        smart/suggest/PhraseTrie.h
        smart/suggest/SuggestionEngine.h
        smart/correct/BaseSyllableSet.h
        smart/correct/FuzzyIndex.h
        smart/correct/AutoCorrectEngine.h
        smart/correct/ManualCorrectionDetector.h
        smart/SmartWorker.h
        storage/JsonSettingsStore.h
        storage/Migrations.h
        storage/SqliteLexiconStore.h
        text/VietnameseText.h
        text/VietnameseDistance.h
        text/Utf.h
        threading/SpscQueue.h
        threading/TaskThread.h
        util/SystemClock.h
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
