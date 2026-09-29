// Expanding a snippet through the real pipeline (Phase 5 A, layer 2).
//
// The whole of this layer turns on one distinction: an abbreviation is a sequence of KEYS,
// and what those keys leave on screen is something else entirely. Get that wrong and the
// expansion eats a character of the word in front of it, or never fires at all. Most of
// the tests below are about that, and about the other half of the job - being quiet.

#include <optional>
#include <string>

#include <gtest/gtest.h>

#include "core/interfaces/ISnippetSource.h"
#include "core/interfaces/ISuggestionProvider.h"
#include "core/snippet/SnippetIndex.h"

#include "tests/fakes/FakeEngine.h"
#include "tests/support/PipelineRig.h"

namespace lankey::tests {
namespace {

using core::model::Suggestion;
using core::model::VirtualKey;

// A stand-in for Telex: 's' is a tone mark and is absorbed into the letter before it, so
// the KEYS "osnn" stand on screen as "Onn". Nothing here is Vietnamese - what these tests
// need is only that the keys and the screen have different lengths, the way they really do.
class ToneEngine final : public core::IVietnameseEngine {
public:
    [[nodiscard]] core::model::EngineResult process(const core::model::KeyEvent& key) override {
        using core::model::EngineResult;
        core::model::EngineResult r;
        if (key.injectedBySelf || !key.isDown) {
            r.composed = composed_;
            return r;
        }
        if (key.key == VirtualKey::Backspace) {
            if (!composed_.text.empty()) composed_.text.pop_back();
        } else if (key.unicode == U's' && !composed_.text.empty()) {
            const auto toned = static_cast<char32_t>(composed_.text.back() - 32); // 'o' -> 'O'
            composed_.text.back() = toned;
            composed_.vietnameseTransformApplied = true;
            r.action = EngineResult::Action::Replace;
            r.deleteCount = 1;
            r.insert = std::u32string(1, toned);
        } else if (key.isLetter() || key.isDigit()) {
            composed_.text.push_back(key.unicode);
        } else {
            reset();
        }
        r.composed = composed_;
        return r;
    }

    void reset() override {
        composed_.text.clear();
        composed_.vietnameseTransformApplied = false;
    }
    void configure(const core::model::EngineSettings& s) override { settings_ = s; }
    [[nodiscard]] const core::model::EngineSettings& settings() const override { return settings_; }

private:
    core::model::ComposedText composed_;
    core::model::EngineSettings settings_;
};

// Always has something to offer, so the ordering test has something to be ahead of.
class AlwaysSuggests final : public core::ISuggestionProvider {
public:
    [[nodiscard]] core::model::SuggestionList
    suggest(const core::model::SuggestionQuery& query) const override {
        if (query.typed.empty()) return {};
        Suggestion s;
        s.insert = query.typed + U"...";
        s.deleteCount = static_cast<int>(query.typed.size());
        s.score = 1.0;
        return {s};
    }
};

// The real index and the real template behind the interface: the only thing faked here is
// where the clock and the clipboard come from, and the user's answer to a dialog.
class TestSnippets final : public core::ISnippetSource {
public:
    void load(const std::string& json) {
        ASSERT_TRUE(index_.load(json).has_value());
        context_.variables = index_.variables();
        context_.now.tm_year = 126; // 2026-09-28
        context_.now.tm_mon = 8;
        context_.now.tm_mday = 28;
    }

    [[nodiscard]] std::optional<core::SnippetMatch>
    match(std::u32string_view rawWord) const override {
        ++lookups;
        const auto* s = index_.find(rawWord);
        if (s == nullptr) return std::nullopt;
        return core::SnippetMatch{s->abbr, core::snippet::firstLine(s->body), s->autoExpand};
    }

    [[nodiscard]] std::optional<core::snippet::Rendered>
    expand(std::u32string_view abbr) const override {
        const auto* s = index_.find(abbr);
        if (s == nullptr || cancel) return std::nullopt;
        return s->parsed.render(context_);
    }

    bool cancel = false;     // "the user closed the parameter dialog"
    mutable int lookups = 0; // how often the hot path asked

private:
    core::snippet::SnippetIndex index_;
    core::snippet::RenderContext context_;
};

constexpr const char* kSnippets = R"({
  "variables": { "ten": "Phong" },
  "snippets": [
    { "abbr": "ky",   "body": "Tran trong,\nPhong" },
    { "abbr": "osnn", "body": "Xin chao", "auto": true },
    { "abbr": "td",   "body": "Ngay {date}" },
    { "abbr": "note", "body": "[{cursor}] xong" },
    { "abbr": "kg",   "body": "Kinh gui {param:Ten}," }
  ]
})";

struct SnippetPipelineTest : testing::Test {
    FakeEngine engine;
    TestSnippets snippets;
    PipelineRig rig{engine, nullptr, &snippets};

    SnippetPipelineTest() { snippets.load(kSnippets); }

