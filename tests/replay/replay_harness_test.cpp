#include <gtest/gtest.h>

#include "tests/fakes/FakeEngine.h"
#include "tests/replay/ReplayHarness.h"

namespace lankey::tests {
namespace {

using core::model::Modifier;
using core::model::VirtualKey;
using Kind = ReplayHarness::Event::Kind;

TEST(ReplayHarnessParse, AllEventKinds) {
    const auto events =
        ReplayHarness::parse("# comment\n"
                             "{\"k\":\"a\"}\n"
                             "{\"k\":\"Backspace\"}\n"
                             "{\"k\":\"Tab\",\"mods\":[\"Alt\"]}\n"
                             "{\"text\":\"Xy\"}\n"
                             "{\"focus\":{\"app\":\"chrome.exe\",\"password\":true}}\n"
                             "{\"click\":true}\n"
                             "{\"wait\":250}\n");
    ASSERT_TRUE(events.has_value()) << events.error().message;
    ASSERT_EQ(events->size(), 8u);
    EXPECT_EQ((*events)[0].key.unicode, U'a');
    EXPECT_EQ((*events)[1].key.key, VirtualKey::Backspace);
    EXPECT_EQ((*events)[2].key.key, VirtualKey::Tab);
    EXPECT_TRUE(has((*events)[2].key.modifiers, Modifier::Alt));
    EXPECT_EQ((*events)[3].key.unicode, U'X');
    EXPECT_TRUE(has((*events)[3].key.modifiers, Modifier::Shift));
    EXPECT_EQ((*events)[4].key.unicode, U'y');
    EXPECT_EQ((*events)[5].kind, Kind::Focus);
    EXPECT_EQ((*events)[5].app, "chrome.exe");
    EXPECT_TRUE((*events)[5].password);
    EXPECT_EQ((*events)[6].kind, Kind::Click);
    EXPECT_EQ((*events)[7].kind, Kind::Wait);
    EXPECT_EQ((*events)[7].waitMs, 250);
}

TEST(ReplayHarnessParse, RejectsGarbage) {
    EXPECT_FALSE(ReplayHarness::parse("{\"k\":\"ab\"}").has_value());
    EXPECT_FALSE(ReplayHarness::parse("{\"k\":\"a\",\"mods\":[\"Hyper\"]}").has_value());
    EXPECT_FALSE(ReplayHarness::parse("{\"nope\":1}").has_value());
    EXPECT_FALSE(ReplayHarness::parse("not json").has_value());
}

TEST(ReplayHarnessRun, ProducesScreenAndCommitsWithFakeEngine) {
    FakeEngine engine;
    const auto events = ReplayHarness::parse("{\"text\":\"ab cx.\"}\n{\"k\":\"Enter\"}\n");
    ASSERT_TRUE(events.has_value());
    const auto result = ReplayHarness::run(*events, engine);
    ASSERT_TRUE(result.has_value());
    // FakeEngine: x replaces the last char. Enter reaches the application as the line
    // break it inserts.
    EXPECT_EQ(result->screen, U"ab X.\n");
    ASSERT_EQ(result->commits.size(), 2u);
    EXPECT_EQ(result->commits[0], "ab|SP|0");
    EXPECT_EQ(result->commits[1], "ab x|.|1");
}

TEST(ReplayHarnessUtf8, RoundTrip) {
    const std::u32string s = U"chương trình ư";
    EXPECT_EQ(ReplayHarness::fromUtf8(ReplayHarness::toUtf8(s)), s);
}

} // namespace
} // namespace lankey::tests
