#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "core/model/Phrase.h"

namespace lankey::core::model {

// What the SuggestionEngine is asked on every keystroke (hook thread).
struct SuggestionQuery {
    // Case-folded text of the last committed syllables, oldest first. Views into the
    // caller's PhraseWindow: valid for the duration of the suggest() call only.
    std::vector<std::u32string_view> context;
    std::u32string typed;  // the syllable being typed, exactly as on screen ("Chư")
    std::u32string prefix; // the same, case-folded ("chư") - the lookup key
    std::string appName;
};

struct Suggestion {
    Phrase phrase;         // the full phrase, e.g. "chương trình"
    std::u32string insert; // replaces `typed` on screen, casing re-applied: "Chương trình"
    int deleteCount = 0;   // len(typed): the context syllables are already on screen
    double score = 0.0;
};

using SuggestionList = std::vector<Suggestion>;

} // namespace lankey::core::model