    static core::model::KeyEvent named(VirtualKey key) {
        core::model::KeyEvent e;
        e.key = key;
        e.isDown = true;
        return e;
    }
};

// --- offering it -----------------------------------------------------------------------

TEST_F(SnippetPipelineTest, AnAbbreviationIsOfferedWhileItIsBeingTyped) {
    rig.type("ky");
    ASSERT_FALSE(rig.pipeline().popup().items.empty());
    const auto& first = rig.pipeline().popup().items.front();
    EXPECT_EQ(first.kind, Suggestion::Kind::Snippet);
    EXPECT_EQ(first.insert, U"Tran trong,") << "the popup shows the first line, not the body";
}

TEST_F(SnippetPipelineTest, ASnippetIsShownWithoutWaitingForTheIdleTimer) {
    // No settle(): the user typed the trigger themselves, so there is nothing to second
    // guess and nothing to wait for.
    rig.type("ky");
    EXPECT_FALSE(rig.pipeline().popup().items.empty());
}

TEST_F(SnippetPipelineTest, NothingIsOfferedForAWordThatIsNotAnAbbreviation) {
    rig.type("kyz");
    for (const auto& s : rig.pipeline().popup().items)
        EXPECT_NE(s.kind, Suggestion::Kind::Snippet);
}

TEST_F(SnippetPipelineTest, ASnippetIsOfferedAheadOfWhatTheSmartLayerGuessed) {
    AlwaysSuggests guesses;
    PipelineRig withBoth{engine, &guesses, &snippets};
    withBoth.type("ky");
    const auto& items = withBoth.pipeline().popup().items;
    ASSERT_GE(items.size(), 2u) << "the guess should still be there, just not first";
    EXPECT_EQ(items.front().kind, Suggestion::Kind::Snippet);
}

// --- keys, not the screen --------------------------------------------------------------

TEST_F(SnippetPipelineTest, TheAbbreviationIsMatchedOnTheKeysTypedNotOnWhatIsOnScreen) {
    ToneEngine tone;
    PipelineRig toned{tone, nullptr, &snippets};
    toned.type("osnn");
    ASSERT_EQ(toned.screen(), U"Onn") << "the engine absorbed the 's'";
    ASSERT_FALSE(toned.pipeline().popup().items.empty())
        << "the keys were o-s-n-n, which is the trigger";
    EXPECT_EQ(toned.pipeline().popup().items.front().kind, Suggestion::Kind::Snippet);
}

TEST_F(SnippetPipelineTest, WhatIsDeletedIsTheAbbreviationAsItStandsOnScreen) {
    ToneEngine tone;
    PipelineRig toned{tone, nullptr, &snippets};
    toned.type("di osnn");
    ASSERT_EQ(toned.screen(), U"di Onn");
    toned.press(named(VirtualKey::Tab));
    // Three characters stood on screen, not the four that were typed. Deleting four would
    // have taken the space before it.
    EXPECT_EQ(toned.screen(), U"di Xin chao");
    EXPECT_EQ(toned.sink.applied().back().deleteCount, 3);
}

// --- expanding it ----------------------------------------------------------------------

TEST_F(SnippetPipelineTest, TabPutsTheBodyOnScreenInPlaceOfTheAbbreviation) {
    rig.type("ky");
    ASSERT_TRUE(rig.press(named(VirtualKey::Tab))) << "the key belongs to the popup";
    EXPECT_EQ(rig.screen(), U"Tran trong,\nPhong");
}

TEST_F(SnippetPipelineTest, NothingExpandsOnItsOwnUnlessTheSnippetSaysSo) {
    // "ky" has no "auto" flag, so a space after it is just a space.
    rig.type("ky ");
    EXPECT_EQ(rig.screen(), U"ky ");
}

TEST_F(SnippetPipelineTest, AnAutoSnippetExpandsOnTheTerminatingKey) {
    rig.type("osnn ");
    // The terminating key is kept: the user meant to type a space, and swallowing it would
    // leave them pressing space twice for every expansion.
    EXPECT_EQ(rig.screen(), U"Xin chao ");
}

TEST_F(SnippetPipelineTest, AnAutoSnippetExpandsMidSentenceWithoutTouchingWhatCameBefore) {
    rig.type("noi osnn ");
    EXPECT_EQ(rig.screen(), U"noi Xin chao ");
}

TEST_F(SnippetPipelineTest, TheBodyIsRenderedNotInsertedLiterally) {
    rig.type("td");
    rig.press(named(VirtualKey::Tab));
    EXPECT_EQ(rig.screen(), U"Ngay 28/09/2026");
}

TEST_F(SnippetPipelineTest, TheCursorHoleComesBackAsCaretMoves) {
    rig.type("note");
    rig.press(named(VirtualKey::Tab));
    const auto& cmd = rig.sink.applied().back();
    EXPECT_EQ(cmd.insert, U"[] xong");
    // The fake screen has no caret to move, so the command is what there is to check:
    // "] xong" is six characters, and six steps back from the end is between the brackets.
    EXPECT_EQ(cmd.caretLeft, 6);
}

