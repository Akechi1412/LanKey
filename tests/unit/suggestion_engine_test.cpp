#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "core/convert/ConversionIndex.h"
#include "core/model/Thresholds.h"
#include "core/smart/suggest/Scorer.h"
#include "core/smart/suggest/SuggestionEngine.h"
#include "core/text/VietnameseText.h"

namespace lankey::core::smart {
namespace {

using model::LexiconEntry;
using model::Phrase;
using model::Suggestion;
using model::SuggestionQuery;
using model::SuggestionSettings;
using model::Syllable;
using model::Thresholds;

constexpr std::int64_t kNow = 1'700'000'000;

Phrase phrase(const std::u32string& joined) {
    Phrase p;
    std::u32string cur;
    for (const char32_t c : joined) {
        if (c == U' ') {
            p.syllables.push_back(Syllable::fromComposed(cur));
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    if (!cur.empty()) p.syllables.push_back(Syllable::fromComposed(cur));
    return p;
}

LexiconEntry entry(const std::u32string& joined, std::uint32_t freq, std::int64_t lastUsed = kNow) {
    LexiconEntry e;
    e.phrase = phrase(joined);
    e.frequency = freq;
    e.firstSeenAt = lastUsed;
    e.lastUsedAt = lastUsed;
    return e;
}

SuggestionQuery query(std::initializer_list<const char32_t*> context, const std::u32string& typed) {
    SuggestionQuery q;
    for (const auto* c : context)
        q.context.emplace_back(c); // literals are already lowercase NFC
    q.typed = typed;
    q.prefix = Syllable::fromComposed(typed).text;
    return q;
}

std::vector<std::u32string> joined(const model::SuggestionList& list) {
    std::vector<std::u32string> out;
    for (const auto& s : list)
        out.push_back(s.phrase.joined());
    return out;
}

struct SuggestionEngineTest : testing::Test {
    SuggestionEngine engine;
    SuggestionSettings settings;

    void publish(const std::vector<LexiconEntry>& entries) {
        engine.setSettings(settings);
        engine.publish(SuggestionEngine::buildSnapshot(entries, kNow, settings));
    }

    // The user's own VI-EN-JA glossary.
    void publishGlossary(const std::string& csv) {
        auto index = std::make_shared<convert::ConversionIndex>();
        const auto r = index->load(csv);
        EXPECT_TRUE(r.has_value()) << (r ? "" : r.error().message);
        engine.publishConversions(std::move(index));
    }
};

constexpr const char* kGlossary = "vi,en,ja,note\n"
                                  "\u0111\u0103ng nh\u1eadp,login,\u30ed\u30b0\u30a4\u30f3,\n"
                                  "l\u1ed7i,bug,,\n";

TEST_F(SuggestionEngineTest, GlossaryOffersTheOtherColumnsAfterAWordIsCommitted) {
    settings.enabled = true;
    publish({});
    publishGlossary(kGlossary);
    // Prediction: the word is finished, nothing is being typed.
    const auto list = engine.suggest(query({U"\u0111\u0103ng", U"nh\u1eadp"}, U""));
    ASSERT_EQ(list.size(), 2u);
    EXPECT_EQ(list[0].kind, model::Suggestion::Kind::Conversion);
    EXPECT_EQ(list[0].insert, U"login");
    EXPECT_EQ(list[0].language, model::Language::English);
    EXPECT_EQ(list[0].replacedSyllables, 2);
    EXPECT_EQ(list[1].insert, U"\u30ed\u30b0\u30a4\u30f3");
    EXPECT_EQ(list[1].language, model::Language::Japanese);
}

TEST_F(SuggestionEngineTest, GlossaryWorksWithNothingLearnedYet) {
    // A new user who wrote dictionary.csv gets their own terms back on day one, before
    // the lexicon has anything in it at all.
    settings.enabled = true;
    engine.setSettings(settings);
    publishGlossary(kGlossary);
    const auto list = engine.suggest(query({U"l\u1ed7i"}, U""));
    ASSERT_EQ(list.size(), 1u);
    EXPECT_EQ(list[0].insert, U"bug");
}

TEST_F(SuggestionEngineTest, GlossaryIsQuietInTheMiddleOfAWord) {
    // "\u0111\u0103ng nh\u1ead" is half typed; converting it would replace letters the user is
    // still writing. Conversions only ever follow a committed word.
    settings.enabled = true;
    publish({});
    publishGlossary(kGlossary);
    const auto list = engine.suggest(query({U"\u0111\u0103ng"}, U"nh\u1eadp"));
    for (const auto& s : list)
        EXPECT_NE(s.kind, model::Suggestion::Kind::Conversion);
}

TEST_F(SuggestionEngineTest, GlossaryFollowsTheModeTheUserIsTypingIn) {
    settings.enabled = true;
    publish({});
    publishGlossary(kGlossary);
    auto q = query({U"login"}, U"");
    q.sourceLanguage = model::Language::English;
    const auto list = engine.suggest(q);
    ASSERT_FALSE(list.empty());
    EXPECT_EQ(list[0].insert, U"\u0111\u0103ng nh\u1eadp");
    EXPECT_EQ(list[0].language, model::Language::Vietnamese);
}

TEST_F(SuggestionEngineTest, GlossaryComesBeforeWhatWasMerelyLearned) {
    settings.enabled = true;
    publish({entry(U"l\u1ed7i th\u1eddi", 50), entry(U"l\u1ed7i h\u1ec7 th\u1ed1ng", 40)});
    publishGlossary(kGlossary);
    const auto list = engine.suggest(query({U"l\u1ed7i"}, U""));
    ASSERT_FALSE(list.empty());
    EXPECT_EQ(list[0].kind, model::Suggestion::Kind::Conversion) << "the user wrote this row";
    EXPECT_EQ(list[0].insert, U"bug");
}

TEST(Scorer, StaticPrefersFrequentRecentAndLonger) {
    SuggestionSettings s;
    const double frequent = Scorer::staticScore(entry(U"a", 50), kNow, s);
    const double rare = Scorer::staticScore(entry(U"a", 5), kNow, s);
    EXPECT_GT(frequent, rare);
    const double old = Scorer::staticScore(entry(U"a", 5, kNow - 60 * 86400), kNow, s);
    EXPECT_GT(rare, old);
    const double two = Scorer::staticScore(entry(U"a b", 5), kNow, s);
    EXPECT_GT(two, rare); // same frequency: the 2-syllable phrase wins (w5)
}

TEST(Scorer, DynamicRewardsSavedKeystrokes) {
    SuggestionSettings s;
    EXPECT_GT(Scorer::dynamicScore(12, 2, false, s), Scorer::dynamicScore(6, 2, false, s));
    EXPECT_GT(Scorer::dynamicScore(6, 2, true, s), Scorer::dynamicScore(6, 2, false, s));
    EXPECT_EQ(Scorer::dynamicScore(0, 0, false, s), 0.0);
}

TEST_F(SuggestionEngineTest, NoSnapshotNoSuggestions) {
    EXPECT_TRUE(engine.suggest(query({}, U"ch")).empty());
    EXPECT_FALSE(engine.hasSnapshot());
}

TEST_F(SuggestionEngineTest, PrefixLookupRanksLongerPhraseFirst) {
    publish({entry(U"chương", 10), entry(U"chương trình", 10), entry(U"chuyên", 10)});
    const auto list = engine.suggest(query({}, U"chư"));
    ASSERT_EQ(list.size(), 2u);
    EXPECT_EQ(list[0].phrase.joined(), U"chương trình");
    EXPECT_EQ(list[0].insert, U"chương trình");
    EXPECT_EQ(list[0].deleteCount, 3);
    EXPECT_EQ(list[1].phrase.joined(), U"chương");
}

TEST_F(SuggestionEngineTest, ContextLookupFindsPhraseContinuation) {
    publish({entry(U"hệ điều hành", 10), entry(U"điều hành", 10), entry(U"điện", 10)});
    // After "hệ", typing "đi" should propose the 3-syllable phrase and the 2-syllable one.
    const auto list = engine.suggest(query({U"hệ"}, U"đi"));
    const auto names = joined(list);
    ASSERT_GE(names.size(), 2u);
    EXPECT_EQ(names[0], U"hệ điều hành");
    // Insert only what is missing after the context that is already on screen.
    EXPECT_EQ(list[0].insert, U"điều hành");
    EXPECT_EQ(list[0].deleteCount, 2);
    EXPECT_TRUE(std::ranges::find(names, U"điện") != names.end());
}

TEST_F(SuggestionEngineTest, PredictionWithEmptyPrefixContinuesTheContext) {
    publish({entry(U"hệ điều hành", 10), entry(U"hệ điều hành windows", 5), entry(U"hệp", 10),
             entry(U"điều", 10)});
    const auto list = engine.suggest(query({U"hệ"}, U""));
    const auto names = joined(list);
    ASSERT_EQ(names.size(), 2u);
    EXPECT_EQ(names[0], U"hệ điều hành"); // shorter, more frequent
    EXPECT_EQ(list[0].insert, U"điều hành");
    EXPECT_EQ(list[0].deleteCount, 0);
    EXPECT_EQ(names[1], U"hệ điều hành windows");
    // "hệp" starts with "hệ" but is not a continuation at a word boundary.
    EXPECT_TRUE(std::ranges::find(names, U"hệp") == names.end());
}

TEST_F(SuggestionEngineTest, PredictionNeedsContext) {
    publish({entry(U"chương trình", 10)});
    EXPECT_TRUE(engine.suggest(query({}, U"")).empty());
}

TEST_F(SuggestionEngineTest, PredictionFallsBackToShorterContext) {
    publish({entry(U"điều hành", 10)});
    // "hệ điều" has nothing; "điều" alone continues to "điều hành".
    const auto list = engine.suggest(query({U"hệ", U"điều"}, U""));
    ASSERT_EQ(list.size(), 1u);
    EXPECT_EQ(list[0].insert, U"hành");
}

TEST_F(SuggestionEngineTest, RespectsMinPrefixAndFrequencyThreshold) {
    publish({entry(U"chương", 10), entry(U"chưa", Thresholds::kLearnMinFrequency - 1)});
    EXPECT_TRUE(engine.suggest(query({}, U"c")).empty()); // below minPrefixLength (2)
    const auto list = engine.suggest(query({}, U"chư"));
    EXPECT_EQ(joined(list), (std::vector<std::u32string>{U"chương"}));
}

TEST_F(SuggestionEngineTest, NeverSuggestsWhatIsAlreadyFullyTyped) {
    publish({entry(U"chương", 10)});
    EXPECT_TRUE(engine.suggest(query({}, U"chương")).empty());
}

TEST_F(SuggestionEngineTest, BlockedEntriesAreSkipped) {
    auto blocked = entry(U"chương", 10);
    blocked.blocked = true;
    publish({blocked, entry(U"chưa", 10)});
    EXPECT_EQ(joined(engine.suggest(query({}, U"chư"))), (std::vector<std::u32string>{U"chưa"}));
}

TEST_F(SuggestionEngineTest, CapsAtMaxSuggestions) {
    std::vector<LexiconEntry> entries;
    for (int i = 0; i < 12; ++i) {
        entries.push_back(entry(
            U"ab" + std::u32string(1, static_cast<char32_t>(U'a' + static_cast<unsigned>(i))), 10));
    }
    publish(entries);
    EXPECT_EQ(engine.suggest(query({}, U"ab")).size(),
              static_cast<std::size_t>(Thresholds::kMaxSuggestions));
}

TEST_F(SuggestionEngineTest, DisabledInSettings) {
    settings.enabled = false;
    publish({entry(U"chương", 10)});
    EXPECT_TRUE(engine.suggest(query({}, U"chư")).empty());
}

TEST(ApplyCasing, MirrorsWhatWasTyped) {
    EXPECT_EQ(text::applyCasing(U"chư", U"chương trình"), U"chương trình");
    EXPECT_EQ(text::applyCasing(U"Chư", U"chương trình"), U"Chương trình");
    EXPECT_EQ(text::applyCasing(U"CHƯ", U"chương trình"), U"CHƯƠNG TRÌNH");
    EXPECT_EQ(text::applyCasing(U"C", U"chương"), U"Chương"); // a single capital = capitalised
    EXPECT_EQ(text::applyCasing(U"", U"x"), U"x");
}

} // namespace
} // namespace lankey::core::smart
