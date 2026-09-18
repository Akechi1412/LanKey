#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "core/text/VietnameseDistance.h"

namespace lankey::core::smart {

// Every syllable within a given VietnameseDistance of a misspelling, exactly, with a
// handful of distance computations (PLAN 6.2 chose a BK-tree; this replaces it - see
// ADR-011).
//
// The metric has two kinds of edits: cheap substitutions inside a class (same base vowel
// 0.4, confusable consonants 0.6) and full edits (anything else, insert, delete: 1.0).
// Map every unit to its class and a syllable becomes a "skeleton": "đường", "đưởng" and
// "đương" all share one. Two syllables are within 1.5 only if their skeletons are at most
// one full edit apart, so the SymSpell trick applies to skeletons: index each key under
// its skeleton and under every skeleton with one unit deleted; look the query up the same
// way; verify the few candidates with the exact metric. Radii above 1.5 are not exact
// (two full edits) and not used.
//
// Build once from the base dictionary (DB thread, ~ms), then read from any thread; a
// second small index carries the user's own trusted syllables.
class FuzzyIndex {
public:
    struct Match {
        std::u32string_view key;
        int scaledDistance; // VietnameseDistance::scaled units (5 = 1.0)
    };

    // Keys must be NFC + case-folded. Duplicates are ignored.
    void insert(std::u32string_view key);

    // Every key with scaled distance <= maxScaled (<= 7), nearest first. `out` is cleared.
    void search(std::u32string_view query, int maxScaled, std::vector<Match>& out) const;

    [[nodiscard]] std::size_t size() const noexcept { return keys_.size(); }
    [[nodiscard]] bool empty() const noexcept { return keys_.empty(); }

    // Exactness holds up to one full edit: 1.0 + 0.4 = 1.4 is the largest radius below
    // the next full edit (2.0).
    static constexpr int kMaxExactRadiusScaled = 9;

private:
    struct Entry {
        std::u32string key;
        text::VietnameseDistance::Units units;
    };
    std::vector<Entry> keys_;
    // skeleton (or skeleton with one unit removed) -> indices into keys_
    std::unordered_map<std::u32string, std::vector<std::uint32_t>> buckets_;
};

} // namespace lankey::core::smart
