#pragma once

#include <cstdint>
#include <vector>

#include "core/model/Phrase.h"

namespace lankey::core::model {

// One learned phrase with its statistics, as stored in user_lexicon.
struct LexiconEntry {
    Phrase phrase;
    std::uint32_t frequency = 0;
    std::int64_t firstSeenAt = 0; // unix seconds
    std::int64_t lastUsedAt = 0;
    bool pinned = false;  // never cleaned up
    bool blocked = false; // never suggested

    [[nodiscard]] int syllableCount() const noexcept { return phrase.syllableCount(); }
};

// An increment to apply to one entry; batched in RAM and flushed by the DB thread.
struct LexiconDelta {
    Phrase phrase;
    std::int32_t frequencyDelta = 1;
    std::int64_t usedAt = 0; // unix seconds
};

using LexiconDeltaBatch = std::vector<LexiconDelta>;

} // namespace lankey::core::model
