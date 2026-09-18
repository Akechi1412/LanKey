#include <algorithm>
#include <random>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "core/smart/correct/BaseSyllableSet.h"
#include "core/smart/correct/FuzzyIndex.h"
#include "core/text/VietnameseDistance.h"

namespace lankey::core::smart {
namespace {

using text::VietnameseDistance;

std::vector<std::u32string> keys(const std::vector<FuzzyIndex::Match>& matches) {
    std::vector<std::u32string> out;
    for (const auto& m : matches)
        out.emplace_back(m.key);
    return out;
}

TEST(FuzzyIndex, EmptyTreeFindsNothing) {
    FuzzyIndex t;
    std::vector<FuzzyIndex::Match> out;
    t.search(U"a", 10, out);
    EXPECT_TRUE(out.empty());
}

TEST(FuzzyIndex, ExactMatchHasDistanceZero) {
    FuzzyIndex t;
    t.insert(U"chương");
    t.insert(U"trình");
    std::vector<FuzzyIndex::Match> out;
    t.search(U"chương", 0, out);
    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(out[0].key, U"chương");
    EXPECT_EQ(out[0].scaledDistance, 0);
}

TEST(FuzzyIndex, DuplicatesAreIgnored) {
    FuzzyIndex t;
    t.insert(U"a");
    t.insert(U"a");
    EXPECT_EQ(t.size(), 1u);
}

TEST(FuzzyIndex, FindsNeighboursWithinRadiusNearestFirst) {
    FuzzyIndex t;
    for (const auto* s : {U"sửa", U"sữa", U"sứa", U"xưa", U"mua", U"chương", U"sa"})
        t.insert(s);
    std::vector<FuzzyIndex::Match> out;
    t.search(U"sữa", 5, out); // radius 1.0
    const auto k = keys(out);
    ASSERT_GE(k.size(), 3u);
    EXPECT_EQ(k[0], U"sữa");
    // 0.4 tone slips come before the 1.0 edits.
    EXPECT_NE(std::find(k.begin(), k.end(), U"sửa"), k.end());
    EXPECT_NE(std::find(k.begin(), k.end(), U"sứa"), k.end());
    EXPECT_EQ(std::find(k.begin(), k.end(), U"chương"), k.end());
    EXPECT_TRUE(std::is_sorted(out.begin(), out.end(), [](const auto& a, const auto& b) {
        return a.scaledDistance < b.scaledDistance;
    }));
}

// The index must agree with a brute-force scan at every radius it is used with: this is
// what makes the skeleton/one-deletion trick trustworthy.
TEST(FuzzyIndex, MatchesBruteForceOnTheRealDictionary) {
    const auto& dict = BaseSyllableSet::builtin();
    FuzzyIndex t;
    std::vector<std::u32string> all;
    dict.forEach([&](std::u32string_view s) {
        t.insert(s);
        all.emplace_back(s);
    });
    ASSERT_EQ(t.size(), dict.size());

    // Fixed cases plus a pseudo-random sample of one-letter mutations of dictionary
    // words: a metric violation shows up as a missing neighbour somewhere in here.
    std::vector<std::u32string> queries = {U"chuơng", U"nguời", U"sữa", U"tiếg",
                                           U"việtt",  U"xyz",   U"aự"};
    std::mt19937 rng(7);
    std::uniform_int_distribution<std::size_t> pick(0, all.size() - 1);
    const char32_t letters[] = {U'a', U'ư', U'ơ', U'ệ', U'n', U'g', U'h', U'c', U'ố', U'ừ'};
    for (int i = 0; i < 600; ++i) {
        std::u32string q = all[pick(rng)];
        const auto pos = rng() % q.size();
        switch (rng() % 3) {
        case 0:
            q[pos] = letters[rng() % std::size(letters)];
            break;
        case 1:
            q.insert(pos, 1, letters[rng() % std::size(letters)]);
            break;
        default:
            if (q.size() > 1) q.erase(pos, 1);
            break;
        }
        queries.push_back(q);
    }
    for (const auto& q : queries) {
        for (const int radius : {3, 5, 7}) {
            std::vector<FuzzyIndex::Match> out;
            t.search(q, radius, out);
            std::vector<std::u32string> expected;
            for (const auto& s : all) {
                if (VietnameseDistance::scaled(q, s) <= radius) expected.push_back(s);
            }
            auto got = keys(out);
            std::sort(got.begin(), got.end());
            std::sort(expected.begin(), expected.end());
            EXPECT_EQ(got, expected)
                << "query " << std::string(q.begin(), q.end()) << " radius " << radius;
        }
    }
}

TEST(FuzzyIndex, RealTyposFindTheirCorrection) {
    const auto& dict = BaseSyllableSet::builtin();
    FuzzyIndex t;
    dict.forEach([&](std::u32string_view s) { t.insert(s); });
    std::vector<FuzzyIndex::Match> out;

    t.search(U"chuơng", 5, out);
    EXPECT_NE(std::find(keys(out).begin(), keys(out).end(), U"chương"), keys(out).end());

    t.search(U"nguời", 5, out);
    EXPECT_NE(std::find(keys(out).begin(), keys(out).end(), U"người"), keys(out).end());
}

} // namespace
} // namespace lankey::core::smart
