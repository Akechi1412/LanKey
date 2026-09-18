#pragma once

#include <cstdint>
#include <vector>

#include "core/model/FocusContext.h"
#include "core/model/Phrase.h"

namespace lankey::core::model {

// Hook thread -> worker event: "a syllable just ended". Value type, cheap to copy; the
// worker owns its copy and never touches the hook thread's PhraseWindow.
struct SyllableCommitted {
    PhraseWindow window;     // copy, already includes the syllable just committed
    char32_t terminator = 0; // ' ', '.', ',', '\n', '\t'... or 0 for navigation keys /
                             // focus loss. Needed to retype correctly on AutoCorrect,
                             // and to skip AutoCorrect after Enter (chat apps send).
    bool vietnameseTransformApplied = false;
    FocusContext focus;
    std::int64_t timestampMs = 0;
    std::uint64_t generation = 0; // InputBuffer generation AFTER the terminator

    // Non-empty when this commit completed a retype: the user deleted the last
    // `retypedFrom.size()` committed syllables and typed the same number again. The window
    // already holds the new syllables; these are the old ones (folded). What the
    // ManualCorrectionDetector learns correction_map from.
    std::vector<Syllable> retypedFrom;
    // The user deleted into text the pipeline never saw (typed before LanKey, or past the
    // phrase window), so `syllable()` may be only the tail of what is on screen. Never
    // corrected, never learned.
    bool uncertain = false;

    // The syllable that was just committed (newest in the window).
    [[nodiscard]] const Syllable& syllable() const { return window.committed.back(); }
};

} // namespace lankey::core::model
