// Conformance of WidthConverter against the Unicode character database.
//
// tests/data/width-pairs.tsv is generated from NFKC/NFKD by
// tests/data/make_width_pairs.py, so this checks the implementation against the standard
// rather than against a table written by the same hand. Every pair is checked in both
// directions, and every code point of the two blocks is checked to be either a pair or
// left alone.

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "core/text/Utf.h"
#include "core/text/WidthConverter.h"

namespace lankey::core::text {
namespace {

struct Pair {
    std::u32string half;
    std::u32string full;
    std::string note;
};

std::u32string parseHexes(const std::string& field) {
    std::u32string out;
    std::istringstream in(field);
    std::string token;
    while (in >> token) {
        out.push_back(static_cast<char32_t>(std::stoul(token, nullptr, 16)));
    }
    return out;
}

const std::vector<Pair>& pairs() {
    static const std::vector<Pair> table = [] {
        std::vector<Pair> rows;
        const std::string path = std::string(LANKEY_SOURCE_DIR) + "/tests/data/width-pairs.tsv";
        std::ifstream in(path);
        EXPECT_TRUE(in.is_open()) << "missing " << path
                                  << " - run: python tests/data/make_width_pairs.py";
        std::string line;
        while (std::getline(in, line)) {
            if (line.empty() || line[0] == '#') continue;
            const std::size_t firstTab = line.find('\t');
            const std::size_t secondTab = line.find('\t', firstTab + 1);
            if (firstTab == std::string::npos || secondTab == std::string::npos) continue;
            rows.push_back({parseHexes(line.substr(0, firstTab)),
                            parseHexes(line.substr(firstTab + 1, secondTab - firstTab - 1)),
                            line.substr(secondTab + 1)});
        }
        return rows;
    }();
    return table;
}

std::string show(std::u32string_view s) {
    std::string out;
    for (const char32_t c : s) {
        char buf[16] = {};
        std::snprintf(buf, sizeof(buf), "U+%04X ", static_cast<unsigned>(c));
        out += buf;
    }
    return out;
}

TEST(WidthConformance, TableIsLoaded) {
    ASSERT_GE(pairs().size(), 180u) << "width-pairs.tsv looks truncated";
}

TEST(WidthConformance, EveryPairConvertsToFullWidth) {
    for (const Pair& p : pairs()) {
        EXPECT_EQ(toFullWidth(p.half), p.full)
            << p.note << ": " << show(p.half) << "-> got " << show(toFullWidth(p.half)) << ", want "
            << show(p.full);
    }
}

TEST(WidthConformance, EveryPairConvertsToHalfWidth) {
    for (const Pair& p : pairs()) {
        EXPECT_EQ(toHalfWidth(p.full), p.half)
            << p.note << ": " << show(p.full) << "-> got " << show(toHalfWidth(p.full)) << ", want "
            << show(p.half);
    }
}

TEST(WidthConformance, PairsRoundTripBothWays) {
    for (const Pair& p : pairs()) {
        EXPECT_EQ(toHalfWidth(toFullWidth(p.half)), p.half) << p.note << " " << show(p.half);
        EXPECT_EQ(toFullWidth(toHalfWidth(p.full)), p.full) << p.note << " " << show(p.full);
    }
}

// Anything in the two blocks that is not in the table must be left untouched: silently
// mangling a character the table does not cover is worse than not converting it.
TEST(WidthConformance, CodePointsOutsideTheTableAreUntouched) {
    std::vector<bool> isHalfSource(0x10000, false);
    std::vector<bool> isFullSource(0x10000, false);
    for (const Pair& p : pairs()) {
        if (p.half.size() == 1) isHalfSource[p.half[0]] = true;
        if (p.full.size() == 1) isFullSource[p.full[0]] = true;
        // A two-unit half-width spelling: its first unit is already marked by its own row.
    }
    const auto check = [&](char32_t first, char32_t last) {
        for (char32_t c = first; c <= last; ++c) {
            const std::u32string one(1, c);
            if (!isHalfSource[c]) {
                EXPECT_EQ(toFullWidth(one), one)
                    << "toFullWidth changed " << show(one) << "to " << show(toFullWidth(one));
            }
            if (!isFullSource[c]) {
                EXPECT_EQ(toHalfWidth(one), one)
                    << "toHalfWidth changed " << show(one) << "to " << show(toHalfWidth(one));
            }
        }
    };
    check(0x0000, 0x00FF); // ASCII and Latin-1: only 0x20..0x7E convert
    check(0x3000, 0x30FF); // CJK punctuation and katakana
    check(0x4E00, 0x4E20); // kanji: never touched
    check(0xFF00, 0xFFEF); // half/full-width forms block, including ￠￡￢￣￤￥ and ￨..￮
    check(0x1E00, 0x1EFF); // Vietnamese letters: never touched
}

// The visible promise of the hotkey: pressing it twice gives the text back.
TEST(WidthConformance, ToggleIsInvolutiveOnEveryPair) {
    for (const Pair& p : pairs()) {
        EXPECT_EQ(toggleWidth(toggleWidth(p.half)), p.half) << p.note << " " << show(p.half);
        EXPECT_EQ(toggleWidth(toggleWidth(p.full)), p.full) << p.note << " " << show(p.full);
    }
}

TEST(WidthConformance, RealisticSentences) {
    struct Case {
        std::u32string half;
        std::u32string full;
    };
    const Case cases[] = {
        {U"Windows 11 (build 26200)", U"Ｗｉｎｄｏｗｓ　１１　（ｂｕｉｌｄ　２６２００）"},
        {U"ﾊﾟｿｺﾝ ﾃﾞｽｸﾄｯﾌﾟ", U"パソコン　デスクトップ"},
        {U"e-mail: a@b.co.jp", U"ｅ－ｍａｉｌ：　ａ＠ｂ．ｃｏ．ｊｐ"},
        {U"ｶﾞｷﾞｸﾞｹﾞｺﾞ ﾊﾟﾋﾟﾌﾟﾍﾟﾎﾟ", U"ガギグゲゴ　パピプペポ"},
        {U"ｱｲｳｴｵ 123 ABC", U"アイウエオ　１２３　ＡＢＣ"},
    };
    for (const Case& c : cases) {
        EXPECT_EQ(toFullWidth(c.half), c.full) << show(c.half);
        EXPECT_EQ(toHalfWidth(c.full), c.half) << show(c.full);
        EXPECT_EQ(toggleWidth(c.half), c.full) << show(c.half);
        EXPECT_EQ(toggleWidth(c.full), c.half) << show(c.full);
    }
}

// Mixed scripts: only the convertible characters move, the rest stay byte for byte.
TEST(WidthConformance, MixedTextKeepsEverythingElse) {
    const std::u32string mixed = U"Tiếng Việt 日本語 ひらがな ｶﾀｶﾅ ABC 123";
    const std::u32string full = toFullWidth(mixed);
    EXPECT_EQ(full, U"Ｔｉếｎｇ　Ｖｉệｔ　日本語　ひらがな　カタカナ　ＡＢＣ　１２３");
    EXPECT_EQ(toHalfWidth(full), U"Tiếng Việt 日本語 ひらがな ｶﾀｶﾅ ABC 123");
}

// A voiced mark that does not belong to the preceding letter must stay a separate mark,
// not silently merge into it.
TEST(WidthConformance, StrayVoicedMarksStaySeparate) {
    EXPECT_EQ(toFullWidth(U"ｱﾞ"), U"ア゛");  // ア has no voiced form
    EXPECT_EQ(toFullWidth(U"ﾞｱ"), U"゛ア");  // leading mark
    EXPECT_EQ(toFullWidth(U"ｶﾟ"), U"カ゜");  // カ has no semi-voiced form
    EXPECT_EQ(toFullWidth(U"ﾊﾞﾞ"), U"バ゛"); // one mark consumed, the second is stray
    EXPECT_EQ(toHalfWidth(U"ア゛"), U"ｱﾞ");
}

// Where LanKey deliberately differs from Windows' own 全角/半角 conversion
// (LCMapStringEx with LCMAP_FULLWIDTH/LCMAP_HALFWIDTH, locale ja-JP). The comparison is
// tests/data/compare-windows.ps1; these four cases are the entire difference, and they are
// pinned here so a future change to the table has to argue with them.
TEST(WidthConformance, DeliberateDeviationsFromWindows) {
    // 1. Backslash. Windows ja-JP leaves both forms alone - a Shift-JIS legacy, where 0x5C
    //    was the yen sign - which also makes ＼ impossible to convert back. LanKey follows
    //    Unicode: the whole ASCII range converts, and the round trip holds.
    EXPECT_EQ(toFullWidth(U"\\"), U"＼");
    EXPECT_EQ(toHalfWidth(U"＼"), U"\\");

    // 2. Half-width Hangul jamo (U+FFA0..U+FFDC). Windows maps them to the Hangul
    //    compatibility block; LanKey is a Vietnamese/Japanese tool and leaves Korean alone.
    for (char32_t c = 0xFFA0; c <= 0xFFDC; ++c) {
        const std::u32string one(1, c);
        EXPECT_EQ(toFullWidth(one), one) << show(one);
    }
    EXPECT_EQ(toHalfWidth(U"ᄀ각"), U"ᄀ각");

    // 3. ￦ (U+FFE6, full-width Korean won): same reason.
    EXPECT_EQ(toHalfWidth(U"￦"), U"￦");

    // 4. ヾ (U+30FE, katakana voiced iteration mark). Windows turns it into ヽ + a
    //    half-width mark - a mixed-width result. LanKey leaves it alone.
    EXPECT_EQ(toHalfWidth(U"ヽヾ"), U"ヽヾ");
}

// A whole paragraph is converted in one keystroke; make sure the cost is linear and small
// (the reverse table is a hash map built once, the symbol list is six entries).
TEST(WidthConformance, ConvertsALargeSelectionQuickly) {
    std::u32string text;
    while (text.size() < 100000)
        text += U"ｱｲｳ ABC 123 ｶﾞｷﾞ 日本語 xyz ";
    const auto started = std::chrono::steady_clock::now();
    const std::u32string full = toFullWidth(text);
    const std::u32string back = toHalfWidth(full);
    const auto micros = std::chrono::duration_cast<std::chrono::microseconds>(
                            std::chrono::steady_clock::now() - started)
                            .count();
    EXPECT_EQ(back, text);
    EXPECT_LT(micros, 100000) << "two passes over 100k characters took " << micros << " us";
    std::printf("[bench] width conversion %zu chars both ways: %lld us\n", text.size(),
                static_cast<long long>(micros));
}

} // namespace
} // namespace lankey::core::text