TEST_F(SnippetPipelineTest, ABodyWithABlankExpandsAndPutsTheCaretInTheBlank) {
    rig.type("kg");
    rig.press(named(VirtualKey::Tab));
    const auto& cmd = rig.sink.applied().back();
    EXPECT_EQ(cmd.insert, U"Kinh gui ,");
    EXPECT_EQ(cmd.caretLeft, 1) << "the user types the name where the name goes";
}

TEST_F(SnippetPipelineTest, ACancelledParameterDialogLeavesTheAbbreviationAsTyped) {
    snippets.cancel = true;
    rig.type("ky");
    rig.press(named(VirtualKey::Tab));
    EXPECT_EQ(rig.screen(), U"ky") << "nothing was inserted, so nothing may be deleted";
    EXPECT_TRUE(rig.pipeline().popup().items.empty()) << "but the offer is taken down";
}

// --- staying quiet ---------------------------------------------------------------------

TEST_F(SnippetPipelineTest, EscapeStopsTheSnippetForThisWordExpansionIncluded) {
    rig.type("osnn");
    ASSERT_TRUE(rig.press(named(VirtualKey::Escape)));
    rig.type(" ");
    EXPECT_EQ(rig.screen(), U"osnn ") << "dismissed means dismissed, not merely hidden";
}

TEST_F(SnippetPipelineTest, DismissingFromTheUiIsTheSameAsEscape) {
    // The UI is the only side that can see there is nowhere for the text to go - the user
    // clicked out of the input. What it asks for has to be as final as Esc, or the popup
    // would come straight back on the next key and go on swallowing Tab.
    rig.type("osnn");
    ASSERT_FALSE(rig.pipeline().popup().items.empty());
    rig.pipeline().dismissPopup();
    EXPECT_TRUE(rig.pipeline().popup().items.empty());
    rig.type(" ");
    EXPECT_EQ(rig.screen(), U"osnn ") << "and the auto-expanding snippet stays put too";
}

TEST_F(SnippetPipelineTest, NoSnippetIsOfferedInAPasswordField) {
    core::model::FocusContext focus;
    focus.isPasswordField = true;
    rig.focus.setFocus(focus);
    rig.type("osnn ");
    EXPECT_TRUE(rig.pipeline().popup().items.empty());
    EXPECT_EQ(rig.screen(), U"osnn ") << "and it does not expand behind the dots either";
}

TEST_F(SnippetPipelineTest, BackspaceWithinTheWordKeepsMatchingTheKeysThatAreLeft) {
    rig.type("kyz");
    ASSERT_TRUE(rig.pipeline().popup().items.empty() ||
                rig.pipeline().popup().items.front().kind != Suggestion::Kind::Snippet);
    rig.press(named(VirtualKey::Backspace));
    ASSERT_FALSE(rig.pipeline().popup().items.empty());
    EXPECT_EQ(rig.pipeline().popup().items.front().kind, Suggestion::Kind::Snippet)
        << "the keys that are left are k-y";
}

TEST_F(SnippetPipelineTest, DeletingIntoFinishedTextStopsMatchingUntilTheNextWord) {
    rig.type("ky ");
    rig.press(named(VirtualKey::Backspace)); // the space goes; "ky" is back under the caret
    // "ky" is on screen, but which keys made it is not something the pipeline knows any
    // more - in Telex the same three letters can come from several. Guessing from the
    // letters is exactly the mistake this layer exists to avoid.
    for (const auto& s : rig.pipeline().popup().items)
        EXPECT_NE(s.kind, Suggestion::Kind::Snippet);
    rig.type(" ky");
    ASSERT_FALSE(rig.pipeline().popup().items.empty());
    EXPECT_EQ(rig.pipeline().popup().items.front().kind, Suggestion::Kind::Snippet)
        << "a fresh word starts a fresh match";
}

// --- what the rest of the system is told ------------------------------------------------

TEST_F(SnippetPipelineTest, ASnippetBodyIsNotLearnedAsSomethingTheUserTyped) {
    rig.type("osnn ");
    for (const auto& c : rig.commits)
        EXPECT_TRUE(c.window.committed.empty()) << "a body is boilerplate, not their prose";
    EXPECT_TRUE(rig.pipeline().window().committed.empty());
}

TEST_F(SnippetPipelineTest, TheWordAfterASnippetIsNotJoinedToIt) {
    rig.type("osnn ");
    rig.commits.clear();
    rig.type("nay ");
    ASSERT_EQ(rig.commits.size(), 1u);
    const auto& window = rig.commits.front().window;
    ASSERT_EQ(window.committed.size(), 1u) << "the body is not phrase context";
    EXPECT_EQ(window.committed.front().text, U"nay");
}

TEST_F(SnippetPipelineTest, TheHotPathAsksTheSourceOncePerKey) {
    const int before = snippets.lookups;
    rig.type("abc");
    EXPECT_LE(snippets.lookups - before, 3);
}

} // namespace
} // namespace lankey::tests
