// Snippet bodies and what they expand to (Phase 5 A).
//
// A snippet body is text the user wrote with holes in it. The holes are the whole point,
// and the rules around them are where this can go wrong on somebody's screen: an
// unrecognised hole must stay as typed rather than vanish, and the caret has to land where
// the user put {cursor}.

#include <map>
#include <string>

#include <gtest/gtest.h>

#include "core/snippet/SnippetTemplate.h"
#include "core/text/Utf.h"

namespace lankey::core::snippet {
namespace {

// 2026-09-28 14:05:09, as broken-down local time. Tests pass the time in rather than
// reading the clock, so they say the same thing in every timezone.
std::tm sampleTime() {
    std::tm t{};
    t.tm_year = 126; // 1900 + 126
    t.tm_mon = 8;    // September
    t.tm_mday = 28;
    t.tm_hour = 14;
    t.tm_min = 5;
    t.tm_sec = 9;
    return t;
}

RenderContext context(std::u32string clipboard = {},
                      std::map<std::u32string, std::u32string> params = {},
                      std::map<std::u32string, std::u32string> variables = {}) {
    RenderContext ctx;
    ctx.now = sampleTime();
    ctx.clipboard = std::move(clipboard);
    ctx.params = std::move(params);
    ctx.variables = std::move(variables);
    return ctx;
}

std::u32string render(std::u32string_view body, const RenderContext& ctx) {
    return SnippetTemplate::parse(body).render(ctx).text;
}

TEST(SnippetTemplate, TextWithoutHolesIsItself) {
    EXPECT_EQ(render(U"Xin chào", context()), U"Xin chào");
    EXPECT_EQ(render(U"", context()), U"");
}

TEST(SnippetTemplate, DateAndTimeUseTheContextsClock) {
    EXPECT_EQ(render(U"{date}", context()), U"28/09/2026");
    EXPECT_EQ(render(U"{time}", context()), U"14:05");
}

TEST(SnippetTemplate, DateTakesAFormat) {
    EXPECT_EQ(render(U"{date:yyyy-MM-dd}", context()), U"2026-09-28");
    EXPECT_EQ(render(U"{date:dd MM yyyy}", context()), U"28 09 2026");
    EXPECT_EQ(render(U"{date:HH:mm:ss}", context()), U"14:05:09");
    // Anything that is not a format letter is copied through, so a Japanese date reads
    // the way a Japanese document expects.
    EXPECT_EQ(render(U"{date:yyyy年MM月dd日}", context()), U"2026年09月28日");
}

TEST(SnippetTemplate, ClipboardComesFromTheContext) {
    EXPECT_EQ(render(U"Xem: {clipboard}", context(U"https://example.com")),
              U"Xem: https://example.com");
    // Nothing copied yet is not an error; it is simply empty.
    EXPECT_EQ(render(U"Xem: {clipboard}", context()), U"Xem: ");
}

TEST(SnippetTemplate, CursorIsRemovedAndReportedAsAnOffsetFromTheEnd) {
    const auto r = SnippetTemplate::parse(U"Chào {cursor},\nTrân trọng").render(context());
    EXPECT_EQ(r.text, U"Chào ,\nTrân trọng");
    // Eleven characters follow the caret: ",\nTrân trọng".
    EXPECT_EQ(r.cursorOffsetFromEnd, 12);
}

TEST(SnippetTemplate, OnlyTheFirstCursorCounts) {
    // Two carets cannot both be where typing continues; the later one is left as written
    // so the user can see what they asked for instead of silently losing it.
    const auto r = SnippetTemplate::parse(U"a{cursor}b{cursor}c").render(context());
    EXPECT_EQ(r.text, U"ab{cursor}c");
    EXPECT_EQ(r.cursorOffsetFromEnd, 10);
}

TEST(SnippetTemplate, NoCursorMeansTheCaretStaysAtTheEnd) {
    EXPECT_EQ(SnippetTemplate::parse(U"xong").render(context()).cursorOffsetFromEnd, 0);
}

TEST(SnippetTemplate, ABuiltInHoleIsNeverShadowedByAVariableOfTheSameName) {
    // {date} means the date, always. A body may be copied from a colleague or a template,
    // and what it prints must not depend on what the reader happens to have called their
    // variables. The UI is what warns the user that such a variable can never fire.
    const auto vars = std::map<std::u32string, std::u32string>{
        {U"date", U"KHONG DUNG"}, {U"time", U"KHONG DUNG"}, {U"clipboard", U"KHONG DUNG"}};
    EXPECT_EQ(render(U"{date}", context({}, {}, vars)), U"28/09/2026");
    EXPECT_EQ(render(U"{time}", context({}, {}, vars)), U"14:05");
    EXPECT_EQ(render(U"{clipboard}", context(U"đã chép", {}, vars)), U"đã chép");
}

TEST(SnippetTemplate, ReservedNamesAreTheOnesTheTemplateActuallyTakes) {
    // The list the settings window warns on must be the list parse() really claims - the
    // two drifting apart is how a user ends up with a variable that silently never fires.
    for (const auto* name : {U"date", U"time", U"clipboard", U"cursor", U"param"}) {
        EXPECT_TRUE(SnippetTemplate::isReservedName(name)) << text::toUtf8(name);
    }
    EXPECT_FALSE(SnippetTemplate::isReservedName(U"ten"));
    EXPECT_FALSE(SnippetTemplate::isReservedName(U"Date")) << "names are matched exactly";
}

// --- parameters ------------------------------------------------------------------------------

TEST(SnippetTemplate, ParametersAreListedInTheOrderTheyAppear) {
    const auto t = SnippetTemplate::parse(U"{param:Khách hàng} — {param:Mã lỗi}");
    ASSERT_EQ(t.params().size(), 2u);
    EXPECT_EQ(t.params()[0], U"Khách hàng");
    EXPECT_EQ(t.params()[1], U"Mã lỗi");
}

TEST(SnippetTemplate, TheSameParameterTwiceIsAskedOnceAndFilledTwice) {
    const auto t = SnippetTemplate::parse(U"{param:Tên} ơi, chào {param:Tên}");
    ASSERT_EQ(t.params().size(), 1u);
    EXPECT_EQ(t.render(context({}, {{U"Tên", U"Phong"}})).text, U"Phong ơi, chào Phong");
}

TEST(SnippetTemplate, AParameterNobodyAnsweredBecomesNothing) {
    // Rendering without an answer must not leave "{param:Tên}" sitting in the message the
    // user is about to send.
    const auto t = SnippetTemplate::parse(U"Chào {param:Tên}");
    EXPECT_EQ(t.render(context()).text, U"Chào ");
}

TEST(SnippetTemplate, TheCaretLandsInTheFirstBlankNobodyFilled) {
    // The blank is where the user has to type next, so that is where they are put - no
    // dialog to answer first, no arrow keys to hunt back with.
    const auto r = SnippetTemplate::parse(U"Kính gửi {param:Tên},").render(context());
    EXPECT_EQ(r.text, U"Kính gửi ,");
    EXPECT_EQ(r.cursorOffsetFromEnd, 1) << "just before the comma";
}

TEST(SnippetTemplate, AnAnsweredParameterLeavesTheCaretAtTheEnd) {
    const auto r =
        SnippetTemplate::parse(U"Kính gửi {param:Tên},").render(context({}, {{U"Tên", U"Phong"}}));
    EXPECT_EQ(r.text, U"Kính gửi Phong,");
    EXPECT_EQ(r.cursorOffsetFromEnd, 0) << "nothing is left to fill in";
}

TEST(SnippetTemplate, OnlyTheFirstBlankTakesTheCaret) {
    const auto r = SnippetTemplate::parse(U"{param:A}-{param:B}").render(context());
    EXPECT_EQ(r.text, U"-");
    EXPECT_EQ(r.cursorOffsetFromEnd, 1) << "the first blank, not the last";
}

TEST(SnippetTemplate, AnExplicitCursorWinsOverABlank) {
    // The user pointed at a spot. A blank they chose to leave empty does not override it.
    const auto r = SnippetTemplate::parse(U"{param:A}bc{cursor}d").render(context());
    EXPECT_EQ(r.text, U"bcd");
    EXPECT_EQ(r.cursorOffsetFromEnd, 1);
}

// --- the user's own variables -------------------------------------------------------------

TEST(SnippetTemplate, UserVariablesComeFromTheContext) {
    EXPECT_EQ(render(U"{ten} — {cty}", context({}, {}, {{U"ten", U"Phong"}, {U"cty", U"LanKey"}})),
              U"Phong — LanKey");
}

TEST(SnippetTemplate, AVariableIsNotAParameter) {
    const auto t = SnippetTemplate::parse(U"{ten}");
    EXPECT_TRUE(t.params().empty()) << "a variable has a value already; nobody is asked for it";
}

// --- what is NOT a hole ---------------------------------------------------------------------

TEST(SnippetTemplate, AnUnknownNameIsLeftExactlyAsWritten) {
    // The body is the user's text. Swallowing something we do not recognise would delete
    // their words; showing it back is how they find the typo.
    EXPECT_EQ(render(U"{khong_biet}", context()), U"{khong_biet}");
    EXPECT_EQ(render(U"giá: {100}", context()), U"giá: {100}");
}

TEST(SnippetTemplate, AnUnclosedBraceIsLeftAlone) {
    EXPECT_EQ(render(U"a { b", context()), U"a { b");
    EXPECT_EQ(render(U"{date", context()), U"{date");
}

TEST(SnippetTemplate, ABraceRightBeforeAKnownHoleStillWorks) {
    EXPECT_EQ(render(U"{ {date}", context()), U"{ 28/09/2026");
}

} // namespace
} // namespace lankey::core::snippet
