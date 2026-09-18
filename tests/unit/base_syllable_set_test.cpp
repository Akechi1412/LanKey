#include <gtest/gtest.h>

#include "core/smart/correct/BaseSyllableSet.h"

namespace lankey::core::smart {
namespace {

TEST(BaseSyllableSet, BuiltinIsLoadedAndSized) {
    const auto& set = BaseSyllableSet::builtin();
    EXPECT_GT(set.size(), 6000u);
    EXPECT_LT(set.size(), 10000u);
}

TEST(BaseSyllableSet, ContainsCommonSyllablesLowercaseNfc) {
    const auto& set = BaseSyllableSet::builtin();
    EXPECT_TRUE(set.contains(U"chương"));
    EXPECT_TRUE(set.contains(U"trình"));
    EXPECT_TRUE(set.contains(U"người"));
    EXPECT_TRUE(set.contains(U"việt"));
    EXPECT_TRUE(set.contains(U"a"));
}

TEST(BaseSyllableSet, RejectsTyposEnglishAndUppercase) {
    const auto& set = BaseSyllableSet::builtin();
    EXPECT_FALSE(set.contains(U"chuơng"));
    EXPECT_FALSE(set.contains(U"xyz"));
    EXPECT_FALSE(set.contains(U"with"));
    EXPECT_FALSE(set.contains(U"Chương")); // keys are case-folded by the caller
    EXPECT_FALSE(set.contains(U""));
}

TEST(BaseSyllableSet, CustomListForTests) {
    const char32_t* const words[] = {U"ab", U"cd"};
    const BaseSyllableSet set{std::span<const char32_t* const>(words)};
    EXPECT_EQ(set.size(), 2u);
    EXPECT_TRUE(set.contains(U"ab"));
    EXPECT_FALSE(set.contains(U"ef"));
    int n = 0;
    set.forEach([&](std::u32string_view) { ++n; });
    EXPECT_EQ(n, 2);
}

} // namespace
} // namespace lankey::core::smart
