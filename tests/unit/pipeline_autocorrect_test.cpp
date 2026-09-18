// InputPipeline x AutoCorrect: applying a worker correction to the exact on-screen span,
// the generation guard, Undo (Backspace / Ctrl+Z) and detecting the user's own fixes
// (delete whole syllables, retype them) for the ManualCorrectionDetector.

#include <gtest/gtest.h>

#include "core/model/Thresholds.h"
#include "core/pipeline/InputPipeline.h"

#include "tests/fakes/FakeEngine.h"
#include "tests/support/PipelineRig.h"

namespace lankey::core::pipeline {
namespace {

using model::Correction;
using model::CorrectionSource;
using model::KeyEvent;
using model::Modifier;
using model::ReplacementReason;
using model::Syllable;
using model::Thresholds;
using model::VirtualKey;
using tests::FakeEngine;
using tests::PipelineRig;

KeyEvent named(VirtualKey k, Modifier mods = Modifier::None) {
    KeyEvent e;
    e.key = k;
    e.modifiers = mods;
    return e;
}

KeyEvent release(VirtualKey k) {
    KeyEvent e = named(k);
    e.isDown = false;
    return e;
}

Correction correction(std::initializer_list<const char32_t*> corrected, const char32_t* wrongKey,
                      std::uint64_t generation) {
    Correction c;
    c.syllableCount = static_cast<int>(corrected.size());
    for (const auto* s : corrected)
        c.corrected.push_back(Syllable{s, {}});
    c.wrongKey = wrongKey;
    c.expectedGeneration = generation;
    c.source = CorrectionSource::BaseDictionary;
    c.confidence = 0.8;
    return c;
}

struct AutoCorrectPipelineTest : testing::Test {
    FakeEngine engine;
    PipelineRig rig{engine};

