#include "core/text/VietnameseDistance.h"

#include <algorithm>
#include <cstddef>
#include <vector>

#include "core/text/VietnameseText.h"

namespace lankey::core::text {

namespace {

// One unit per consonant digraph, in the private use area so they never collide with
// real letters.
constexpr char32_t kCh = 0xE001;
constexpr char32_t kTr = 0xE002;
constexpr char32_t kNg = 0xE003;
constexpr char32_t kNgh = 0xE004;
constexpr char32_t kNh = 0xE005;
constexpr char32_t kTh = 0xE006;
constexpr char32_t kPh = 0xE007;
constexpr char32_t kKh = 0xE008;
constexpr char32_t kGh = 0xE009;
constexpr char32_t kGi = 0xE00A;

constexpr int kSameVowel = 2;  // 0.4
constexpr int kConfusable = 3; // 0.6
constexpr int kFull = 5;       // 1.0

bool isBaseVowel(char32_t base) noexcept {
    return base == U'a' || base == U'e' || base == U'i' || base == U'o' || base == U'u' ||
           base == U'y';
}

bool isVowelLetter(char32_t c) noexcept {
    return isBaseVowel(stripDiacritics(c));
}

// Dialect confusion groups (PLAN 6.3); 0 = none.
int confusableGroup(char32_t unit) noexcept {
    switch (unit) {
    case U'x':
    case U's':
        return 1;
    case kCh:
    case kTr:
        return 2;
    case U'd':
    case kGi:
    case U'r':
        return 3;
    case U'n':
    case U'l':
        return 4;
    case U'c':
    case U'k':
    case U'q':
        return 5;
    case kNg:
    case kNgh:
        return 6;
    case U'g':
    case kGh:
        return 7;
    default:
        return 0;
    }
}

} // namespace

// Split a syllable into units: digraphs collapse to one symbol, every other code point
// stands alone. Greedy, longest digraph first ("ngh" before "ng" before "n").
void VietnameseDistance::tokenize(std::u32string_view s, Units& out) {
    out.clear();
    const std::size_t n = s.size();
    const auto at = [&](std::size_t i) { return i < n ? s[i] : U'\0'; };
    for (std::size_t i = 0; i < n;) {
        const char32_t c = s[i];
        const char32_t next = at(i + 1);
        char32_t unit = c;
        std::size_t len = 1;
        switch (c) {
        case U'n':
            if (next == U'g') {
                if (at(i + 2) == U'h') {
                    unit = kNgh;
                    len = 3;
                } else {
                    unit = kNg;
                    len = 2;
                }
            } else if (next == U'h') {
                unit = kNh;
                len = 2;
            }
            break;
        case U'c':
            if (next == U'h') {
                unit = kCh;
                len = 2;
            }
            break;
        case U't':
            if (next == U'r') {
                unit = kTr;
                len = 2;
            } else if (next == U'h') {
                unit = kTh;
                len = 2;
            }
            break;
        case U'p':
            if (next == U'h') {
                unit = kPh;
                len = 2;
            }
            break;
        case U'k':
            if (next == U'h') {
                unit = kKh;
                len = 2;
            }
            break;
        case U'g':
            if (next == U'h') {
                unit = kGh;
                len = 2;
            } else if (stripDiacritics(next) == U'i') {
                // "gi" is the onset only when a vowel follows the i ("giá", "giường"); in
                // "gì" the i is the nucleus and only the g belongs to the onset.
                unit = kGi;
                len = isVowelLetter(at(i + 2)) ? 2 : 1;
            }
            break;
        default:
            break;
        }
        out.push_back(unit);
        i += len;
    }
}

namespace {

int substitutionCost(char32_t a, char32_t b) noexcept {
    if (a == b) return 0;
    if (isVowelLetter(a) && isVowelLetter(b) && stripDiacritics(a) == stripDiacritics(b)) {
        return kSameVowel;
    }
    const int group = confusableGroup(a);
    if (group != 0 && group == confusableGroup(b)) return kConfusable;
    return kFull;
}

} // namespace

char32_t VietnameseDistance::unitClass(char32_t unit) noexcept {
    if (isVowelLetter(unit)) return stripDiacritics(unit);
    if (const int group = confusableGroup(unit); group != 0) {
        return static_cast<char32_t>(0xE100 + group);
    }
    return unit;
}

int VietnameseDistance::scaled(std::u32string_view a, std::u32string_view b) {
    // Buffers persist per thread so no call allocates after warm-up.
    static thread_local Units ua;
    static thread_local Units ub;
    tokenize(a, ua);
    tokenize(b, ub);
    return scaledUnits(ua, ub);
}

int VietnameseDistance::scaledUnits(std::span<const char32_t> ua, std::span<const char32_t> ub) {
    // Worker-thread hot loop (a fuzzy search calls this for every candidate).
    const std::size_t m = ua.size();
    const std::size_t n = ub.size();
    if (m == 0) return static_cast<int>(n) * kFull;
    if (n == 0) return static_cast<int>(m) * kFull;

    // Two rolling rows over the columns of b, in a fixed-size stack buffer: syllables are
    // at most kMaxSyllableLength code points, and anything longer is not a syllable.
    constexpr std::size_t kMaxUnits = 40;
    if (n >= kMaxUnits || m >= kMaxUnits) return static_cast<int>(std::max(m, n)) * kFull;
    int rows[2][kMaxUnits + 1];
    int* prev = rows[0];
    int* cur = rows[1];
    for (std::size_t j = 0; j <= n; ++j)
        prev[j] = static_cast<int>(j) * kFull;

    for (std::size_t i = 1; i <= m; ++i) {
        cur[0] = static_cast<int>(i) * kFull;
        const char32_t ai = ua[i - 1];
        for (std::size_t j = 1; j <= n; ++j) {
            const int sub = prev[j - 1] + substitutionCost(ai, ub[j - 1]);
            const int del = prev[j] + kFull;
            const int ins = cur[j - 1] + kFull;
            cur[j] = std::min(sub, std::min(del, ins));
        }
        std::swap(prev, cur);
    }
    return prev[n];
}

} // namespace lankey::core::text
