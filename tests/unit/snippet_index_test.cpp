// Reading snippets.json (Phase 5 A).
//
// The file is the user's, edited by hand as often as through the UI, so the tests are
// mostly about being forgiving with the file and strict with the matching.

#include <string>

#include <gtest/gtest.h>

#include "core/snippet/SnippetIndex.h"

namespace lankey::core::snippet {
namespace {

SnippetIndex indexOf(const std::string& json) {
    SnippetIndex index;
    const auto r = index.load(json);
    EXPECT_TRUE(r.has_value()) << (r ? "" : r.error().message);
    return index;
}

constexpr const char* kSample = R"({
  "variables": { "ten": "Phong", "cty": "LanKey" },
  "snippets": [
    { "abbr": "osnn", "body": "お世話になっております。", "auto": true },
    { "abbr": "ky", "body": "Trân trọng,\n{ten}" },
    { "abbr": "Ky", "body": "KHÁC" }
  ]
})";

TEST(SnippetIndex, ReadsSnippetsAndVariables) {
    const auto index = indexOf(kSample);
    EXPECT_EQ(index.size(), 3u);
    EXPECT_EQ(index.variables().at(U"ten"), U"Phong");
}

TEST(SnippetIndex, FindsByAbbreviation) {
    const auto index = indexOf(kSample);
    const auto* s = index.find(U"osnn");
    ASSERT_NE(s, nullptr);
    EXPECT_EQ(s->body, U"お世話になっております。");
    EXPECT_TRUE(s->autoExpand);
}

TEST(SnippetIndex, AbbreviationsAreCaseSensitive) {
    // "ky" and "Ky" are different triggers: an abbreviation is a key the user types on
    // purpose, and folding case would make "Ky" fire the wrong one mid-sentence.
    const auto index = indexOf(kSample);
    ASSERT_NE(index.find(U"ky"), nullptr);
    ASSERT_NE(index.find(U"Ky"), nullptr);
    EXPECT_EQ(index.find(U"Ky")->body, U"KHÁC");
    EXPECT_EQ(index.find(U"KY"), nullptr);
}

TEST(SnippetIndex, AutoExpandDefaultsToOff) {
    const auto index = indexOf(kSample);
    EXPECT_FALSE(index.find(U"ky")->autoExpand)
        << "expanding without being asked is opt-in, per snippet";
}

TEST(SnippetIndex, AnEmptyFileIsAnEmptyIndex) {
    const auto index = indexOf(R"({"snippets": []})");
    EXPECT_EQ(index.size(), 0u);
    EXPECT_EQ(index.find(U"ky"), nullptr);
}

TEST(SnippetIndex, RowsWithoutAnAbbreviationOrBodyAreSkipped) {
    const auto index = indexOf(R"({"snippets": [
        {"abbr": "", "body": "x"},
        {"abbr": "y", "body": ""},
        {"body": "no abbr"},
        {"abbr": "ok", "body": "fine"}
    ]})");
    EXPECT_EQ(index.size(), 1u);
    EXPECT_NE(index.find(U"ok"), nullptr);
}

TEST(SnippetIndex, TheLastRowWinsWhenAnAbbreviationRepeats) {
    const auto index = indexOf(R"({"snippets": [
        {"abbr": "ky", "body": "first"},
        {"abbr": "ky", "body": "second"}
    ]})");
    EXPECT_EQ(index.find(U"ky")->body, U"second");
}

TEST(SnippetIndex, BrokenJsonIsRefusedRatherThanPartlyRead) {
    SnippetIndex index;
    EXPECT_FALSE(index.load("{ not json").has_value());
}

TEST(SnippetIndex, TheLongestAbbreviationDecidesHowFarBackMatchingLooks) {
    const auto index = indexOf(kSample);
    EXPECT_EQ(index.maxAbbrLength(), 4); // "osnn"
}

TEST(SnippetIndex, BuildsFromTheSnippetsKeptInSettings) {
    core::model::SnippetSettings settings;
    settings.variables = {{"ten", "Phong"}};
    settings.items = {{"ky", "Trân trọng,\n{ten}", false},
                      {"osnn", "お世話に", true},
                      {"", "no abbreviation", false}};
    SnippetIndex index;
    index.load(settings);
    EXPECT_EQ(index.size(), 2u) << "the row with no trigger is skipped";
    EXPECT_EQ(index.variables().at(U"ten"), U"Phong");
    ASSERT_NE(index.find(U"osnn"), nullptr);
    EXPECT_TRUE(index.find(U"osnn")->autoExpand);
}

// --- the clipboard is only watched if the file asks -----------------------------------------

TEST(SnippetIndex, TheIndexSaysWhetherAnythingNeedsTheClipboard) {
    EXPECT_FALSE(indexOf(kSample).usesClipboard());
    const auto pasting = indexOf(R"({"snippets": [{"abbr": "p", "body": "-> {clipboard}"}]})");
    EXPECT_TRUE(pasting.usesClipboard());
}

TEST(SnippetIndex, ReplacingTheOnlyClipboardRowTakesTheClipboardBackOut) {
    // The answer has to come from the rows that survived, not from the rows that were
    // read: this one was overwritten before the file ended.
    const auto index = indexOf(R"({"snippets": [
        {"abbr": "p", "body": "{clipboard}"},
        {"abbr": "p", "body": "nothing pasted here"}
    ]})");
    EXPECT_FALSE(index.usesClipboard());
}

// --- what the popup filters on ------------------------------------------------------------

TEST(SnippetIndex, SearchMatchesTheAbbreviationOrTheBody) {
    const auto index = indexOf(kSample);
    auto hits = index.search(U"osnn");
    ASSERT_EQ(hits.size(), 1u);
    EXPECT_EQ(hits[0]->abbr, U"osnn");
    // The user remembers what the snippet says, not always what they called it.
    hits = index.search(U"trân");
    ASSERT_EQ(hits.size(), 1u);
    EXPECT_EQ(hits[0]->abbr, U"ky");
    EXPECT_EQ(index.search(U"").size(), 3u) << "no filter lists everything";
}

} // namespace
} // namespace lankey::core::snippet