    std::uint64_t gen() const { return rig.pipeline().generation(); }
    const model::SyllableCommitted& lastCommit() const { return rig.commits.back(); }
};

TEST_F(AutoCorrectPipelineTest, CorrectionRetypesTheSpanWithTheTypedCasing) {
    rig.type("Chuong ");
    ASSERT_TRUE(rig.pipeline().applyCorrection(correction({U"chuong2"}, U"chuong", gen())));

    const auto& cmd = rig.sink.applied().back();
    EXPECT_EQ(cmd.deleteCount, 7); // "Chuong" + the space
    EXPECT_EQ(cmd.insert, U"Chuong2 ");
    EXPECT_EQ(cmd.reason, ReplacementReason::AutoCorrect);
    EXPECT_EQ(rig.screen(), U"Chuong2 ");
    // The context now reads the corrected phrase.
    ASSERT_EQ(rig.pipeline().window().committedCount(), 1u);
    EXPECT_EQ(rig.pipeline().window().committed[0].text, U"chuong2");
    EXPECT_EQ(rig.pipeline().window().committed[0].typed, U"Chuong2");
    ASSERT_EQ(rig.correctionsApplied.size(), 1u);
    EXPECT_EQ(rig.correctionsApplied[0], U"chuong");
    EXPECT_EQ(rig.pipeline().stats().correctionsApplied, 1u);
    // Never silent: the UI was handed a notice.
    ASSERT_FALSE(rig.popups.empty());
    EXPECT_EQ(rig.popups.back().notice, U"Chuong \u2192 Chuong2");
    EXPECT_TRUE(rig.popups.back().items.empty());
}

TEST_F(AutoCorrectPipelineTest, StaleGenerationIsDropped) {
    rig.type("chuong ");
    const auto g = gen();
    rig.type("t"); // the user kept typing before the worker answered
    EXPECT_FALSE(rig.pipeline().applyCorrection(correction({U"chuong2"}, U"chuong", g)));
    EXPECT_EQ(rig.screen(), U"chuong t");
    EXPECT_EQ(rig.pipeline().stats().correctionsDropped, 1u);
    EXPECT_TRUE(rig.correctionsApplied.empty());
}

TEST_F(AutoCorrectPipelineTest, BackspaceRightAfterUndoesAndTellsTheWorker) {
    rig.type("chuong ");
    ASSERT_TRUE(rig.pipeline().applyCorrection(correction({U"chuong2"}, U"chuong", gen())));
    ASSERT_EQ(rig.screen(), U"chuong2 ");

    EXPECT_TRUE(rig.press(named(VirtualKey::Backspace)));
    EXPECT_TRUE(rig.press(release(VirtualKey::Backspace))); // the release is eaten too
    EXPECT_EQ(rig.screen(), U"chuong ");
    const auto& cmd = rig.sink.applied().back();
    EXPECT_EQ(cmd.reason, ReplacementReason::Undo);
    EXPECT_EQ(cmd.deleteCount, 8);
    EXPECT_EQ(cmd.insert, U"chuong ");
    EXPECT_EQ(rig.pipeline().window().committed[0].text, U"chuong");
    ASSERT_EQ(rig.correctionsRejected.size(), 1u);
    EXPECT_EQ(rig.correctionsRejected[0], U"chuong");
    EXPECT_EQ(rig.correctionsRejectedTo[0], U"chuong2");
    EXPECT_EQ(rig.pipeline().stats().correctionsUndone, 1u);
    const auto recent = rig.pipeline().recentCorrections();
    ASSERT_EQ(recent.size(), 1u);
    EXPECT_EQ(recent[0].original, U"chuong");
    EXPECT_EQ(recent[0].corrected, U"chuong2");
    EXPECT_TRUE(recent[0].undone);

    // A second Backspace is an ordinary Backspace.
    EXPECT_FALSE(rig.press(named(VirtualKey::Backspace)));
    EXPECT_EQ(rig.screen(), U"chuong");
}

TEST_F(AutoCorrectPipelineTest, CtrlZUndoesAsWell) {
    rig.type("chuong ");
    ASSERT_TRUE(rig.pipeline().applyCorrection(correction({U"chuong2"}, U"chuong", gen())));
    EXPECT_TRUE(rig.press(named(VirtualKey::Z, Modifier::Control)));
    EXPECT_EQ(rig.screen(), U"chuong ");
    EXPECT_EQ(rig.correctionsRejected.size(), 1u);
    // The app never saw the Ctrl+Z, so it must not undo anything of its own.
    EXPECT_EQ(rig.sink.applied().back().reason, ReplacementReason::Undo);
}

TEST_F(AutoCorrectPipelineTest, UndoWindowClosesWithTheNextKeyOrAfterThreeSeconds) {
    rig.type("chuong ");
    ASSERT_TRUE(rig.pipeline().applyCorrection(correction({U"chuong2"}, U"chuong", gen())));
    rig.type("a");
    EXPECT_FALSE(rig.press(named(VirtualKey::Backspace))); // deletes the "a", no undo
    EXPECT_EQ(rig.screen(), U"chuong2 ");
    EXPECT_TRUE(rig.correctionsRejected.empty());

    rig.type("b ");
    ASSERT_TRUE(rig.pipeline().applyCorrection(correction({U"b2"}, U"b", gen())));
    rig.clock.advanceMs(Thresholds::kUndoWindowMs + 1);
    EXPECT_FALSE(rig.press(named(VirtualKey::Backspace)));
    EXPECT_TRUE(rig.correctionsRejected.empty());
}

TEST_F(AutoCorrectPipelineTest, PhraseCorrectionCoversSeveralSyllables) {
    rig.type("toi Sua loi ");
    ASSERT_TRUE(rig.pipeline().applyCorrection(correction({U"sua2", U"loi2"}, U"sua loi", gen())));
    const auto& cmd = rig.sink.applied().back();
    EXPECT_EQ(cmd.deleteCount, 8); // "Sua loi" + space
    EXPECT_EQ(cmd.insert, U"Sua2 loi2 ");
    EXPECT_EQ(rig.screen(), U"toi Sua2 loi2 ");
    ASSERT_EQ(rig.pipeline().window().committedCount(), 3u);
    EXPECT_EQ(rig.pipeline().window().committed[1].text, U"sua2");
    EXPECT_EQ(rig.pipeline().window().committed[2].text, U"loi2");

    EXPECT_TRUE(rig.press(named(VirtualKey::Backspace)));
    EXPECT_EQ(rig.screen(), U"toi Sua loi ");
    EXPECT_EQ(rig.pipeline().window().committed[1].typed, U"Sua");
}

TEST_F(AutoCorrectPipelineTest, UnreconstructibleSeparatorsDropThePhraseCorrection) {
    rig.type("sua  loi "); // two spaces between the syllables
    EXPECT_FALSE(rig.pipeline().applyCorrection(correction({U"sua2", U"loi2"}, U"sua loi", gen())));
    EXPECT_EQ(rig.screen(), U"sua  loi ");
    EXPECT_EQ(rig.pipeline().stats().correctionsDropped, 1u);
    // The last syllable alone is still fine.
    EXPECT_TRUE(rig.pipeline().applyCorrection(correction({U"loi2"}, U"loi", gen())));
    EXPECT_EQ(rig.screen(), U"sua  loi2 ");
}

TEST_F(AutoCorrectPipelineTest, NoOpRuleChangesNothing) {
    rig.type("chuong ");
    const auto before = rig.sink.applied().size();
    EXPECT_FALSE(rig.pipeline().applyCorrection(correction({U"chuong"}, U"chuong", gen())));
    EXPECT_EQ(rig.sink.applied().size(), before);
    EXPECT_TRUE(rig.correctionsApplied.empty());
}

TEST_F(AutoCorrectPipelineTest, DeletingAndRetypingTheLastSyllableIsReportedAsAFix) {
    rig.type("sua loi ");
    ASSERT_EQ(rig.commits.size(), 2u);
    rig.type("\b\b\b\b"); // "loi " gone
    EXPECT_EQ(rig.screen(), U"sua ");
    rig.type("loi2 ");
    ASSERT_EQ(rig.commits.size(), 3u);
    ASSERT_EQ(lastCommit().retypedFrom.size(), 1u);
    EXPECT_EQ(lastCommit().retypedFrom[0].text, U"loi");
    // The context is whole again: the untouched prefix plus the retyped syllable.
    ASSERT_EQ(lastCommit().window.committed.size(), 2u);
    EXPECT_EQ(lastCommit().window.committed[0].text, U"sua");
    EXPECT_EQ(lastCommit().window.committed[1].text, U"loi2");
    EXPECT_EQ(rig.pipeline().window().committedCount(), 2u);
    EXPECT_EQ(rig.pipeline().stats().retypesDetected, 1u);
}

TEST_F(AutoCorrectPipelineTest, RetypingTwoSyllablesReportsBoth) {
    rig.type("toi sua loi ");
    rig.type("\b\b\b\b\b\b\b\b"); // "sua loi " gone
    EXPECT_EQ(rig.screen(), U"toi ");
    rig.type("sua2 ");
    EXPECT_TRUE(lastCommit().retypedFrom.empty()); // one of two so far
    rig.type("loi2 ");
    ASSERT_EQ(lastCommit().retypedFrom.size(), 2u);
    EXPECT_EQ(lastCommit().retypedFrom[0].text, U"sua");
    EXPECT_EQ(lastCommit().retypedFrom[1].text, U"loi");
    ASSERT_EQ(lastCommit().window.committed.size(), 3u);
    EXPECT_EQ(lastCommit().window.committed[0].text, U"toi");
    EXPECT_EQ(lastCommit().window.committed[1].text, U"sua2");
    EXPECT_EQ(lastCommit().window.committed[2].text, U"loi2");
}

TEST_F(AutoCorrectPipelineTest, PartialDeleteGluesTheKeptHeadBackOn) {
    // "sua" -> delete " " and "a" -> type "b ": the engine only composes "b", the screen
    // shows "sub". The commit must be "sub", and it is a retype of "sua".
    rig.type("Sua loi ");
    rig.type("\b\b");
    EXPECT_EQ(rig.screen(), U"Sua lo");
    rig.type("b");
    EXPECT_EQ(rig.pipeline().window().current, U"lob"); // suggestions see the whole word
    rig.type(" ");
    EXPECT_EQ(rig.screen(), U"Sua lob ");
    ASSERT_EQ(lastCommit().window.committed.size(), 2u);
    EXPECT_EQ(lastCommit().window.committed[0].typed, U"Sua");
    EXPECT_EQ(lastCommit().window.committed[1].typed, U"lob");
    ASSERT_EQ(lastCommit().retypedFrom.size(), 1u);
    EXPECT_EQ(lastCommit().retypedFrom[0].text, U"loi");
    EXPECT_FALSE(lastCommit().uncertain);
    // A correction of that syllable retypes the whole of it, not the tail.
    ASSERT_TRUE(rig.pipeline().applyCorrection(correction({U"lob2"}, U"lob", gen())));
    EXPECT_EQ(rig.sink.applied().back().deleteCount, 4);
    EXPECT_EQ(rig.screen(), U"Sua lob2 ");
}

TEST_F(AutoCorrectPipelineTest, DeletingIntoUnknownTextMarksTheNextCommitUncertain) {
    rig.sink.typeThrough(U'x'); // text that was there before LanKey
    rig.type("\b");
    rig.type("ab ");
    EXPECT_TRUE(lastCommit().uncertain);
    rig.type("cd ");
    EXPECT_FALSE(lastCommit().uncertain);
}

TEST_F(AutoCorrectPipelineTest, DeletingPastTheWindowMarksUncertainToo) {
    rig.type("ab ");
    rig.type("\b\b\b\b"); // one more than we know about
    rig.type("cd ");
    EXPECT_TRUE(lastCommit().uncertain);
    EXPECT_TRUE(lastCommit().retypedFrom.empty());
}

TEST_F(AutoCorrectPipelineTest, LateDeletionIsNotAFixButStillRestoresContext) {
    rig.type("sua loi ");
    rig.clock.advanceMs(Thresholds::kRetypeWindowMs + 1);
    rig.type("\b\b\b\b");
    rig.type("loi2 ");
    EXPECT_TRUE(lastCommit().retypedFrom.empty());
    ASSERT_EQ(lastCommit().window.committed.size(), 2u);
    EXPECT_EQ(lastCommit().window.committed[0].text, U"sua");
}

TEST_F(AutoCorrectPipelineTest, PunctuationCountsTowardsTheSyllableEdge) {
    rig.type("sua loi, ");  // "loi" + "," + " "
    rig.type("\b\b\b\b\b"); // "loi, " gone
    EXPECT_EQ(rig.screen(), U"sua ");
    rig.type("loi2 ");
    ASSERT_EQ(lastCommit().retypedFrom.size(), 1u);
    EXPECT_EQ(lastCommit().retypedFrom[0].text, U"loi");
}

TEST_F(AutoCorrectPipelineTest, FocusChangeAbandonsARetype) {
    rig.type("sua loi ");
    rig.type("\b\b\b\b");
    rig.pipeline().onPointerClick();
    rig.type("loi2 ");
    EXPECT_TRUE(lastCommit().retypedFrom.empty());
}

} // namespace
} // namespace lankey::core::pipeline
