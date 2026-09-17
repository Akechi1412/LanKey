#pragma once

#include <cstdint>
#include <string>

#include "core/model/TextReplacement.h"

namespace lankey::core::model {

enum class CorrectionSource : std::uint8_t {
    CorrectionMap,  // learned from the user's own manual fixes
    BaseDictionary, // fuzzy match against the base syllable set
};

// Output of ICorrector (worker thread). `replacement` already carries the generation
// check; `original` is kept so Undo can restore exactly what the user typed.
struct Correction {
    TextReplacement replacement;
    std::u32string original; // e.g. "chuơng" (without terminator)
    CorrectionSource source = CorrectionSource::BaseDictionary;
    double confidence = 0.0;
};

} // namespace lankey::core::model
