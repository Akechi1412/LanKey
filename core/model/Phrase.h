#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "core/model/Thresholds.h"

namespace lankey::core::model {

// One syllable as the Smart Layer sees it: the engine's composed output, NFC, case-folded.
// "Chương" and "chương" produce the same Syllable; the original casing is not kept here
// (re-applying the user's capitalisation is done at replacement time from what is on
// screen).
struct Syllable {
    std::u32string text; // "chương"

    // Build from engine output (already NFC in practice; normalised again as a guard).
    [[nodiscard]] static Syllable fromComposed(std::u32string_view composed);

    friend bool operator==(const Syllable&, const Syllable&) = default;
};

// 1..kMaxPhraseSyllables consecutive syllables from the same paragraph. This is the unit
// of learning and suggestion: "chương trình" is one Phrase, so it can be learned,
// suggested and corrected as a whole.
struct Phrase {
    std::vector<Syllable> syllables;

    // "chương trình" - the lexicon key and what the user sees.
    [[nodiscard]] std::u32string joined() const;
    [[nodiscard]] int syllableCount() const noexcept { return static_cast<int>(syllables.size()); }

    friend bool operator==(const Phrase&, const Phrase&) = default;
};

// The last kMaxPhraseSyllables committed syllables plus the one being typed.
//
// OWNED BY THE HOOK THREAD (InputPipeline). The worker only ever receives a copy inside
// SyllableCommitted and never mutates it: the SuggestionEngine needs this context on the
// hook thread itself.
struct PhraseWindow {
    // Oldest first; size <= kMaxPhraseSyllables. A vector, not a deque: it is copied into
    // every SyllableCommitted event and a deque copy allocates a 4 KB block for 5 items.
    std::vector<Syllable> committed;
    std::u32string current;       // composed text of the syllable being typed
    std::uint64_t generation = 0; // InputBuffer generation when `current` last changed

    void commit(Syllable syllable);
    void reset() noexcept;

    // Candidate phrases ending at the newest committed syllable, longest first:
    // [s-2 s-1 s0], [s-1 s0], [s0]. Empty when nothing is committed.
    [[nodiscard]] std::vector<Phrase> candidatePhrases() const;

    [[nodiscard]] bool empty() const noexcept { return committed.empty() && current.empty(); }
    [[nodiscard]] std::size_t committedCount() const noexcept { return committed.size(); }
};

} // namespace lankey::core::model
