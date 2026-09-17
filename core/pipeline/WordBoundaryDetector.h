#pragma once

#include "core/model/KeyEvent.h"

namespace lankey::core::pipeline {

// What a key does to the syllable/phrase structure. Pure classification, no state.
//
//   None      - part of the syllable being typed (letters, VNI digits, Backspace...).
//   Syllable  - ends the syllable but not the phrase: space, tab, comma, quotes, brackets.
//               "chương trình" is still one phrase across the space.
//   Paragraph - ends the syllable AND the phrase context: Enter, sentence punctuation
//               (. ! ? ; :), navigation keys, Escape. Nothing typed after this may be
//               joined to what came before it.
//
// Mouse clicks and focus changes are also Paragraph boundaries; they arrive through
// InputPipeline::onPointerClick / onFocusChanged rather than as keys.
enum class Boundary { None, Syllable, Paragraph };

class WordBoundaryDetector {
public:
    [[nodiscard]] static Boundary classify(const model::KeyEvent& key) noexcept;

    // The character to remember as the terminator of a committed syllable, so an
    // AutoCorrect retype can reproduce it: the key's own character, or a control
    // character for Enter/Tab, or 0 for navigation keys.
    [[nodiscard]] static char32_t terminatorFor(const model::KeyEvent& key) noexcept;
};

} // namespace lankey::core::pipeline
