#pragma once

#include <cstddef>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_set>

namespace lankey::core::smart {

// The closed set of syllables that exist in Vietnamese (PLAN 5.7): ~6.7k entries compiled
// into the binary from data/vi_base_syllables.txt, NFC and lowercase. Membership is the
// first question AutoCorrect asks; the same list seeds the fuzzy index.
//
// Immutable after construction; safe to read from any thread.
class BaseSyllableSet {
public:
    // The compiled-in dictionary.
    [[nodiscard]] static const BaseSyllableSet& builtin();

    // Any list (tests). Entries must be NFC + case-folded.
    explicit BaseSyllableSet(std::span<const char32_t* const> syllables);

    [[nodiscard]] bool contains(std::u32string_view syllable) const;
    [[nodiscard]] std::size_t size() const noexcept { return set_.size(); }

    void forEach(const std::function<void(std::u32string_view)>& fn) const;

private:
    struct Hash {
        using is_transparent = void;
        std::size_t operator()(std::u32string_view s) const noexcept {
            return std::hash<std::u32string_view>{}(s);
        }
    };
    struct Eq {
        using is_transparent = void;
        bool operator()(std::u32string_view a, std::u32string_view b) const noexcept {
            return a == b;
        }
    };
    std::unordered_set<std::u32string, Hash, Eq> set_;
};

} // namespace lankey::core::smart
