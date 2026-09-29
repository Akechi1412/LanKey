// Picking a glossary conversion from the popup, through the real pipeline.
//
// A conversion is the one suggestion that REPLACES words already on screen instead of
// finishing the one being typed, so the interesting part is the bookkeeping: what is
// deleted, what the phrase window holds afterwards, and what is not learned from it.

#include <memory>
#include <string>

#include <gtest/gtest.h>

#include "core/convert/ConversionIndex.h"
#include "core/smart/suggest/SuggestionEngine.h"

#include "tests/fakes/FakeEngine.h"
#include "tests/support/PipelineRig.h"

namespace lankey::tests {
namespace {

using core::model::Suggestion;
using core::model::VirtualKey;

// FakeEngine passes ASCII straight through and the typist walks the string a byte at a
// time, so the rows here are ASCII. What these tests exercise is the bookkeeping around a
// replacement, not the words being replaced.
constexpr const char* kGlossary = "vi,en,ja,note\n"
                                  "dang nhap,login,ログイン,\n"
                                  "loi,bug,,\n";

struct ConversionPipelineTest : testing::Test {
    FakeEngine engine;
    core::smart::SuggestionEngine suggestions;
    PipelineRig rig{engine, &suggestions};

    ConversionPipelineTest() {
        core::model::SuggestionSettings s;
        s.enabled = true;
        suggestions.setSettings(s);
        suggestions.publish(core::smart::SuggestionEngine::buildSnapshot({}, 0, s));
        auto index = std::make_shared<core::convert::ConversionIndex>();
        EXPECT_TRUE(index->load(kGlossary).has_value());
        suggestions.publishConversions(std::move(index));
    }

    core::model::KeyEvent named(VirtualKey key) const {
        core::model::KeyEvent e;
        e.key = key;
        e.isDown = true;
        return e;
    }
};

TEST_F(ConversionPipelineTest, TabReplacesTheTypedWordWithTheGlossaryTerm) {
    rig.type("dang nhap ");
    rig.settle();
    ASSERT_FALSE(rig.pipeline().popup().items.empty());
    const auto& first = rig.pipeline().popup().items.front();
    ASSERT_EQ(first.kind, Suggestion::Kind::Conversion);
    EXPECT_EQ(first.insert, U"login");

    EXPECT_TRUE(rig.press(named(VirtualKey::Tab)));
    // The two Vietnamese syllables and the space after them are gone, replaced by the
    // English term - not appended to it.
    EXPECT_EQ(rig.screen(), U"login ");
}

TEST_F(ConversionPipelineTest, TheWindowHoldsTheTermAfterwardsNotTheOriginal) {
    rig.type("dang nhap ");
    rig.settle();
    rig.press(named(VirtualKey::Tab));
    const auto& committed = rig.pipeline().window().committed;
    ASSERT_EQ(committed.size(), 1u);
    EXPECT_EQ(committed[0].text, U"login");
}

TEST_F(ConversionPipelineTest, AConversionIsNotLearnedAsSomethingTheUserTyped) {
    rig.type("dang nhap ");
    rig.settle();
    rig.press(named(VirtualKey::Tab));
    // The glossary row is the user's own data already; counting it as a typed phrase
    // would inflate the lexicon with words they never wrote.
    EXPECT_TRUE(rig.selections.empty());
}

TEST_F(ConversionPipelineTest, TextBeforeTheTermIsLeftAlone) {
    rig.type("trang dang nhap ");
    rig.settle();
    ASSERT_FALSE(rig.pipeline().popup().items.empty());
    ASSERT_EQ(rig.pipeline().popup().items.front().kind, Suggestion::Kind::Conversion);
    rig.press(named(VirtualKey::Tab));
    EXPECT_EQ(rig.screen(), U"trang login ");
}

TEST_F(ConversionPipelineTest, ASingleSyllableTermReplacesOnlyItself) {
    rig.type("bao loi ");
    rig.settle();
    ASSERT_FALSE(rig.pipeline().popup().items.empty());
    rig.press(named(VirtualKey::Tab));
    EXPECT_EQ(rig.screen(), U"bao bug ");
}

TEST_F(ConversionPipelineTest, NothingIsOfferedForAWordOutsideTheGlossary) {
    rig.type("hom nay ");
    rig.settle();
    for (const auto& s : rig.pipeline().popup().items)
        EXPECT_NE(s.kind, Suggestion::Kind::Conversion);
}

} // namespace
} // namespace lankey::tests
