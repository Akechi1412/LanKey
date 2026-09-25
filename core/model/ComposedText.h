#pragma once

#include <string>

namespace lankey::core::model {

// The syllable currently being composed, as the engine understands it, after processing
// the latest key. Always NFC (precomposed). Not case-folded - that belongs to the Smart Layer.
struct ComposedText {
    std::u32string text;
    // The engine applied at least one Vietnamese transformation (tone mark, circumflex,
    // horn, đ) to this syllable. AutoCorrect only considers syllables with this flag set;
    // otherwise every interleaved English word would look "not in dictionary" and get
    // wrongly corrected.
    bool vietnameseTransformApplied = false;
};

// What the engine returns for ONE KeyEvent. The engine knows nothing about the screen or
// SendInput; it only describes what to do with the text before the caret.
struct EngineResult {
    enum class Action {
        // The engine does not care about this key; the platform lets it through as usual.
        PassThrough,
        // The engine swallows the key and nothing changes on screen (e.g. the key was
        // blocked by spell check).
        Swallow,
        // The engine swallows the key and asks to delete `deleteCount` codepoints before
        // the caret, then insert `insert`. This is the only way the engine produces
        // diacritics.
        Replace,
    };

    Action action = Action::PassThrough;
    int deleteCount = 0;
    std::u32string insert;
    // Syllable state after processing this key (even for PassThrough).
    ComposedText composed;
    // Non-empty when the engine composed a Vietnamese syllable, decided it was not a word,
    // and is putting the raw keys back ("kh\u00f4ngg" -> "khoongg"): this is that composed
    // form, which exists nowhere else afterwards. The only evidence of what the typist
    // meant when a slip makes the syllable invalid - and `composed` is cleared at the same
    // moment, so without this the smart layer sees nothing to repair.
    std::u32string restoredFrom;
};

} // namespace lankey::core::model
