#include <gtest/gtest.h>

#include "core/text/WidthConverter.h"

namespace lankey::core::text {
namespace {

TEST(WidthConverter, AsciiToFull) {
    EXPECT_EQ(toFullWidth(U"Abc 123!?"), U"Ａｂｃ　１２３！？");
}

TEST(WidthConverter, FullToHalf) {
    EXPECT_EQ(toHalfWidth(U"Ａｂｃ　１２３！？"), U"Abc 123!?");
}

TEST(WidthConverter, KatakanaBothWays) {
    EXPECT_EQ(toFullWidth(U"ｶﾀｶﾅ ｶﾞｷﾞ ﾊﾟ ｳﾞ ｰ｡｢｣､･"), U"カタカナ　ガギ　パ　ヴ　ー。「」、・");
    EXPECT_EQ(toHalfWidth(U"カタカナ　ガギ　パ　ヴ　ー。「」、・"), U"ｶﾀｶﾅ ｶﾞｷﾞ ﾊﾟ ｳﾞ ｰ｡｢｣､･");
}

TEST(WidthConverter, SmallKanaAndLoneMarks) {
    EXPECT_EQ(toFullWidth(U"ｧｨｩｪｫｬｭｮｯ ﾞ ﾟ"), U"ァィゥェォャュョッ　゛　゜");
    EXPECT_EQ(toHalfWidth(U"ァィゥェォャュョッ　゛　゜"), U"ｧｨｩｪｫｬｭｮｯ ﾞ ﾟ");
}

TEST(WidthConverter, LeavesOtherScriptsAlone) {
    EXPECT_EQ(toFullWidth(U"tiếng Việt 日本語 ひらがな"), U"ｔｉếｎｇ　Ｖｉệｔ　日本語　ひらがな");
    EXPECT_EQ(toHalfWidth(U"日本語 ひらがな"), U"日本語 ひらがな");
}

TEST(WidthConverter, EveryAsciiPrintableRoundTrips) {
    for (char32_t c = 0x21; c <= 0x7E; ++c) {
        const std::u32string one(1, c);
        const auto full = toFullWidth(one);
        ASSERT_EQ(full.size(), 1u);
        EXPECT_EQ(full[0], c + 0xFEE0);
        EXPECT_EQ(toHalfWidth(full), one);
    }
}

TEST(WidthConverter, EveryHalfWidthKanaRoundTrips) {
    for (char32_t c = 0xFF61; c <= 0xFF9F; ++c) {
        const std::u32string one(1, c);
        EXPECT_EQ(toHalfWidth(toFullWidth(one)), one) << std::hex << static_cast<unsigned>(c);
    }
}

TEST(WidthConverter, ToggleGoesToMinorityClassAndIsInvolutive) {
    EXPECT_EQ(toggleWidth(U"abc ｱ"), U"ａｂｃ　ア");
    EXPECT_EQ(toggleWidth(U"ａｂｃ x"), U"abc x");
    EXPECT_EQ(toggleWidth(toggleWidth(U"hello ｶﾞ")), U"hello ｶﾞ");
    EXPECT_EQ(toggleWidth(U"日本語"), U"日本語");
    EXPECT_EQ(toggleWidth(U""), U"");
}

} // namespace
} // namespace lankey::core::text
