// InputPipeline x suggestions: the pending -> shown lifecycle (idle timer), prediction
// after a space, key handling while the popup is up, and what selecting does to the
// screen and to the phrase context.

#include <gtest/gtest.h>

#include "core/pipeline/InputPipeline.h"
#include "core/smart/suggest/SuggestionEngine.h"

#include "tests/fakes/FakeEngine.h"
#include "tests/support/PipelineRig.h"

namespace lankey::core::pipeline {
namespace {

using model::KeyEvent;
using model::LexiconEntry;
using model::Phrase;
using model::ReplacementReason;
using model::Syllable;
using model::VirtualKey;
using tests::FakeEngine;
using tests::PipelineRig;

KeyEvent named(VirtualKey k) {
    KeyEvent e;
    e.key = k;
    return e;
}

KeyEvent release(VirtualKey k) {
    KeyEvent e = named(k);
    e.isDown = false;
    return e;
}

LexiconEntry entry(std::initializer_list<const char32_t*> syllables, std::uint32_t freq) {
    LexiconEntry e;
    for (const auto* s : syllables)
        e.phrase.syllables.push_back(Syllable::fromComposed(s));
    e.frequency = freq;
    e.lastUsedAt = 1'700'000'000;
    return e;
}

struct SuggestTest : testing::Test {
    FakeEngine engine;
    smart::SuggestionEngine suggestions;
    PipelineRig rig{engine, &suggestions};

