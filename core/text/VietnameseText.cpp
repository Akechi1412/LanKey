#include "core/text/VietnameseText.h"

#include <algorithm>
#include <iterator>

namespace lankey::core::text {

namespace {

struct ComposeEntry {
    char32_t first;
    char32_t mark;
    char32_t composed;
};

struct LetterEntry {
    char32_t letter;
    char32_t lower;
    char32_t upper;
    char32_t base;
};

#include "core/text/VietnameseTables.inc"

// Both tables are sorted by their first field, so lookups are binary searches over a few
// hundred entries - fast enough for the hook thread and allocation-free.
const LetterEntry* findLetter(char32_t c) noexcept {
    const auto* end = std::end(kLetters);
    const auto* it =
        std::lower_bound(std::begin(kLetters), end, c,
                         [](const LetterEntry& e, char32_t v) { return e.letter < v; });
    return (it != end && it->letter == c) ? it : nullptr;
}

char32_t findComposed(char32_t first, char32_t mark) noexcept {
    const auto* end = std::end(kCompose);
    const auto* it =
        std::lower_bound(std::begin(kCompose), end, first,
                         [](const ComposeEntry& e, char32_t v) { return e.first < v; });
    for (; it != end && it->first == first; ++it) {
        if (it->mark == mark) return it->composed;
    }
    return 0;
}

bool isCombiningMark(char32_t c) noexcept {
    switch (c) {
    case 0x0300: // grave
    case 0x0301: // acute
    case 0x0302: // circumflex
    case 0x0303: // tilde
    case 0x0306: // breve
    case 0x0309: // hook above
    case 0x031B: // horn
    case 0x0323: // dot below
        return true;
    default:
        return false;
    }
}

} // namespace

std::u32string nfc(std::u32string_view s) {
    std::u32string out;
    out.reserve(s.size());
    for (const char32_t c : s) {
        if (!out.empty() && isCombiningMark(c)) {
            if (const char32_t composed = findComposed(out.back(), c); composed != 0) {
                out.back() = composed;
                continue;
            }
        }
        out.push_back(c);
    }
    return out;
}

char32_t toLower(char32_t c) noexcept {
    if (c >= U'A' && c <= U'Z') return c + (U'a' - U'A');
    if (const auto* e = findLetter(c)) return e->lower;
    return c;
}

char32_t toUpper(char32_t c) noexcept {
    if (c >= U'a' && c <= U'z') return c - (U'a' - U'A');
    if (const auto* e = findLetter(c)) return e->upper;
    return c;
}

std::u32string caseFold(std::u32string_view s) {
    std::u32string out(s);
    for (auto& c : out)
        c = toLower(c);
    return out;
}

char32_t stripDiacritics(char32_t c) noexcept {
    if (const auto* e = findLetter(c)) return e->base;
    return c;
}

std::u32string stripDiacritics(std::u32string_view s) {
    std::u32string out(s);
    for (auto& c : out)
        c = stripDiacritics(c);
    return out;
}

bool isLetter(char32_t c) noexcept {
    if ((c >= U'a' && c <= U'z') || (c >= U'A' && c <= U'Z')) return true;
    return findLetter(c) != nullptr;
}

bool isAllLetters(std::u32string_view s) noexcept {
    return !s.empty() && std::ranges::all_of(s, isLetter);
}

bool hasDiacritic(char32_t c) noexcept {
    // Every Vietnamese letter is in the table; only those with a diacritic map to a
    // different base ("ă" -> "a", "đ" -> "d"; "a" -> "a").
    const auto* e = findLetter(c);
    return e != nullptr && e->base != c;
}

} // namespace lankey::core::text
