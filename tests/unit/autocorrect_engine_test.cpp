#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "core/model/Thresholds.h"
#include "core/smart/correct/AutoCorrectEngine.h"

namespace lankey::core::smart {
namespace {

using model::AutoCorrectLevel;
using model::AutoCorrectSettings;
using model::CorrectionRule;
using model::CorrectionRuleSource;
using model::CorrectionSource;
using model::LexiconEntry;
using model::Syllable;
using model::SyllableCommitted;
using model::Thresholds;

SyllableCommitted committed(std::initializer_list<const char32_t*> window,
                            char32_t terminator = U' ', bool transformed = true) {
    SyllableCommitted c;
    for (const auto* s : window)
        c.window.committed.push_back(Syllable::fromComposed(s));
    c.terminator = terminator;
    c.vietnameseTransformApplied = transformed;
    c.generation = 42;
    return c;
}

LexiconEntry entry(const char32_t* syllable, std::uint32_t freq) {
    LexiconEntry e;
    e.phrase.syllables.push_back(Syllable::fromComposed(syllable));
    e.frequency = freq;
    return e;
}

CorrectionRule rule(const char32_t* wrong, const char32_t* correct, double confidence) {
    CorrectionRule r;
    r.wrong = wrong;
    r.correct = correct;
    r.confidence = confidence;
    return r;
}

constexpr std::int64_t kNow = 1'700'000'000;

class AutoCorrectEngineTest : public testing::Test {
protected:
    AutoCorrectEngineTest() {
        engine.publishBase(base());
        publish({}, {}, {});
        AutoCorrectSettings s;
        s.level = AutoCorrectLevel::Cautious;
        engine.setSettings(s);
    }

    // The base index is expensive to build (~7k inserts); share it across the suite.
    static std::shared_ptr<const BaseIndex> base() {
        static const auto index = BaseIndex::build(BaseSyllableSet::builtin());
        return index;
    }

    void publish(const std::vector<LexiconEntry>& entries, const std::vector<CorrectionRule>& rules,
                 const std::vector<std::u32string>& blacklist) {
        engine.publish(
            CorrectionSnapshot::build(entries, rules, blacklist, BaseSyllableSet::builtin(), kNow));
    }

    void setLevel(AutoCorrectLevel level) {
        AutoCorrectSettings s;
        s.level = level;
        engine.setSettings(s);
    }

