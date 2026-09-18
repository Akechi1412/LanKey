#include <gtest/gtest.h>

#include "core/text/VietnameseDistance.h"

namespace lankey::core::text {
namespace {

using D = VietnameseDistance;

TEST(VietnameseDistance, IdenticalIsZero) {
    EXPECT_EQ(D::scaled(U"chương", U"chương"), 0);
    EXPECT_EQ(D::scaled(U"", U""), 0);
}

TEST(VietnameseDistance, EmptyAgainstNonEmptyIsOnePerUnit) {
    EXPECT_DOUBLE_EQ(D::of(U"", U"ab"), 2.0);
    EXPECT_DOUBLE_EQ(D::of(U"ngh", U""), 1.0); // "ngh" is one unit
}

TEST(VietnameseDistance, ToneSlipOnSameVowelIsCheap) {
    EXPECT_DOUBLE_EQ(D::of(U"sữa", U"sửa"), 0.4);
    EXPECT_DOUBLE_EQ(D::of(U"a", U"ă"), 0.4);
    EXPECT_DOUBLE_EQ(D::of(U"ă", U"â"), 0.4);
}

TEST(VietnameseDistance, DifferentVowelIsFull) {
    EXPECT_DOUBLE_EQ(D::of(U"ba", U"bo"), 1.0);
}

TEST(VietnameseDistance, ConfusableConsonantsAreDialectCost) {
    EXPECT_DOUBLE_EQ(D::of(U"xa", U"sa"), 0.6);
    EXPECT_DOUBLE_EQ(D::of(U"cha", U"tra"), 0.6); // one digraph unit, not two edits
    EXPECT_DOUBLE_EQ(D::of(U"nam", U"lam"), 0.6);
    EXPECT_DOUBLE_EQ(D::of(U"da", U"ra"), 0.6);
    EXPECT_DOUBLE_EQ(D::of(U"giá", U"dá"), 0.6);
    EXPECT_DOUBLE_EQ(D::of(U"ca", U"ka"), 0.6);
    EXPECT_DOUBLE_EQ(D::of(U"nga", U"ngha"), 0.6);
    EXPECT_DOUBLE_EQ(D::of(U"ga", U"gha"), 0.6);
}

TEST(VietnameseDistance, UnrelatedConsonantIsFull) {
    EXPECT_DOUBLE_EQ(D::of(U"ba", U"ma"), 1.0);
    EXPECT_DOUBLE_EQ(D::of(U"da", U"đa"), 1.0); // đ is its own letter
}

TEST(VietnameseDistance, NoTranspositionShortcut) {
    // Two same-vowel slips, not one swap: a swap shortcut would break the triangle
    // inequality a metric index depends on.
    EXPECT_DOUBLE_EQ(D::of(U"chuơng", U"chưong"), 0.8);
    EXPECT_DOUBLE_EQ(D::of(U"gõ", U"õg"), 2.0);
}

TEST(VietnameseDistance, TriangleInequalityHoldsOnKnownOsaCounterexample) {
    // With optimal string alignment the swap made d(aự,ựa) smaller than the detour over
    // "au" allows - the case that lost "au" in the (former) BK-tree.
    const auto ab = D::scaled(U"aự", U"au");
    const auto bc = D::scaled(U"au", U"ựa");
    const auto ac = D::scaled(U"aự", U"ựa");
    EXPECT_LE(ac, ab + bc);
    EXPECT_LE(ab, ac + bc);
    EXPECT_LE(bc, ab + ac);
}

TEST(VietnameseDistance, InsertAndDeleteAreFull) {
    EXPECT_DOUBLE_EQ(D::of(U"chương", U"chưng"), 1.0);
    EXPECT_DOUBLE_EQ(D::of(U"chương", U"chươngg"), 1.0);
}

TEST(VietnameseDistance, GiBeforeNucleusIsNotADigraph) {
    // "gì" = g + ì: the i carries the tone, so it must stay a vowel unit.
    EXPECT_DOUBLE_EQ(D::of(U"gì", U"gi"), 0.4);
    // "giường" -> the onset is the gi digraph; only the second vowel differs.
    EXPECT_DOUBLE_EQ(D::of(U"giường", U"giừơng"), 0.8);
}

TEST(VietnameseDistance, TypicalTyposFallInsideRadiusTwo) {
    EXPECT_LE(D::of(U"chuơng", U"chương"), 1.0);
    EXPECT_LE(D::of(U"nguời", U"người"), 1.0);
    EXPECT_LE(D::of(U"việtt", U"việt"), 1.0);
}

TEST(VietnameseDistance, IsSymmetric) {
    const char32_t* pairs[][2] = {
        {U"sữa", U"sửa"}, {U"chuơng", U"chương"}, {U"xa", U"tra"}, {U"", U"ngh"}, {U"giá", U"gì"},
    };
    for (const auto& p : pairs) {
        EXPECT_EQ(D::scaled(p[0], p[1]), D::scaled(p[1], p[0]));
    }
}

} // namespace
} // namespace lankey::core::text
