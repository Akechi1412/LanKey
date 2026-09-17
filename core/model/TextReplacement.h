#pragma once

#include <cstdint>
#include <string>

namespace lankey::core::model {

enum class ReplacementReason : std::uint8_t {
    Engine,      // Telex/VNI transformation of the syllable being typed
    Suggestion,  // user picked a suggestion
    AutoCorrect, // asynchronous correction after a commit
    Macro,
    Undo, // restoring what the user typed after a rejected AutoCorrect
};

// The only way core changes text on screen: "delete N code points before the caret, then
// insert this". The platform decides how (one SendInput batch, key by key, clipboard...).
//
// expectedGeneration is the InputBuffer generation the command was computed against.
// The hook thread applies the command only if the generation still matches; otherwise
// the user has typed in the meantime and the command would eat their text, so it is
// dropped. Commands created on the hook thread itself always match.
struct TextReplacement {
    int deleteCount = 0;
    std::u32string insert;
    std::uint64_t expectedGeneration = 0;
    ReplacementReason reason = ReplacementReason::Engine;

    friend bool operator==(const TextReplacement&, const TextReplacement&) = default;
};

} // namespace lankey::core::model
