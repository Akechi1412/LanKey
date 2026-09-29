#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "core/model/EngineSettings.h"
#include "core/model/Hotkey.h"
#include "core/model/Thresholds.h"

namespace lankey::core::model {

enum class AutoCorrectLevel : std::uint8_t { Off, Cautious, Balanced, Aggressive };

struct SuggestionSettings {
    bool enabled = true;
    int minPrefixLength = Thresholds::kMinPrefixForSuggest; // 1..4
    // The popup appears only after the user stops typing for this long (flicker guard).
    int idleDelayMs = Thresholds::kSuggestionIdleDelayMs;
    bool selectWithDigits = false;
    // Enter accepts the highlighted suggestion. Off by default: in a chat window or a
    // form, Enter means "send", and taking it would cost the user a message.
    bool selectWithEnter = false;
    // Scoring weights (see SuggestionEngine). Tunable without a rebuild.
    double weightFrequency = 1.0;
    double weightRecency = 0.8;
    double weightAppContext = 0.3;
    double weightSavedKeystrokes = 0.4;
    double weightPhraseLength = 0.3;
    double recencyLambda = 0.05; // half-life ~ 14 days

    friend bool operator==(const SuggestionSettings&, const SuggestionSettings&) = default;
};

struct AutoCorrectSettings {
    AutoCorrectLevel level = AutoCorrectLevel::Cautious;
    // Executable names (case-insensitive) where nothing is ever corrected. Empty by
    // default: the transform guard already keeps identifiers, commands and English out of
    // reach, and Vietnamese is typed in editors and terminals too (comments, commits, chat).
    std::vector<std::string> excludedApps;

    friend bool operator==(const AutoCorrectSettings&, const AutoCorrectSettings&) = default;
};

struct PrivacySettings {
    // Executable names (case-insensitive) in which nothing is learned or corrected.
    std::vector<std::string> excludedApps = {"keepass.exe", "1password.exe", "bitwarden.exe"};
    // Executable names in which suggestions are hidden (typing still works).
    std::vector<std::string> suggestionsDisabledApps;

    friend bool operator==(const PrivacySettings&, const PrivacySettings&) = default;
};

enum class SendKeysMode : std::uint8_t { Batch, KeyByKey };

struct AdvancedSettings {
    // How replacements reach the application: one SendInput batch (fast) or one key at a
    // time (for applications that drop batched input).
    SendKeysMode sendKeys = SendKeysMode::Batch;

    friend bool operator==(const AdvancedSettings&, const AdvancedSettings&) = default;
};

// One row of the user's VI-EN-JA glossary, UTF-8 as it is written to settings.json. Any
// column except `note` may be empty; a row with fewer than two filled columns has nothing
// to convert between and is dropped when the index is built.
struct GlossaryEntry {
    std::string vi;
    std::string en;
    std::string ja;
    std::string note;

    friend bool operator==(const GlossaryEntry&, const GlossaryEntry&) = default;
};

// One abbreviation and what it expands to. `body` may contain newlines and the {holes}
// SnippetTemplate understands.
struct SnippetEntry {
    std::string abbr;
    std::string body;
    // Expand the moment the ending key is typed, instead of offering it in the popup.
    // Off unless the user asks for it per snippet: text appearing without being asked for
    // is the thing people hate most about snippet tools.
    bool autoExpand = false;

    friend bool operator==(const SnippetEntry&, const SnippetEntry&) = default;
};

struct SnippetSettings {
    // name -> value, usable as {name} in any body. Kept sorted by name: unlike the list of
    // snippets there is no order to preserve here, and sorting keeps settings.json stable
    // across saves so a diff shows only what actually changed.
    std::vector<std::pair<std::string, std::string>> variables;
    std::vector<SnippetEntry> items;

    friend bool operator==(const SnippetSettings&, const SnippetSettings&) = default;
};

// Remember Vietnamese/English per application: the mode the user last chose while that
// executable had focus is restored when it regains focus.
struct LanguageMemory {
    bool enabled = false;
    // executable name (lowercase) -> true = Vietnamese
    std::vector<std::pair<std::string, bool>> perApp;

    friend bool operator==(const LanguageMemory&, const LanguageMemory&) = default;
};

// Everything the user can configure. Serialised to settings.json (schemaVersion guards
// migrations). Published to the hook thread as an immutable snapshot.
struct Settings {
    // 2: the glossary and the snippets moved in here from dictionary.csv and
    //    snippets.json. A file still saying 1 is one whose rows have not been imported yet.
    static constexpr int kSchemaVersion = 2;

    int schemaVersion = kSchemaVersion;
    bool vietnameseEnabled = true;
    EngineSettings engine;
    SuggestionSettings suggestions;
    AutoCorrectSettings autoCorrect;
    PrivacySettings privacy;
    AdvancedSettings advanced;
    LanguageMemory languageMemory;
    HotkeySettings hotkeys;
    // The user's own words. They live here rather than in files of their own so there is
    // one place to edit, one place to back up, and one file to watch.
    std::vector<GlossaryEntry> glossary;
    SnippetSettings snippets;

    friend bool operator==(const Settings&, const Settings&) = default;
};

} // namespace lankey::core::model
