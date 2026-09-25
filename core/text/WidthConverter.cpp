#include "core/text/WidthConverter.h"

#include <cstddef>
#include <unordered_map>

namespace lankey::core::text {

namespace {

// Full-width code point of each half-width katakana U+FF61..U+FF9F, in order.
constexpr char32_t kHalfKanaFirst = 0xFF61;
constexpr char32_t kHalfKanaLast = 0xFF9F;
constexpr char32_t kDakuten = 0xFF9E;    // ﾞ
constexpr char32_t kHandakuten = 0xFF9F; // ﾟ
constexpr char32_t kFullKana[] = {
    0x3002, 0x300C, 0x300D, 0x3001, 0x30FB, 0x30F2, 0x30A1, 0x30A3, 0x30A5, 0x30A7, 0x30A9,
    0x30E3, 0x30E5, 0x30E7, 0x30C3, 0x30FC, 0x30A2, 0x30A4, 0x30A6, 0x30A8, 0x30AA, 0x30AB,
    0x30AD, 0x30AF, 0x30B1, 0x30B3, 0x30B5, 0x30B7, 0x30B9, 0x30BB, 0x30BD, 0x30BF, 0x30C1,
    0x30C4, 0x30C6, 0x30C8, 0x30CA, 0x30CB, 0x30CC, 0x30CD, 0x30CE, 0x30CF, 0x30D2, 0x30D5,
    0x30D8, 0x30DB, 0x30DE, 0x30DF, 0x30E0, 0x30E1, 0x30E2, 0x30E4, 0x30E6, 0x30E8, 0x30E9,
    0x30EA, 0x30EB, 0x30EC, 0x30ED, 0x30EF, 0x30F3, 0x309B, 0x309C,
};
static_assert(sizeof(kFullKana) / sizeof(kFullKana[0]) == kHalfKanaLast - kHalfKanaFirst + 1);

// Latin-1 symbols with a full-width form. Not derivable from an offset: ¦ and ¬ swap
// places in the full-width block (U+00A6 -> U+FFE4, U+00AC -> U+FFE2).
struct SymbolPair {
    char32_t half;
    char32_t full;
};
constexpr SymbolPair kSymbols[] = {
    {0x00A2, 0xFFE0}, // ¢
    {0x00A3, 0xFFE1}, // £
    {0x00AC, 0xFFE2}, // ¬
    {0x00AF, 0xFFE3}, // ¯
    {0x00A6, 0xFFE4}, // ¦
    {0x00A5, 0xFFE5}, // ¥
};

constexpr char32_t fullSymbol(char32_t half) noexcept {
    for (const auto& pair : kSymbols) {
        if (pair.half == half) return pair.full;
    }
    return 0;
}
constexpr char32_t halfSymbol(char32_t full) noexcept {
    for (const auto& pair : kSymbols) {
        if (pair.full == full) return pair.half;
    }
    return 0;
}

constexpr bool isHalfKana(char32_t c) noexcept {
    return c >= kHalfKanaFirst && c <= kHalfKanaLast;
}
constexpr bool isHalfAscii(char32_t c) noexcept {
    return c >= 0x20 && c <= 0x7E;
}
constexpr bool isFullAscii(char32_t c) noexcept {
    return (c >= 0xFF01 && c <= 0xFF5E) || c == 0x3000;
}
// Full-width forms that come from a half-width kana: katakana block plus the punctuation
// and marks in kFullKana.
constexpr bool isFullKana(char32_t c) noexcept {
    return (c >= 0x30A1 && c <= 0x30FC) || c == 0x3001 || c == 0x3002 || c == 0x300C ||
           c == 0x300D || c == 0x309B || c == 0x309C;
}

// Voiced (dakuten) and semi-voiced (handakuten) forms. For most rows the voiced form is
// the next code point and the semi-voiced one the one after; five katakana are irregular
// because their voiced forms were encoded apart from the base (ウ→ヴ, ワ→ヷ, ヰ→ヸ,
// ヱ→ヹ, ヲ→ヺ). Missing those is what "ﾜﾞ" and "ｦﾞ" not converting looked like.
struct IrregularVoiced {
    char32_t base;
    char32_t voiced;
};
constexpr IrregularVoiced kIrregularVoiced[] = {
    {0x30A6, 0x30F4}, // ウ -> ヴ
    {0x30EF, 0x30F7}, // ワ -> ヷ
    {0x30F0, 0x30F8}, // ヰ -> ヸ
    {0x30F1, 0x30F9}, // ヱ -> ヹ
    {0x30F2, 0x30FA}, // ヲ -> ヺ
};

// The voiced form of a full-width katakana, or 0 when it has none.
constexpr char32_t voicedForm(char32_t base) noexcept {
    for (const auto& irregular : kIrregularVoiced) {
        if (irregular.base == base) return irregular.voiced;
    }
    const bool regular = (base >= 0x30AB && base <= 0x30C1 && (base - 0x30AB) % 2 == 0) || // カ..チ
                         (base >= 0x30C4 && base <= 0x30C8 && (base - 0x30C4) % 2 == 0) || // ツ..ト
                         (base >= 0x30CF && base <= 0x30DB && (base - 0x30CF) % 3 == 0);   // ハ..ホ
    return regular ? base + 1 : 0;
}

// The semi-voiced form (only the ハ row has one), or 0.
constexpr char32_t semiVoicedForm(char32_t base) noexcept {
    const bool hRow = base >= 0x30CF && base <= 0x30DB && (base - 0x30CF) % 3 == 0;
    return hRow ? base + 2 : 0;
}

// full-width code point -> its half-width spelling (one or two units).
const std::unordered_map<char32_t, std::u32string>& reverseKana() {
    static const std::unordered_map<char32_t, std::u32string> table = [] {
        std::unordered_map<char32_t, std::u32string> t;
        for (std::size_t i = 0; i < sizeof(kFullKana) / sizeof(kFullKana[0]); ++i) {
            const char32_t half = kHalfKanaFirst + static_cast<char32_t>(i);
            const char32_t full = kFullKana[i];
            t.emplace(full, std::u32string(1, half));
            if (const char32_t voiced = voicedForm(full); voiced != 0) {
                t.emplace(voiced, std::u32string{half, kDakuten});
            }
            if (const char32_t semi = semiVoicedForm(full); semi != 0) {
                t.emplace(semi, std::u32string{half, kHandakuten});
            }
        }
        return t;
    }();
    return table;
}

} // namespace

std::u32string toFullWidth(std::u32string_view s) {
    std::u32string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        const char32_t c = s[i];
        if (c == 0x20) {
            out.push_back(0x3000);
        } else if (c >= 0x21 && c <= 0x7E) {
            out.push_back(c + 0xFEE0);
        } else if (const char32_t symbol = fullSymbol(c); symbol != 0) {
            out.push_back(symbol);
        } else if (isHalfKana(c)) {
            char32_t full = kFullKana[c - kHalfKanaFirst];
            if (i + 1 < s.size()) {
                const char32_t mark = s[i + 1];
                const char32_t voiced = mark == kDakuten ? voicedForm(full) : 0;
                const char32_t semi = mark == kHandakuten ? semiVoicedForm(full) : 0;
                if (voiced != 0 || semi != 0) {
                    full = voiced != 0 ? voiced : semi;
                    ++i; // the mark became part of the letter
                }
            }
            out.push_back(full);
        } else {
            out.push_back(c);
        }
    }
    return out;
}

std::u32string toHalfWidth(std::u32string_view s) {
    std::u32string out;
    out.reserve(s.size());
    const auto& reverse = reverseKana();
    for (const char32_t c : s) {
        if (c == 0x3000) {
            out.push_back(0x20);
        } else if (c >= 0xFF01 && c <= 0xFF5E) {
            out.push_back(c - 0xFEE0);
        } else if (const char32_t symbol = halfSymbol(c); symbol != 0) {
            out.push_back(symbol);
        } else if (const auto it = reverse.find(c); it != reverse.end()) {
            out += it->second;
        } else {
            out.push_back(c);
        }
    }
    return out;
}

std::u32string toggleWidth(std::u32string_view s) {
    std::size_t half = 0;
    std::size_t full = 0;
    for (const char32_t c : s) {
        if (isHalfAscii(c) || isHalfKana(c) || fullSymbol(c) != 0) {
            ++half;
        } else if (isFullAscii(c) || isFullKana(c) || halfSymbol(c) != 0) {
            ++full;
        }
    }
    if (half == 0 && full == 0) return std::u32string(s);
    return half >= full ? toFullWidth(s) : toHalfWidth(s);
}

} // namespace lankey::core::text