    AutoCorrectEngine engine;
};

TEST_F(AutoCorrectEngineTest, OffNeverCorrects) {
    setLevel(AutoCorrectLevel::Off);
    EXPECT_FALSE(engine.check(committed({U"nguời"})).has_value());
}

TEST_F(AutoCorrectEngineTest, ValidSyllableIsLeftAlone) {
    EXPECT_FALSE(engine.check(committed({U"người"})).has_value());
    EXPECT_FALSE(engine.check(committed({U"chương", U"trình"})).has_value());
}

TEST_F(AutoCorrectEngineTest, UniqueNearestNeighbourIsCorrected) {
    // A doubled letter: one deletion away from exactly one word.
    setLevel(AutoCorrectLevel::Balanced);
    const auto c = engine.check(committed({U"việtt"}));
    ASSERT_TRUE(c.has_value());
    EXPECT_EQ(c->syllableCount, 1);
    ASSERT_EQ(c->corrected.size(), 1u);
    EXPECT_EQ(c->corrected[0].text, U"việt");
    EXPECT_EQ(c->wrongKey, U"việtt");
    EXPECT_EQ(c->expectedGeneration, 42u);
    EXPECT_EQ(c->source, CorrectionSource::BaseDictionary);
    EXPECT_DOUBLE_EQ(c->confidence, 0.5); // 1 - 1.0/2.0
}

TEST_F(AutoCorrectEngineTest, EnglishWithoutVietnameseTransformIsNotTouched) {
    // "with" is not in the closed dictionary, but no tone/modifier was typed.
    EXPECT_FALSE(engine.check(committed({U"with"}, U' ', /*transformed=*/false)).has_value());
    EXPECT_FALSE(engine.check(committed({U"git"}, U' ', /*transformed=*/false)).has_value());
}

TEST_F(AutoCorrectEngineTest, NeverAfterEnterTabOrNavigation) {
    setLevel(AutoCorrectLevel::Balanced);
    EXPECT_FALSE(engine.check(committed({U"việtt"}, U'\n')).has_value());
    EXPECT_FALSE(engine.check(committed({U"việtt"}, U'\t')).has_value());
    EXPECT_FALSE(engine.check(committed({U"việtt"}, 0)).has_value());
    EXPECT_TRUE(engine.check(committed({U"việtt"}, U',')).has_value());
}

TEST_F(AutoCorrectEngineTest, PasswordFieldDigitsAndAllCapsAreSkipped) {
    setLevel(AutoCorrectLevel::Balanced);
    auto c = committed({U"việtt"});
    c.focus.isPasswordField = true;
    EXPECT_FALSE(engine.check(c).has_value());

    EXPECT_FALSE(engine.check(committed({U"việt1"})).has_value());

    auto caps = committed({U"việtt"});
    caps.window.committed[0].typed = U"VIỆTT";
    EXPECT_FALSE(engine.check(caps).has_value());
}

TEST_F(AutoCorrectEngineTest, AmbiguousCandidatesAreNotCorrectedUnlessTheUserTypesOne) {
    // "chuơng" is one modifier slip from both "chương" and "chuông".
    EXPECT_FALSE(engine.check(committed({U"chuơng"})).has_value());

    publish({entry(U"chương", 12), entry(U"chuông", 2)}, {}, {});
    const auto c = engine.check(committed({U"chuơng"}));
    ASSERT_TRUE(c.has_value());
    EXPECT_EQ(c->corrected[0].text, U"chương");
    EXPECT_EQ(c->source, CorrectionSource::BaseDictionary);

    // Equal frequency: still ambiguous.
    publish({entry(U"chương", 3), entry(U"chuông", 3)}, {}, {});
    EXPECT_FALSE(engine.check(committed({U"chuơng"})).has_value());
}

TEST_F(AutoCorrectEngineTest, UniqueModifierSlipIsFixedAtCautious) {
    // Measured on the dictionary: 72% of one-vowel tone/modifier slips have exactly one
    // nearest word. "đuờng" (missing the horn on u) is one of them.
    const auto c = engine.check(committed({U"đuờng"}));
    ASSERT_TRUE(c.has_value());
    EXPECT_EQ(c->corrected[0].text, U"đường");
    EXPECT_EQ(c->source, CorrectionSource::BaseDictionary);
}

TEST_F(AutoCorrectEngineTest, FrequentWordBeatsACloserRareOne) {
    // "nguoif" without the horns gives "nguòi": nguồi/nguội are 0.4 away, "người" 0.8.
    // A user who writes "người" is served "người" - at Balanced, whose reach is 1.0.
    setLevel(AutoCorrectLevel::Balanced);
    EXPECT_FALSE(engine.check(committed({U"nguòi"})).has_value()); // no history: tie
    publish({entry(U"người", 3)}, {}, {});
    const auto c = engine.check(committed({U"nguòi"}));
    ASSERT_TRUE(c.has_value());
    EXPECT_EQ(c->corrected[0].text, U"người");
    // Cautious never reaches 0.8, whatever the history.
    setLevel(AutoCorrectLevel::Cautious);
    EXPECT_FALSE(engine.check(committed({U"nguòi"})).has_value());
}

TEST_F(AutoCorrectEngineTest, ATonedTypoPrefersTonedCandidates) {
    // "dduowngr": wrong tone key. "đường" and "đương" are both 0.4 away; the user pressed a
    // tone key, so the toneless "đương" is not what they meant.
    const auto c = engine.check(committed({U"đưởng"}));
    ASSERT_TRUE(c.has_value());
    EXPECT_EQ(c->corrected[0].text, U"đường");
}

TEST_F(AutoCorrectEngineTest, ToneSlipsResolveThroughWhatTheUserWrites) {
    // Every tone of a rhyme is usually a word: "nguời" sits 0.4 from nguồi, nguội and
    // người. Only this user's own history can tell which one they meant.
    EXPECT_FALSE(engine.check(committed({U"nguời"})).has_value());
    publish({entry(U"người", 40), entry(U"nguội", 1)}, {}, {});
    const auto c = engine.check(committed({U"nguời"}));
    ASSERT_TRUE(c.has_value());
    EXPECT_EQ(c->corrected[0].text, U"người");
}

TEST_F(AutoCorrectEngineTest, LevelControlsHowFarAFixMayReach) {
    // One full edit (1.0): a doubled letter. Out of reach for Cautious (0.6).
    EXPECT_FALSE(engine.check(committed({U"việtt"})).has_value());
    setLevel(AutoCorrectLevel::Balanced);
    const auto c = engine.check(committed({U"việtt"}));
    ASSERT_TRUE(c.has_value());
    EXPECT_EQ(c->corrected[0].text, U"việt");
    // "tiếg" is one edit from tiếng, tiếc, tiếp, tiết...: ambiguous at any level.
    EXPECT_FALSE(engine.check(committed({U"tiếg"})).has_value());
}

TEST_F(AutoCorrectEngineTest, TrustedPersonalSyllableIsNotCorrected) {
    setLevel(AutoCorrectLevel::Balanced);
    publish({entry(U"việtt", Thresholds::kTrustMinFrequency)}, {}, {});
    EXPECT_FALSE(engine.check(committed({U"việtt"})).has_value());
    // Below the trust threshold it is still a typo.
    publish({entry(U"việtt", Thresholds::kTrustMinFrequency - 1)}, {}, {});
    EXPECT_TRUE(engine.check(committed({U"việtt"})).has_value());
}

TEST_F(AutoCorrectEngineTest, ARepeatedTypoIsNotTrustedNextToTheWordItStandsFor) {
    // Dogfood 2026-09-18: "đưởng" typed 6 times while testing, "đường" 11 times. Trust by
    // count alone would have silenced the correction for good.
    publish({entry(U"đưởng", 6), entry(U"đường", 11)}, {}, {});
    const auto c = engine.check(committed({U"đưởng"}));
    ASSERT_TRUE(c.has_value());
    EXPECT_EQ(c->corrected[0].text, U"đường");
    // The other way round it is a word of their own ("đường" rarely, "đưởng" a lot).
    publish({entry(U"đưởng", 12), entry(U"đường", 3)}, {}, {});
    EXPECT_FALSE(engine.check(committed({U"đưởng"})).has_value());
}

TEST_F(AutoCorrectEngineTest, PersonalSyllablesAreFuzzyCandidatesToo) {
    // A name the user types a lot; a slip on it is fixed towards the name.
    publish({entry(U"lankey", 20)}, {}, {});
    setLevel(AutoCorrectLevel::Balanced);
    const auto c = engine.check(committed({U"lankeyy"}));
    ASSERT_TRUE(c.has_value());
    EXPECT_EQ(c->corrected[0].text, U"lankey");
}

TEST_F(AutoCorrectEngineTest, CorrectionMapBeatsTheDictionaryAndWorksOnPhrases) {
    // Every syllable of "sữa lỗi" is valid; only the user's own rule knows better.
    publish({}, {rule(U"sữa lỗi", U"sửa lỗi", 0.8)}, {});
    const auto c = engine.check(committed({U"tôi", U"sữa", U"lỗi"}));
    ASSERT_TRUE(c.has_value());
    EXPECT_EQ(c->syllableCount, 2);
    ASSERT_EQ(c->corrected.size(), 2u);
    EXPECT_EQ(c->corrected[0].text, U"sửa");
    EXPECT_EQ(c->corrected[1].text, U"lỗi");
    EXPECT_EQ(c->wrongKey, U"sữa lỗi");
    EXPECT_EQ(c->source, CorrectionSource::CorrectionMap);
    EXPECT_DOUBLE_EQ(c->confidence, 0.8);
    // Not in that context: nothing.
    EXPECT_FALSE(engine.check(committed({U"sữa", U"tươi"})).has_value());
}

TEST_F(AutoCorrectEngineTest, CorrectionMapNeedsEnoughConfidence) {
    publish({}, {rule(U"sữa lỗi", U"sửa lỗi", Thresholds::kCorrectionApplyConfidence - 0.1)}, {});
    EXPECT_FALSE(engine.check(committed({U"sữa", U"lỗi"})).has_value());
}

TEST_F(AutoCorrectEngineTest, CorrectionMapAppliesEvenWithoutVietnameseTransform) {
    publish({}, {rule(U"teh", U"the", 0.9)}, {});
    const auto c = engine.check(committed({U"teh"}, U' ', /*transformed=*/false));
    ASSERT_TRUE(c.has_value());
    EXPECT_EQ(c->corrected[0].text, U"the");
}

TEST_F(AutoCorrectEngineTest, LongestContextRuleWins) {
    publish({}, {rule(U"lỗi", U"lối", 0.9), rule(U"sửa lỗi", U"sửa lỗi", 0.9)}, {});
    // The 2-syllable rule maps the phrase onto itself: no change, and it shadows the
    // single-syllable rule. (A same-text rule is still "a correction" to the engine; the
    // pipeline drops no-op replacements.)
    const auto c = engine.check(committed({U"sửa", U"lỗi"}));
    ASSERT_TRUE(c.has_value());
    EXPECT_EQ(c->wrongKey, U"sửa lỗi");
}

TEST_F(AutoCorrectEngineTest, BlacklistStopsEverything) {
    publish({}, {rule(U"sữa lỗi", U"sửa lỗi", 0.9)}, {U"sữa lỗi"});
    EXPECT_FALSE(engine.check(committed({U"sữa", U"lỗi"})).has_value());

    setLevel(AutoCorrectLevel::Balanced);
    publish({}, {}, {U"việtt"});
    EXPECT_FALSE(engine.check(committed({U"việtt"})).has_value());
}

TEST_F(AutoCorrectEngineTest, RejectedGuessIsNotRepeatedDuringTheCooldown) {
    setLevel(AutoCorrectLevel::Balanced);
    ASSERT_TRUE(engine.check(committed({U"việtt"})).has_value());
    // The user undid it once: a zero-confidence rule with one rejection, just now.
    CorrectionRule rejected = rule(U"việtt", U"", 0.0);
    rejected.timesRejected = 1;
    rejected.updatedAt = kNow - 3600;
    publish({}, {rejected}, {});
    EXPECT_FALSE(engine.check(committed({U"việtt"})).has_value());
    // A week later the guess is allowed again.
    rejected.updatedAt = kNow - Thresholds::kRejectionCooldownSeconds - 1;
    publish({}, {rejected}, {});
    EXPECT_TRUE(engine.check(committed({U"việtt"})).has_value());
}

TEST_F(AutoCorrectEngineTest, ExcludedAppsAreNeverCorrected) {
    // Nothing is excluded by default - editors and terminals get Vietnamese too.
    setLevel(AutoCorrectLevel::Balanced);
    auto c = committed({U"việtt"});
    c.focus.appName = "Code.exe";
    EXPECT_TRUE(engine.check(c).has_value());
    AutoCorrectSettings s;
    s.level = AutoCorrectLevel::Balanced;
    s.excludedApps = {"code.exe"};
    engine.setSettings(s);
    EXPECT_FALSE(engine.check(c).has_value()); // case-insensitive
    c.focus.appName = "notepad.exe";
    EXPECT_TRUE(engine.check(c).has_value());
}

TEST_F(AutoCorrectEngineTest, WithoutBaseIndexNothingHappens) {
    AutoCorrectEngine fresh;
    AutoCorrectSettings s;
    s.level = AutoCorrectLevel::Aggressive;
    fresh.setSettings(s);
    EXPECT_FALSE(fresh.check(committed({U"việtt"})).has_value());
}

} // namespace
} // namespace lankey::core::smart
