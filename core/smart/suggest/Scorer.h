#pragma once

#include <cstddef>
#include <cstdint>

#include "core/model/Lexicon.h"
#include "core/model/Settings.h"

namespace lankey::core::smart {

// Ranking of lexicon entries, split in two so the hook thread never computes log/exp:
//
//   static  = w1*log(1+frequency) + w2*exp(-lambda*ageDays) + w5*(syllableCount-1)
//             computed once per snapshot rebuild (recency drifts slowly; 30 s is fine)
//   dynamic = w3*appMatch + w4*(len(phrase)-len(typed))/len(phrase)
//             cheap, computed at query time
//
// Weights live in SuggestionSettings so they can be tuned without a rebuild.
struct Scorer {
    [[nodiscard]] static double staticScore(const model::LexiconEntry& entry,
                                            std::int64_t nowUnixSeconds,
                                            const model::SuggestionSettings& s);

    [[nodiscard]] static double dynamicScore(std::size_t phraseLength, std::size_t typedLength,
                                             bool appMatch, const model::SuggestionSettings& s);
};

} // namespace lankey::core::smart
