#include <optional>

#include <gtest/gtest.h>

#include "core/smart/correct/ManualCorrectionDetector.h"

namespace lankey::core::smart {
namespace {

using model::Syllable;
using model::SyllableCommitted;

std::optional<ManualCorrectionDetector::Fix> detect(const SyllableCommitted& c) {
    return ManualCorrectionDetector::detect(c, BaseSyllableSet::builtin());
}

SyllableCommitted retyped(std::initializer_list<const char32_t*> window,
                          std::initializer_list<const char32_t*> from) {
    SyllableCommitted c;
    for (const auto* s : window)
        c.window.committed.push_back(Syllable::fromComposed(s));
    for (const auto* s : from)
        c.retypedFrom.push_back(Syllable::fromComposed(s));
    return c;
}

TEST(ManualCorrectionDetector, NothingWithoutARetype) {
    EXPECT_FALSE(detect(retyped({U"sửa", U"lỗi"}, {})).has_value());
}

TEST(ManualCorrectionDetector, OneSyllableFix) {
    const auto fix = detect(retyped({U"tôi", U"chương"}, {U"chuơng"}));
    ASSERT_TRUE(fix.has_value());
    EXPECT_EQ(fix->wrong, U"chuơng");
    EXPECT_EQ(fix->correct, U"chương");
}

TEST(ManualCorrectionDetector, PhraseFixKeepsTheWholeRetypedSpan) {
    const auto fix = detect(retyped({U"tôi", U"sửa", U"lỗi"}, {U"sữa", U"lỗi"}));
    ASSERT_TRUE(fix.has_value());
    EXPECT_EQ(fix->wrong, U"sữa lỗi");
    EXPECT_EQ(fix->correct, U"sửa lỗi");
}

TEST(ManualCorrectionDetector, ChangingTheWordIsNotAFix) {
    EXPECT_FALSE(detect(retyped({U"viết", U"bài"}, {U"sửa", U"lỗi"})).has_value());
    EXPECT_FALSE(detect(retyped({U"nhà"}, {U"xe"})).has_value());
}

TEST(ManualCorrectionDetector, IdenticalRetypeIsIgnored) {
    EXPECT_FALSE(detect(retyped({U"sửa"}, {U"sửa"})).has_value());
}

TEST(ManualCorrectionDetector, TypingOnAfterDeletingTheSpaceIsNotAFix) {
    // "sa " -> Backspace -> "u " commits "sau" as a retype of "sa".
    EXPECT_FALSE(detect(retyped({U"sau"}, {U"sa"})).has_value());
    EXPECT_FALSE(detect(retyped({U"tôi"}, {U"tô"})).has_value());
    EXPECT_FALSE(detect(retyped({U"tô"}, {U"tôi"})).has_value()); // trimmed
}

TEST(ManualCorrectionDetector, TwoValidSingleWordsIsAChangeOfMindNotAFix) {
    // Both are words; a rule "có -> cơ" would rewrite the commonest word there is.
    EXPECT_FALSE(detect(retyped({U"cơ"}, {U"có"})).has_value());
    // A phrase may be valid on both sides: the context is the fix.
    EXPECT_TRUE(detect(retyped({U"sửa", U"lỗi"}, {U"sữa", U"lỗi"})).has_value());
}

TEST(ManualCorrectionDetector, TargetMustBeARealWord) {
    EXPECT_FALSE(detect(retyped({U"quaả"}, {U"qua"})).has_value());
    EXPECT_FALSE(detect(retyped({U"khaắc"}, {U"khaswc"})).has_value());
    // English targets are fine (ASCII only).
    EXPECT_TRUE(detect(retyped({U"with"}, {U"wiht"})).has_value());
}

TEST(ManualCorrectionDetector, UnlearnableSyllablesAreIgnored) {
    EXPECT_FALSE(detect(retyped({U"a"}, {U"b"})).has_value());
    EXPECT_FALSE(detect(retyped({U"ab1"}, {U"ab2"})).has_value());
}

} // namespace
} // namespace lankey::core::smart
