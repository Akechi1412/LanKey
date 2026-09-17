#include <map>

#include <gtest/gtest.h>

#include "core/model/Thresholds.h"
#include "core/smart/learn/LearningRecorder.h"

#include "tests/fakes/FakeClock.h"

namespace lankey::core::smart {
namespace {

using model::LexiconDeltaBatch;
using model::Syllable;
using model::SyllableCommitted;
using model::Thresholds;
using tests::FakeClock;

struct RecorderTest : testing::Test {
    FakeClock clock;
    std::vector<LexiconDeltaBatch> flushed;
    LearningRecorder recorder{clock,
                              [this](LexiconDeltaBatch b) { flushed.push_back(std::move(b)); }};

    static SyllableCommitted event(std::initializer_list<const char32_t*> syllables) {
        SyllableCommitted c;
        for (const auto* s : syllables)
            c.window.commit(Syllable::fromComposed(s));
        return c;
    }

    std::map<std::u32string, std::int32_t> lastBatchAsMap() const {
        std::map<std::u32string, std::int32_t> m;
        for (const auto& d : flushed.back())
            m[d.phrase.joined()] = d.frequencyDelta;
        return m;
    }
};

TEST_F(RecorderTest, CountsAllPhrasesEndingInTheNewSyllable) {
    recorder.record(event({U"hệ", U"điều", U"hành"}));
    recorder.flush();
    ASSERT_EQ(flushed.size(), 1u);
    const auto m = lastBatchAsMap();
    EXPECT_EQ(m.size(), 3u);
    EXPECT_EQ(m.at(U"hệ điều hành"), 1);
    EXPECT_EQ(m.at(U"điều hành"), 1);
    EXPECT_EQ(m.at(U"hành"), 1);
}

TEST_F(RecorderTest, ThreeCommitsProduceSixDeltas) {
    // "hệ" -> [hệ]; "điều" -> [hệ điều, điều]; "hành" -> [hệ điều hành, điều hành, hành]
    model::PhraseWindow w;
    for (const auto* s : {U"hệ", U"điều", U"hành"}) {
        w.commit(Syllable::fromComposed(s));
        SyllableCommitted c;
        c.window = w;
        recorder.record(c);
    }
    EXPECT_EQ(recorder.pending(), 6u);
}

TEST_F(RecorderTest, RepeatedPhrasesAreMergedBeforeFlush) {
    recorder.record(event({U"chương", U"trình"}));
    recorder.record(event({U"chương", U"trình"}));
    recorder.flush();
    const auto m = lastBatchAsMap();
    EXPECT_EQ(m.at(U"chương trình"), 2);
    EXPECT_EQ(m.at(U"trình"), 2);
}

TEST_F(RecorderTest, SkipsPhrasesWithOutOfRangeSyllables) {
    recorder.record(event({U"a", U"chương"})); // "a" is shorter than kMinSyllableLength
    recorder.flush();
    const auto m = lastBatchAsMap();
    EXPECT_EQ(m.size(), 1u);
    EXPECT_TRUE(m.contains(U"chương"));
    EXPECT_FALSE(m.contains(U"a chương"));
}

TEST_F(RecorderTest, SelectionWeighsMore) {
    model::Phrase p;
    p.syllables = {Syllable::fromComposed(U"chương"), Syllable::fromComposed(U"trình")};
    recorder.recordSelection(p);
    recorder.flush();
    EXPECT_EQ(lastBatchAsMap().at(U"chương trình"), LearningRecorder::kSelectionWeight);
}

TEST_F(RecorderTest, FlushesByTimeAndBySize) {
    recorder.record(event({U"xin"}));
    recorder.flushIfDue();
    EXPECT_TRUE(flushed.empty());
    clock.advanceMs(Thresholds::kFlushIntervalMs);
    recorder.flushIfDue();
    EXPECT_EQ(flushed.size(), 1u);

    for (int i = 0; i < Thresholds::kFlushBatchSize; ++i) {
        const std::u32string s =
            U"xx" + std::u32string(1, static_cast<char32_t>(U'a' + static_cast<unsigned>(i % 26))) +
            std::u32string(1, static_cast<char32_t>(U'a' + static_cast<unsigned>(i / 26)));
        recorder.record(event({s.c_str()}));
    }
    recorder.flushIfDue();
    EXPECT_EQ(flushed.size(), 2u);
    EXPECT_EQ(recorder.pending(), 0u);
}

TEST_F(RecorderTest, TimestampsComeFromClock) {
    clock.setUnixSeconds(1234);
    recorder.record(event({U"xin"}));
    recorder.flush();
    EXPECT_EQ(flushed.back()[0].usedAt, 1234);
}

TEST_F(RecorderTest, EmptyFlushDoesNotCallHandler) {
    recorder.flush();
    EXPECT_TRUE(flushed.empty());
}

} // namespace
} // namespace lankey::core::smart
