#include <gtest/gtest.h>

#include "core/pipeline/InputPipeline.h"

#include "tests/fakes/FakeEngine.h"
#include "tests/support/PipelineRig.h"

namespace lankey::core::pipeline {
namespace {

using model::KeyEvent;
using model::Modifier;
using model::ReplacementReason;
using model::TextReplacement;
using model::VirtualKey;
using tests::FakeEngine;
using tests::PipelineRig;
using tests::Typist;

KeyEvent named(VirtualKey k, Modifier mods = Modifier::None) {
    KeyEvent e;
    e.key = k;
    e.modifiers = mods;
    return e;
}

struct PipelineTest : testing::Test {
    FakeEngine engine;
    PipelineRig rig{engine};
};

TEST_F(PipelineTest, PassThroughKeysReachTheAppAndBuildCurrentSyllable) {
    rig.type("ab");
    EXPECT_EQ(rig.screen(), U"ab");
    EXPECT_EQ(rig.pipeline().window().current, U"ab");
    EXPECT_TRUE(rig.commits.empty());
    EXPECT_TRUE(rig.sink.applied().empty());
}

TEST_F(PipelineTest, EngineReplaceIsAppliedThroughSinkWithCurrentGeneration) {
    rig.type("ax"); // FakeEngine: 'x' replaces last char with 'X'
    EXPECT_EQ(rig.screen(), U"X");
    ASSERT_EQ(rig.sink.applied().size(), 1u);
    const auto& cmd = rig.sink.applied()[0];
    EXPECT_EQ(cmd.deleteCount, 1);
    EXPECT_EQ(cmd.insert, U"X");
    EXPECT_EQ(cmd.reason, ReplacementReason::Engine);
    EXPECT_EQ(cmd.expectedGeneration, rig.pipeline().generation());
}

TEST_F(PipelineTest, SwallowedKeyChangesNothing) {
    rig.type("a#b");
    EXPECT_EQ(rig.screen(), U"ab");
}

TEST_F(PipelineTest, SpaceCommitsSyllableWithTerminatorAndKeepsPhraseContext) {
    rig.type("chao ban ");
    ASSERT_EQ(rig.commits.size(), 2u);
    EXPECT_EQ(rig.commits[0].syllable().text, U"chao");
    EXPECT_EQ(rig.commits[0].terminator, U' ');
    EXPECT_EQ(rig.commits[0].window.committedCount(), 1u);
    EXPECT_EQ(rig.commits[1].syllable().text, U"ban");
    // Space does NOT reset the window: "chao ban" is visible as a 2-syllable phrase.
    ASSERT_EQ(rig.commits[1].window.committedCount(), 2u);
    EXPECT_EQ(rig.commits[1].window.candidatePhrases()[0].joined(), U"chao ban");
    EXPECT_EQ(rig.screen(), U"chao ban ");
}

TEST_F(PipelineTest, SentencePunctuationCommitsThenResetsWindow) {
    rig.type("chao. ban ");
    ASSERT_EQ(rig.commits.size(), 2u);
    EXPECT_EQ(rig.commits[0].terminator, U'.');
    // After '.', "ban" starts a new paragraph: window holds only "ban".
    EXPECT_EQ(rig.commits[1].window.committedCount(), 1u);
}

TEST_F(PipelineTest, EnterCommitsWithNewlineTerminator) {
    rig.type("xin\n");
    ASSERT_EQ(rig.commits.size(), 1u);
    EXPECT_EQ(rig.commits[0].terminator, U'\n');
    EXPECT_TRUE(rig.pipeline().window().empty());
}

TEST_F(PipelineTest, CommitCarriesTransformFlagFocusAndTimestamp) {
    rig.focus.setFocus({"notepad.exe", false, 42});
    rig.clock.setMonotonicMs(5000);
    rig.type("ax ");
    ASSERT_EQ(rig.commits.size(), 1u);
    EXPECT_TRUE(rig.commits[0].vietnameseTransformApplied);
    EXPECT_EQ(rig.commits[0].focus.appName, "notepad.exe");
    EXPECT_GE(rig.commits[0].timestampMs, 5000);
    EXPECT_EQ(rig.commits[0].generation, rig.pipeline().generation());
}

TEST_F(PipelineTest, CommitEventIsACopyNotALiveView) {
    rig.type("a ");
    rig.type("b ");
    // The first event must still describe the world as it was at that moment.
    EXPECT_EQ(rig.commits[0].window.committedCount(), 1u);
    EXPECT_EQ(rig.commits[1].window.committedCount(), 2u);
}

TEST_F(PipelineTest, SystemModifierResetsContextAndPassesThrough) {
    rig.type("xin ");
    rig.type("ch");
    EXPECT_FALSE(rig.press(named(VirtualKey::Tab, Modifier::Alt))); // Alt+Tab
    EXPECT_TRUE(rig.pipeline().window().empty());
    // No commit for the half-typed "ch".
    EXPECT_EQ(rig.commits.size(), 1u);
    rig.type("ao ");
    // "ao" is not joined to "xin": the window was reset.
    EXPECT_EQ(rig.commits.back().window.committedCount(), 1u);
}

TEST_F(PipelineTest, FocusChangeDiscardsCurrentSyllableAndResetsWindow) {
    rig.type("xin ch");
    rig.focus.setFocus({"chrome.exe", false, 7});
    EXPECT_TRUE(rig.pipeline().window().empty());
    EXPECT_EQ(rig.commits.size(), 1u);
}

TEST_F(PipelineTest, PointerClickResetsWindow) {
    rig.type("xin chao ");
    rig.pipeline().onPointerClick();
    EXPECT_TRUE(rig.pipeline().window().empty());
}

TEST_F(PipelineTest, BackspaceIntoCommittedTextResetsWindow) {
    rig.type("xin ");
    EXPECT_EQ(rig.pipeline().window().committedCount(), 1u);
    rig.type("\b"); // nothing being composed -> deleting the space
    EXPECT_TRUE(rig.pipeline().window().empty());
}

TEST_F(PipelineTest, BackspaceWithinSyllableKeepsWindow) {
    rig.type("xin ch\b");
    EXPECT_EQ(rig.pipeline().window().committedCount(), 1u);
    EXPECT_EQ(rig.pipeline().window().current, U"c");
}

TEST_F(PipelineTest, KeyUpAndInjectedKeysAreIgnored) {
    const auto before = rig.pipeline().generation();
    KeyEvent up = Typist::toKeyEvent('a');
    up.isDown = false;
    EXPECT_FALSE(rig.press(up));
    KeyEvent injected = Typist::toKeyEvent('a');
    injected.injectedBySelf = true;
    EXPECT_FALSE(rig.press(injected));
    EXPECT_EQ(rig.pipeline().generation(), before);
    EXPECT_TRUE(rig.pipeline().window().current.empty());
}

TEST_F(PipelineTest, EveryKeyDownBumpsGeneration) {
    const auto g0 = rig.pipeline().generation();
    rig.type("abc");
    EXPECT_EQ(rig.pipeline().generation(), g0 + 3);
}

TEST_F(PipelineTest, ExternalReplacementAppliedOnlyWhenGenerationMatches) {
    rig.type("chuong ");
    TextReplacement fix;
    fix.deleteCount = 7;
    fix.insert = U"chuongX ";
    fix.reason = ReplacementReason::AutoCorrect;
    fix.expectedGeneration = rig.pipeline().generation();

    EXPECT_TRUE(rig.pipeline().applyReplacement(fix));
    EXPECT_EQ(rig.screen(), U"chuongX ");
    EXPECT_EQ(rig.pipeline().stats().replacementsDroppedByGeneration, 0u);
    // Applying bumped the generation: the same command is now stale.
    EXPECT_FALSE(rig.pipeline().applyReplacement(fix));
    EXPECT_EQ(rig.pipeline().stats().replacementsDroppedByGeneration, 1u);
}

TEST_F(PipelineTest, ExternalReplacementDroppedWhenUserTypedMeanwhile) {
    rig.type("chuong ");
    TextReplacement fix;
    fix.deleteCount = 7;
    fix.insert = U"chuongX ";
    fix.reason = ReplacementReason::AutoCorrect;
    fix.expectedGeneration = rig.pipeline().generation();

    rig.type("t"); // user keeps typing before the worker answers
    EXPECT_FALSE(rig.pipeline().applyReplacement(fix));
    EXPECT_EQ(rig.screen(), U"chuong t"); // nothing was eaten
}

} // namespace
} // namespace lankey::core::pipeline
