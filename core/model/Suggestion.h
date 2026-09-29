#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "core/model/Language.h"
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
    // Which glossary column the user is writing in right now: Vietnamese mode means they
    // typed Vietnamese, so the other columns are what they might want instead.
    Language sourceLanguage = Language::Vietnamese;
};

struct Suggestion {
    // Where this came from. A Phrase is something the user has written before; a
    // Conversion is a row of their own VI-EN-JA glossary and a Snippet an abbreviation
    // from their snippets.json. The last two are the user's own data, typed on purpose,
    // which is why they outrank everything the smart layer guessed and why the popup
    // labels them.
    enum class Kind : std::uint8_t { Phrase, Conversion, Snippet };

    Phrase phrase;         // the full phrase, e.g. "chương trình"
    std::u32string insert; // replaces `typed` on screen, casing re-applied: "Chương trình"
    int deleteCount = 0;   // len(typed): the context syllables are already on screen
    double score = 0.0;
    Kind kind = Kind::Phrase;
    // Conversion only: which column `insert` came from (the popup shows EN/JA), and how
    // many ALREADY COMMITTED syllables it replaces. The count, not a character count:
    // only the pipeline knows what those syllables look like on screen - their casing,
    // and the separators typed between them.
    Language language = Language::Vietnamese;
    int replacedSyllables = 0;
};

using SuggestionList = std::vector<Suggestion>;

} // namespace lankey::core::model
