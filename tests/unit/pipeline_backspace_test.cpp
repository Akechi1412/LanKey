// Backspace through the real engine + InputPipeline.
//
// Backspace is where three models of "what is on screen" have to agree: the engine's own
// composition, the pipeline's committed window + spans, and the application. A
// disagreement rarely shows up as wrong text - it shows up as a wrong phrase handed to
// AutoCorrect, to the suggestion provider and to the worker, which then rewrite or learn
// something the user never typed. So these tests check the commits, not only the screen.
//
// Dogfooding 2026-09-24 found four such disagreements; each has a test here.

#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "tests/replay/ReplayHarness.h"
#include "tests/unit/engine_registry.h"

namespace lankey::tests {
namespace {

struct Outcome {
    std::string screen;
    std::vector<std::string> commits;
};

// Runs a keylog (the replay fixture format, without the files) through the first
// registered engine. Comparing adapters is what the conformance suite is for; one engine
// is enough to pin down the pipeline.
Outcome replay(const std::string& keylog) {
    const auto engines = registeredEngines();
    if (engines.empty()) return {};
    auto engine = engines.front().make();
    const auto events = ReplayHarness::parse(keylog);
    EXPECT_TRUE(events.has_value()) << (events ? "" : events.error().message);
    if (!events) return {};
    const auto result = ReplayHarness::run(*events, *engine);
    EXPECT_TRUE(result.has_value()) << (result ? "" : result.error().message);
    if (!result) return {};
    return {ReplayHarness::toUtf8(result->screen), result->commits};
}

bool haveEngine() {
    return !registeredEngines().empty();
}

#define SKIP_WITHOUT_ENGINE()                                                                      \
    if (!haveEngine()) GTEST_SKIP() << "no engine adapter built"

// --- inside one syllable --------------------------------------------------------------------

TEST(PipelineBackspace, RetypesTheLastLetter) {
    SKIP_WITHOUT_ENGINE();
    // "tiếng", Backspace, "g": the tone stays where it was.
    const auto r = replay(R"({"text":"tieengs"}
{"k":"Backspace"}
{"text":"g "})");
    EXPECT_EQ(r.screen, "tiếng ");
    EXPECT_EQ(r.commits, (std::vector<std::string>{"tiếng|SP|1"}));
}

TEST(PipelineBackspace, DeletingAVietnameseWordAwayLeavesTheNextOneEnglish) {
    SKIP_WITHOUT_ENGINE();
    // "việt" needed a transform; "nam" does not. If the flag survived the deletion, the
    // closed dictionary would treat the next plain word as Vietnamese and try to fix it.
    const auto r = replay(R"({"text":"vieejt"}
{"k":"Backspace"}
{"k":"Backspace"}
{"k":"Backspace"}
{"k":"Backspace"}
{"text":"nam "})");
    EXPECT_EQ(r.screen, "nam ");
    EXPECT_EQ(r.commits, (std::vector<std::string>{"nam|SP|0"}));
}

TEST(PipelineBackspace, HalfDeletedSyllableIsCommittedWhole) {
    SKIP_WITHOUT_ENGINE();
    // Dogfood shape: "sủa ", Backspace x3 leaves "s", then "uwar" is typed. The commit
    // must be the word on screen, or AutoCorrect would "fix" the fragment.
    const auto r = replay(R"({"text":"suar "}
{"k":"Backspace"}
{"k":"Backspace"}
{"k":"Backspace"}
{"text":"uwar "})");
    EXPECT_EQ(r.screen, "sửa ");
    EXPECT_EQ(r.commits.back(), "sửa|SP|1");
}

TEST(PipelineBackspace, RebuildingTheSameWordCommitsWhatIsOnScreen) {
    SKIP_WITHOUT_ENGINE();
    const auto r = replay(R"({"text":"hocj "}
{"k":"Backspace"}
{"k":"Backspace"}
{"k":"Backspace"}
{"text":"ocj "})");
    EXPECT_EQ(r.screen, "học ");
    EXPECT_EQ(r.commits.back(), "học|SP|1");
}

// --- the separator between syllables ---------------------------------------------------------

TEST(PipelineBackspace, ToneKeyAfterDeletingTheSpaceFixesTheWordEverywhere) {
    SKIP_WITHOUT_ENGINE();
    // Adding a tone to a word that was already finished: delete the space, press the tone
    // key. The engine rewrites the word by deleting characters this thread also believes
    // it is holding - the commit must be "chào", not "chao" + "ào".
    const auto r = replay(R"({"text":"chao "}
{"k":"Backspace"}
{"text":"f "})");
    EXPECT_EQ(r.screen, "chào ");
    EXPECT_EQ(r.commits, (std::vector<std::string>{"chao|SP|0", "chào|SP|1"}));
}

TEST(PipelineBackspace, ToneKeyAfterDeletingTheSpaceOnATwoLetterVowel) {
    SKIP_WITHOUT_ENGINE();
    // The same, where the engine rewrites two characters instead of one.
    const auto r = replay(R"({"text":"caau "}
{"k":"Backspace"}
{"text":"j "})");
    EXPECT_EQ(r.screen, "cậu ");
    EXPECT_EQ(r.commits, (std::vector<std::string>{"câu|SP|1", "cậu|SP|1"}));
}

TEST(PipelineBackspace, DeletedSpaceRetypedKeepsTheSyllableWhole) {
    SKIP_WITHOUT_ENGINE();
    // The user deletes only the space after a finished syllable and puts it straight back.
    // Nothing changed, so the word must not be committed a second time (the worker would
    // learn it twice) and the next word must not be glued onto it ("chàobạn").
    const auto r = replay(R"({"text":"chaof "}
{"k":"Backspace"}
{"text":" banj "})");
    EXPECT_EQ(r.screen, "chào bạn ");
    EXPECT_EQ(r.commits, (std::vector<std::string>{"chào|SP|1", "chào bạn|SP|1"}));
}

TEST(PipelineBackspace, DeletedSpaceThenMoreLettersExtendsTheSyllable) {
    SKIP_WITHOUT_ENGINE();
    // Same deletion, but the user keeps typing: "xin" + "h" really is one word.
    const auto r = replay(R"({"text":"xin "}
{"k":"Backspace"}
{"text":"h "})");
    EXPECT_EQ(r.screen, "xinh ");
    EXPECT_EQ(r.commits.back(), "xinh|SP|0");
}

TEST(PipelineBackspace, DeletedPunctuationRetypedKeepsTheSyllableWhole) {
    SKIP_WITHOUT_ENGINE();
    const auto r = replay(R"({"text":"chaof,"}
{"k":"Backspace"}
{"text":", banj "})");
    EXPECT_EQ(r.screen, "chào, bạn ");
    EXPECT_EQ(r.commits.back(), "chào bạn|SP|1");
}

TEST(PipelineBackspace, DeletingOneOfTwoSeparatorsIsNotARetype) {
    SKIP_WITHOUT_ENGINE();
    // Two spaces were typed and one is deleted: the syllable is still finished and still
    // separated from what comes next, so nothing is being retyped.
    const auto r = replay(R"({"text":"chaof  "}
{"k":"Backspace"}
{"text":"banj "})");
    EXPECT_EQ(r.screen, "chào bạn ");
    EXPECT_EQ(r.commits, (std::vector<std::string>{"chào|SP|1", "chào bạn|SP|1"}));
}

// --- whole words ------------------------------------------------------------------------------

TEST(PipelineBackspace, RetypedWordReplacesTheOldOneInTheWindow) {
    SKIP_WITHOUT_ENGINE();
    const auto r = replay(R"({"text":"xin chaof "}
{"k":"Backspace"}
{"k":"Backspace"}
{"k":"Backspace"}
{"k":"Backspace"}
{"k":"Backspace"}
{"text":"banj "})");
    EXPECT_EQ(r.screen, "xin bạn ");
    EXPECT_EQ(r.commits.back(), "xin bạn|SP|1");
}

TEST(PipelineBackspace, DeletingEverythingStartsACleanPhrase) {
    SKIP_WITHOUT_ENGINE();
    const auto r = replay(R"({"text":"xin "}
{"k":"Backspace"}
{"k":"Backspace"}
{"k":"Backspace"}
{"k":"Backspace"}
{"text":"chaof "})");
    EXPECT_EQ(r.screen, "chào ");
    EXPECT_EQ(r.commits, (std::vector<std::string>{"xin|SP|0", "chào|SP|1"}));
}

TEST(PipelineBackspace, DeletingBothWordsAndRetypingThemRebuildsThePhrase) {
    SKIP_WITHOUT_ENGINE();
    const auto r = replay(R"({"text":"vieejt nam "}
{"k":"Backspace"}
{"k":"Backspace"}
{"k":"Backspace"}
{"k":"Backspace"}
{"k":"Backspace"}
{"k":"Backspace"}
{"k":"Backspace"}
{"k":"Backspace"}
{"k":"Backspace"}
{"text":"vieejt nam "})");
    EXPECT_EQ(r.screen, "việt nam ");
    EXPECT_EQ(r.commits.back(), "việt nam|SP|0");
}

TEST(PipelineBackspace, DeletingPastWhatWeSawStillCommitsWhatIsTyped) {
    SKIP_WITHOUT_ENGINE();
    // Nothing was typed through LanKey: the text being deleted came from somewhere else.
    const auto r = replay(R"({"k":"Backspace"}
{"k":"Backspace"}
{"text":"chaof "})");
    EXPECT_EQ(r.screen, "chào ");
    EXPECT_EQ(r.commits, (std::vector<std::string>{"chào|SP|1"}));
}

TEST(PipelineBackspace, DeletingAndRetypingTwiceInARowStaysConsistent) {
    SKIP_WITHOUT_ENGINE();
    const auto r = replay(R"({"text":"xin chaof "}
{"k":"Backspace"}
{"k":"Backspace"}
{"k":"Backspace"}
{"k":"Backspace"}
{"k":"Backspace"}
{"text":"banj "}
{"k":"Backspace"}
{"k":"Backspace"}
{"k":"Backspace"}
{"k":"Backspace"}
{"text":"chaof "})");
    EXPECT_EQ(r.screen, "xin chào ");
    EXPECT_EQ(r.commits.back(), "xin chào|SP|1");
}

TEST(PipelineBackspace, EnterEndsThePhraseAndBackspaceDoesNotReachBack) {
    SKIP_WITHOUT_ENGINE();
    // Enter is a paragraph break: the window was reset there, so deleting after it must
    // not resurrect "chào" as context for the next word.
    const auto r = replay(R"({"text":"chaof"}
{"k":"Enter"}
{"text":"banj"}
{"k":"Backspace"}
{"k":"Backspace"}
{"k":"Backspace"}
{"text":"em "})");
    EXPECT_EQ(r.screen, "chào\nem ");
    EXPECT_EQ(r.commits.back(), "em|SP|0");
}

// --- after LanKey itself rewrote the screen ------------------------------------------------

TEST(PipelineBackspace, TypingOnAfterUndoingACorrection) {
    SKIP_WITHOUT_ENGINE();
    // Backspace right after a correction puts the user's own spelling back; typing then
    // carries on normally.
    const auto r = replay(R"({"lexicon":[["đường",5]]}
{"autocorrect":"cautious"}
{"text":"dduowngr "}
{"k":"Backspace"}
{"text":"ddi "})");
    EXPECT_EQ(r.screen, "đưởng đi ");
    EXPECT_EQ(r.commits.back(), "đưởng đi|SP|1");
}

TEST(PipelineBackspace, DeletingIntoAWordLanKeyRewroteDoesNotRestoreRawKeys) {
    SKIP_WITHOUT_ENGINE();
    // The correction and its undo replaced text behind the engine's back. If the engine
    // still remembers the keys of the word it composed, the next word break "restores"
    // them over text that no longer exists and the line reads "dduowngrdi".
    const auto r = replay(R"({"lexicon":[["đường",5]]}
{"autocorrect":"cautious"}
{"text":"dduowngr "}
{"k":"Backspace"}
{"k":"Backspace"}
{"text":"ddi "})");
    EXPECT_EQ(r.screen, "đưởngđi ");
}

TEST(PipelineBackspace, RetypedSeparatorDoesNotFeedAutoCorrectAGluedWord) {
    SKIP_WITHOUT_ENGINE();
    const auto r = replay(R"({"autocorrect":"aggressive"}
{"text":"chaof "}
{"k":"Backspace"}
{"text":" banj "})");
    EXPECT_EQ(r.screen, "chào bạn ");
}

} // namespace
} // namespace lankey::tests
