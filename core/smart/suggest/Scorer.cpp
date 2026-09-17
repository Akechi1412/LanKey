#include "core/smart/suggest/Scorer.h"

#include <algorithm>
#include <cmath>

namespace lankey::core::smart {

double Scorer::staticScore(const model::LexiconEntry& entry, std::int64_t nowUnixSeconds,
                           const model::SuggestionSettings& s) {
    constexpr double kSecondsPerDay = 86400.0;
    const double ageDays =
        std::max(0.0, static_cast<double>(nowUnixSeconds - entry.lastUsedAt) / kSecondsPerDay);
    return s.weightFrequency * std::log1p(static_cast<double>(entry.frequency)) +
           s.weightRecency * std::exp(-s.recencyLambda * ageDays) +
           s.weightPhraseLength * static_cast<double>(entry.syllableCount() - 1);
}

double Scorer::dynamicScore(std::size_t phraseLength, std::size_t typedLength, bool appMatch,
                            const model::SuggestionSettings& s) {
    const double saved =
        phraseLength == 0
            ? 0.0
            : static_cast<double>(phraseLength - std::min(typedLength, phraseLength)) /
                  static_cast<double>(phraseLength);
    return s.weightAppContext * (appMatch ? 1.0 : 0.0) + s.weightSavedKeystrokes * saved;
}

} // namespace lankey::core::smart
