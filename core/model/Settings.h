#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/model/EngineSettings.h"
#include "core/model/Thresholds.h"

namespace lankey::core::model {

enum class AutoCorrectLevel : std::uint8_t { Off, Cautious, Balanced, Aggressive };

struct SuggestionSettings {
    bool enabled = true;
    int minPrefixLength = Thresholds::kMinPrefixForSuggest; // 1..4
    // The popup appears only after the user stops typing for this long (flicker guard).
    int idleDelayMs = Thresholds::kSuggestionIdleDelayMs;
    bool selectWithDigits = false;
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

    friend bool operator==(const AutoCorrectSettings&, const AutoCorrectSettings&) = default;
};

struct PrivacySettings {
    // Executable names (case-insensitive) in which nothing is learned or corrected.
    std::vector<std::string> excludedApps = {"keepass.exe", "1password.exe", "bitwarden.exe"};
    // Executable names in which suggestions are hidden (typing still works).
    std::vector<std::string> suggestionsDisabledApps;

    friend bool operator==(const PrivacySettings&, const PrivacySettings&) = default;
};

// Everything the user can configure. Serialised to settings.json (schemaVersion guards
// migrations). Published to the hook thread as an immutable snapshot.
struct Settings {
    static constexpr int kSchemaVersion = 1;

    int schemaVersion = kSchemaVersion;
    bool vietnameseEnabled = true;
    EngineSettings engine;
    SuggestionSettings suggestions;
    AutoCorrectSettings autoCorrect;
    PrivacySettings privacy;

    friend bool operator==(const Settings&, const Settings&) = default;
};

} // namespace lankey::core::model
