#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/model/Phrase.h"

namespace lankey::core::model {

enum class CorrectionSource : std::uint8_t {
    CorrectionMap,  // learned from the user's own manual fixes
    BaseDictionary, // fuzzy match against the base syllable set
};

// Output of ICorrector (worker thread): "the last `syllableCount` committed syllables
// should read `corrected`". The hook thread turns this into a TextReplacement itself: it
// alone knows what is on screen (typed casing, the exact separators) and whether the
// generation still matches.
struct Correction {
    int syllableCount = 1;                // how many syllables at the end of the window
    std::vector<Syllable> corrected;      // replacement syllables (folded), same count
    std::u32string wrongKey;              // joined folded wrong span: correction_map key
    std::uint64_t expectedGeneration = 0; // from the SyllableCommitted event
    CorrectionSource source = CorrectionSource::BaseDictionary;
    double confidence = 0.0;
};

enum class CorrectionRuleSource : std::uint8_t { Learned, User, Builtin };

// One correction_map row: what the user has taught us by fixing themselves.
struct CorrectionRule {
    std::u32string wrong;   // joined, folded; may be a phrase ("sữa lỗi")
    std::u32string correct; // joined, folded
    double confidence = 0.0;
    int timesApplied = 0;
    int timesRejected = 0;
    CorrectionRuleSource source = CorrectionRuleSource::Learned;
    std::int64_t updatedAt = 0; // unix seconds; the rejection cooldown counts from here
};

} // namespace lankey::core::model