    void SetUp() override {
        suggestions.publish(smart::SuggestionEngine::buildSnapshot(
            {entry({U"chuong", U"trinh"}, 10), entry({U"chuong"}, 10), entry({U"chuyen"}, 10),
             entry({U"he", U"dieu", U"hanh"}, 10), entry({U"dieu", U"hanh"}, 10),
             entry({U"he", U"dieu", U"hanh", U"windows"}, 5)},
            1'700'000'000, {}));
    }
    const PopupState& lastPopup() const { return rig.popups.back(); }
    bool showing() const { return !rig.pipeline().popup().items.empty(); }
    bool pending() const { return !rig.pipeline().pendingPopup().items.empty(); }
};

// -- pending / idle timer -------------------------------------------------------------

TEST_F(SuggestTest, TypingOnlyParksSuggestionsAsPending) {
    rig.type("c");
    EXPECT_FALSE(pending()); // below min prefix
    rig.type("h");
    EXPECT_TRUE(pending());
    EXPECT_FALSE(showing());
    EXPECT_TRUE(lastPopup().pending);
    EXPECT_EQ(lastPopup().items[0].phrase.joined(), U"chuong trinh");
    // Tab must NOT be swallowed: nothing is visible yet.
    EXPECT_FALSE(rig.press(named(VirtualKey::Tab)));
}

TEST_F(SuggestTest, PromoteShowsOnlyIfNothingHappenedSince) {
    rig.type("ch");
    const auto gen = rig.pipeline().generation();
    rig.type("u");                      // user kept typing before the timer fired
    rig.pipeline().promotePending(gen); // stale timer
    EXPECT_FALSE(showing());
    rig.settle(); // timer for the newest list
    EXPECT_TRUE(showing());
    EXPECT_FALSE(lastPopup().pending);
    EXPECT_EQ(rig.pipeline().stats().suggestionsShown, 1u);
}

TEST_F(SuggestTest, ShownPopupFollowsTypingWithoutWaiting) {
    rig.type("chu");
    rig.settle();
    ASSERT_TRUE(showing());
    rig.type("o"); // still matches "chuong ..."
    EXPECT_TRUE(showing());
    EXPECT_FALSE(pending());
    rig.type("x"); // FakeEngine turns the last char into X: no match any more
    EXPECT_FALSE(showing());
    EXPECT_TRUE(lastPopup().items.empty());
}

TEST_F(SuggestTest, BoundaryHidesShownPopupAndCancelsPending) {
    rig.type("chu");
    rig.settle();
    rig.type(".");
    EXPECT_FALSE(showing());
    EXPECT_FALSE(pending());
    EXPECT_TRUE(lastPopup().items.empty());
}

// -- prediction after a space --------------------------------------------------------

TEST_F(SuggestTest, SpacePredictsTheNextWordsAsPending) {
    rig.type("he ");
    ASSERT_TRUE(pending());
    EXPECT_EQ(lastPopup().items[0].phrase.joined(), U"he dieu hanh");
    EXPECT_EQ(lastPopup().items[0].insert, U"dieu hanh");
    EXPECT_EQ(lastPopup().items[0].deleteCount, 0);
    rig.settle();
    EXPECT_TRUE(showing());
}

TEST_F(SuggestTest, NoPredictionAfterPunctuationOrEnter) {
    rig.type("he.");
    EXPECT_FALSE(pending());
    rig.type("he\n");
    EXPECT_FALSE(pending());
}

TEST_F(SuggestTest, PredictionUsesTwoWordsOfContextFirst) {
    rig.type("he dieu ");
    ASSERT_TRUE(pending());
    EXPECT_EQ(lastPopup().items[0].insert, U"hanh");
    // "he dieu hanh windows" also continues "he dieu": offered too, further down.
    EXPECT_GE(lastPopup().items.size(), 2u);
}

// -- selection -------------------------------------------------------------------------

TEST_F(SuggestTest, TabSelectsInsertsWithTrailingSpaceAndChains) {
    rig.type("he ");
    rig.settle();
    ASSERT_TRUE(showing());
    EXPECT_TRUE(rig.press(named(VirtualKey::Tab)));
    EXPECT_EQ(rig.screen(), U"he dieu hanh ");
    const auto& cmd = rig.sink.applied().back();
    EXPECT_EQ(cmd.reason, ReplacementReason::Suggestion);
    EXPECT_EQ(cmd.deleteCount, 0);
    // Context now holds the whole phrase, learning got one selection, no fake commits.
    ASSERT_EQ(rig.pipeline().window().committedCount(), 3u);
    EXPECT_EQ(rig.pipeline().window().committed[2].text, U"hanh");
    ASSERT_EQ(rig.selections.size(), 1u);
    EXPECT_EQ(rig.selections[0].joined(), U"he dieu hanh");
    EXPECT_EQ(rig.commits.size(), 1u); // only "he" was typed by hand
    // Chained prediction is pending right away ("windows").
    ASSERT_TRUE(pending());
    EXPECT_EQ(lastPopup().items[0].insert, U"windows");
    EXPECT_FALSE(showing());
}

TEST_F(SuggestTest, TabKeyUpIsSwallowedToo) {
    rig.type("chu");
    rig.settle();
    EXPECT_TRUE(rig.press(named(VirtualKey::Tab)));
    EXPECT_TRUE(rig.press(release(VirtualKey::Tab)));
    // A normal key-up afterwards passes through.
    EXPECT_FALSE(rig.press(release(VirtualKey::A)));
}

TEST_F(SuggestTest, CompletionSelectionReplacesTypedPrefix) {
    rig.type("chu");
    rig.settle();
    rig.press(named(VirtualKey::Tab));
    EXPECT_EQ(rig.screen(), U"chuong trinh ");
    EXPECT_EQ(rig.sink.applied().back().deleteCount, 3);
    EXPECT_EQ(rig.pipeline().window().committedCount(), 2u);
}

TEST_F(SuggestTest, ArrowsMoveSelectionAndTabPicksIt) {
    rig.type("chu");
    rig.settle();
    ASSERT_GE(rig.pipeline().popup().items.size(), 2u);
    EXPECT_TRUE(rig.press(named(VirtualKey::ArrowDown)));
    EXPECT_TRUE(rig.press(release(VirtualKey::ArrowDown)));
    EXPECT_EQ(rig.pipeline().popup().selected, 1);
    const auto expected = rig.pipeline().popup().items[1].phrase.joined();
    rig.press(named(VirtualKey::Tab));
    EXPECT_EQ(rig.selections.back().joined(), expected);
    EXPECT_EQ(rig.screen(), expected + U" ");
}

TEST_F(SuggestTest, EnterSelectsLikeTabWhileShown) {
    rig.type("chu");
    rig.settle();
    ASSERT_TRUE(showing());
    EXPECT_TRUE(rig.press(named(VirtualKey::Enter))); // swallowed: it picked the suggestion
    EXPECT_EQ(rig.selections.size(), 1u);
    EXPECT_EQ(rig.screen(), U"chuong trinh ");
    // Without a popup, Enter is just Enter.
    EXPECT_FALSE(rig.press(named(VirtualKey::Enter)));
}

TEST_F(SuggestTest, EscapeDismissesUntilSyllableEnds) {
    rig.type("chu");
    rig.settle();
    EXPECT_TRUE(rig.press(named(VirtualKey::Escape)));
    EXPECT_FALSE(showing());
    EXPECT_FALSE(pending());
    rig.type("o"); // same syllable: stays quiet
    EXPECT_FALSE(pending());
    rig.type(" ch"); // next syllable: back
    EXPECT_TRUE(pending());
}

TEST_F(SuggestTest, PopupKeysPassThroughWhenNothingIsShown) {
    EXPECT_FALSE(rig.press(named(VirtualKey::Tab)));
    EXPECT_FALSE(rig.press(named(VirtualKey::Escape)));
    EXPECT_FALSE(rig.press(named(VirtualKey::ArrowDown)));
    rig.type("chu"); // pending only
    EXPECT_FALSE(rig.press(named(VirtualKey::ArrowDown)));
}

TEST_F(SuggestTest, NoSuggestionsInPasswordFields) {
    rig.focus.setFocus({"chrome.exe", true, 1});
    rig.type("chu");
    EXPECT_FALSE(pending());
    rig.type(" he ");
    EXPECT_FALSE(pending());
}

TEST_F(SuggestTest, FocusChangeAndClickDropEverything) {
    rig.type("chu");
    rig.settle();
    rig.focus.setFocus({"other.exe", false, 2});
    EXPECT_FALSE(showing());
    EXPECT_TRUE(lastPopup().items.empty());
    rig.type("chu");
    rig.pipeline().onPointerClick();
    EXPECT_FALSE(pending());
}

TEST_F(SuggestTest, VietnameseOffIsTransparent) {
    rig.pipeline().setVietnameseEnabled(false);
    rig.type("chu");
    EXPECT_FALSE(pending());
    EXPECT_EQ(rig.screen(), U"chu");
    rig.pipeline().setVietnameseEnabled(true);
    rig.type(" chu");
    EXPECT_TRUE(pending());
}

} // namespace
} // namespace lankey::core::pipeline
