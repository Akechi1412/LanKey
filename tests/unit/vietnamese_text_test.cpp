#include <gtest/gtest.h>

#include "core/text/VietnameseText.h"

#include "tests/support/Typist.h"

namespace lankey::core::text {
namespace {

TEST(Nfc, ComposesToneOnPlainVowel) {
    // "a" + combining acute -> "á"
    EXPECT_EQ(nfc(U"a\u0301"), U"\u00E1");
    EXPECT_EQ(nfc(U"tie\u0302\u0301ng"), U"ti\u1EBFng");
}

TEST(Nfc, ComposesModifierThenTone) {
    // "a" + circumflex + acute -> "ấ" regardless of mark order
    EXPECT_EQ(nfc(U"a\u0302\u0301"), U"\u1EA5");
    EXPECT_EQ(nfc(U"a\u0301\u0302"), U"\u1EA5");
    // "o" + horn + tilde -> "ỡ"
    EXPECT_EQ(nfc(U"o\u031B\u0303"), U"\u1EE1");
    // đ is not produced by composition; it must survive as-is.
    EXPECT_EQ(nfc(U"\u0111\u01B0\u1EDDng"), U"\u0111\u01B0\u1EDDng");
}

TEST(Nfc, KeepsUppercase) {
    EXPECT_EQ(nfc(U"VIE\u0302\u0323T"), U"VI\u1EC6T");
}

TEST(Nfc, LeavesAlreadyComposedAndForeignTextAlone) {
    EXPECT_EQ(nfc(U"ch\u01B0\u01A1ng tr\u00ECnh"), U"ch\u01B0\u01A1ng tr\u00ECnh");
    EXPECT_EQ(nfc(U"hello, world 123"), U"hello, world 123");
    // A mark that cannot combine with the previous character is kept.
    EXPECT_EQ(nfc(U"1\u0301"), U"1\u0301");
    EXPECT_EQ(nfc(U"\u0301"), U"\u0301");
}

TEST(CaseFold, LowersAsciiAndVietnamese) {
    EXPECT_EQ(caseFold(U"Chương Trình"), U"chương trình");
    EXPECT_EQ(caseFold(U"VIỆT NAM"), U"việt nam");
    EXPECT_EQ(caseFold(U"Đà Nẵng"), U"đà nẵng");
    EXPECT_EQ(caseFold(U"abc 123 ."), U"abc 123 .");
}

TEST(StripDiacritics, RemovesTonesAndModifiers) {
    EXPECT_EQ(stripDiacritics(U"chương trình"), U"chuong trinh");
    EXPECT_EQ(stripDiacritics(U"Đường"), U"Duong");
    EXPECT_EQ(stripDiacritics(U"hệ điều hành"), U"he dieu hanh");
    EXPECT_EQ(stripDiacritics(U"plain"), U"plain");
}

TEST(IsLetter, AcceptsAlphabetRejectsOthers) {
    EXPECT_TRUE(isAllLetters(U"chương"));
    EXPECT_TRUE(isAllLetters(U"Đắk"));
    EXPECT_TRUE(isAllLetters(U"text")); // f/j/w/z accepted (see header)
    EXPECT_FALSE(isAllLetters(U"chương1"));
    EXPECT_FALSE(isAllLetters(U"a b"));
    EXPECT_FALSE(isAllLetters(U""));
    EXPECT_FALSE(isLetter(U'@'));
}

TEST(HasDiacritic, DetectsMarks) {
    EXPECT_TRUE(hasDiacritic(U'ư'));
    EXPECT_TRUE(hasDiacritic(U'ấ'));
    EXPECT_TRUE(hasDiacritic(U'đ'));
    EXPECT_TRUE(hasDiacritic(U'Đ'));
    EXPECT_FALSE(hasDiacritic(U'a'));
    EXPECT_FALSE(hasDiacritic(U'x'));
    EXPECT_FALSE(hasDiacritic(U'1'));
}

} // namespace
} // namespace lankey::core::text
