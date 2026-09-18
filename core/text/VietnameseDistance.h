#pragma once

#include <span>
#include <string_view>
#include <vector>

namespace lankey::core::text {

// Weighted Levenshtein distance for Vietnamese syllables (PLAN 6.3).
//
// Costs reflect how Vietnamese typos actually happen:
//   substitute same base vowel      0.4   "ữ" vs "ử", "a" vs "ă" vs "â" - a tone/modifier slip
//   substitute confusable consonant 0.6   x/s, ch/tr, d/gi/r, n/l, c/k/q, ng/ngh, g/gh (dialect)
//   substitute anything else        1.0
//   insert / delete                 1.0
//
// No transposition: the plan's 0.8 adjacent swap (optimal string alignment) breaks the
// triangle inequality, and a metric index silently loses neighbours without it (d(aự,au)=0.4
// went missing behind ựa). Telex makes swaps rare anyway - the engine places the tone
// itself and absorbs most key-order slips. Substitution classes (same base vowel,
// confusable group) are equivalence classes with class-internal cost below any cross-class
// cost, so this is a true metric.
//
// Consonant digraphs (ch, tr, ng, ngh, nh, th, ph, kh, gh, gi) are one unit each, so
// "ch" -> "tr" is a single 0.6 substitution and not two edits.
//
// Distances are returned SCALED BY kScale so they are exact integers (every cost is a
// multiple of 0.2): integer thresholds, no floating-point comparisons anywhere.
//
// Inputs must be NFC and case-folded (Syllable::text). Pure, allocation-free after the
// first call on a thread; safe on any thread.
class VietnameseDistance {
public:
    static constexpr int kScale = 5; // 1.0 -> 5, 0.4 -> 2, 0.6 -> 3

    [[nodiscard]] static int scaled(std::u32string_view a, std::u32string_view b);
    [[nodiscard]] static double of(std::u32string_view a, std::u32string_view b) {
        return static_cast<double>(scaled(a, b)) / kScale;
    }

    // The unit sequence of a syllable (digraphs collapsed). The fuzzy index stores these
    // for its keys and tokenizes the query once, so a search never re-tokenizes anything.
    using Units = std::vector<char32_t>;
    static void tokenize(std::u32string_view s, Units& out);
    // The substitution class of a unit: units in the same class cost less than a full
    // edit to swap (same base vowel, same confusable consonant group); every other pair
    // costs a full edit. Lets an index treat a syllable as a skeleton of classes.
    [[nodiscard]] static char32_t unitClass(char32_t unit) noexcept;
    [[nodiscard]] static int scaledUnits(std::span<const char32_t> a, std::span<const char32_t> b);
};

} // namespace lankey::core::text
