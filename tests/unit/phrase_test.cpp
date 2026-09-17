#include <gtest/gtest.h>

#include "core/model/Phrase.h"

namespace lankey::core::model {
namespace {

TEST(Syllable, FromComposedFoldsCase) {
    const auto s = Syllable::fromComposed(U"Chương");
    EXPECT_EQ(s.text, U"chương");
}

TEST(Syllable, FromComposedNormalisesToNfc) {
    // "a" + combining acute must become the precomposed letter.
    const auto s = Syllable::fromComposed(std::u32string{U'a', 0x0301});
    EXPECT_EQ(s.text, std::u32string{0x00E1});
}

TEST(Phrase, JoinedUsesSingleSpaces) {
    Phrase p;
    p.syllables = {Syllable::fromComposed(U"hệ"), Syllable::fromComposed(U"điều"),
                   Syllable::fromComposed(U"hành")};
    EXPECT_EQ(p.joined(), U"hệ điều hành");
    EXPECT_EQ(p.syllableCount(), 3);
    EXPECT_EQ(Phrase{}.joined(), U"");
}

TEST(PhraseWindow, KeepsAtMostMaxPhraseSyllablesCommitted) {
    PhraseWindow w;
    for (const auto* t : {U"a", U"b", U"c", U"d", U"e", U"f", U"g"})
        w.commit(Syllable::fromComposed(t));
    ASSERT_EQ(w.committedCount(), static_cast<std::size_t>(Thresholds::kMaxPhraseSyllables));
    EXPECT_EQ(w.committed.front().text, U"c");
    EXPECT_EQ(w.committed.back().text, U"g");
}

TEST(PhraseWindow, CommitClearsCurrent) {
    PhraseWindow w;
    w.current = U"chươ";
    w.commit(Syllable::fromComposed(U"chương"));
    EXPECT_TRUE(w.current.empty());
    EXPECT_FALSE(w.empty());
    w.reset();
    EXPECT_TRUE(w.empty());
}

TEST(PhraseWindow, CandidatePhrasesLongestFirst) {
    PhraseWindow w;
    w.commit(Syllable::fromComposed(U"hệ"));
    w.commit(Syllable::fromComposed(U"điều"));
    w.commit(Syllable::fromComposed(U"hành"));
    const auto c = w.candidatePhrases();
    ASSERT_EQ(c.size(), 3u);
    EXPECT_EQ(c[0].joined(), U"hệ điều hành");
    EXPECT_EQ(c[1].joined(), U"điều hành");
    EXPECT_EQ(c[2].joined(), U"hành");
    EXPECT_TRUE(PhraseWindow{}.candidatePhrases().empty());
}

} // namespace
} // namespace lankey::core::model
