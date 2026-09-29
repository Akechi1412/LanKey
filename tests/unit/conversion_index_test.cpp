// The user's own VI-EN-JA glossary (Phase 5 B).
//
// This is not a translation dictionary: every row was typed by the person using it, which
// is why it may be edited in place, shared, and trusted enough to put its text on screen.
// The tests are written against that reading - a row is data the user vouched for.

#include <string>

#include <gtest/gtest.h>

#include "core/convert/ConversionIndex.h"

namespace lankey::core::convert {
namespace {

TEST(ConversionIndex, BuildsFromTheRowsKeptInSettings) {
    ConversionIndex index;
    index.load(std::vector<model::GlossaryEntry>{{"đăng nhập", "login", "ログイン", ""},
                                                 {"chỉ một cột", "", "", "chưa xong"}});
    EXPECT_EQ(index.size(), 1u) << "a row with one column has nothing to convert between";
    const auto* e = index.find(model::Language::English, U"login");
    ASSERT_NE(e, nullptr);
    EXPECT_EQ(e->vi, U"đăng nhập");
}

using model::Language;

ConversionIndex indexOf(const std::string& csv) {
    ConversionIndex index;
    const auto r = index.load(csv);
    EXPECT_TRUE(r.has_value()) << (r ? "" : r.error().message);
    return index;
}

constexpr const char* kSample = "vi,en,ja,note\n"
                                "đăng nhập,login,ログイン,\n"
                                "cơ sở dữ liệu,database,データベース,\n"
                                "lỗi,bug,バグ,\n"
                                "mã nguồn mở,open source,,\n"
                                "kiểm thử,test,,chưa có tiếng Nhật\n";

TEST(ConversionIndex, ReadsTheHeaderAndTheRows) {
    const auto index = indexOf(kSample);
    EXPECT_EQ(index.size(), 5u);
    EXPECT_EQ(index.maxSyllables(), 3); // "cơ sở dữ liệu" is four, clamped to the limit
}

TEST(ConversionIndex, LooksUpByEitherSourceLanguage) {
    const auto index = indexOf(kSample);
    const auto* byVi = index.find(Language::Vietnamese, U"đăng nhập");
    ASSERT_NE(byVi, nullptr);
    EXPECT_EQ(byVi->en, U"login");
    const auto* byEn = index.find(Language::English, U"login");
    ASSERT_NE(byEn, nullptr);
    EXPECT_EQ(byEn->vi, U"đăng nhập");
    const auto* byJa = index.find(Language::Japanese, U"ログイン");
    ASSERT_NE(byJa, nullptr);
    EXPECT_EQ(byJa->vi, U"đăng nhập");
}

TEST(ConversionIndex, KeysIgnoreCaseButKeepDiacritics) {
    const auto index = indexOf(kSample);
    EXPECT_NE(index.find(Language::English, U"LOGIN"), nullptr);
    EXPECT_NE(index.find(Language::Vietnamese, U"Đăng Nhập"), nullptr);
    // "dang nhap" is a different word; stripping diacritics would make every toneless
    // typo match something.
    EXPECT_EQ(index.find(Language::Vietnamese, U"dang nhap"), nullptr);
}

TEST(ConversionIndex, TranslatesToTheLanguageThatWasAskedFor) {
    // One press, one answer. No cycling: asking for Japanese gives Japanese whether the
    // selection is Vietnamese or English.
    const auto index = indexOf(kSample);
    EXPECT_EQ(index.translate(U"đăng nhập", Language::English).text, U"login");
    EXPECT_EQ(index.translate(U"đăng nhập", Language::Japanese).text, U"ログイン");
    EXPECT_EQ(index.translate(U"login", Language::Japanese).text, U"ログイン");
    EXPECT_EQ(index.translate(U"ログイン", Language::English).text, U"login");
    EXPECT_EQ(index.translate(U"ログイン", Language::Vietnamese).text, U"đăng nhập");
}

TEST(ConversionIndex, TranslateSaysWhichLanguageItProduced) {
    // The caller labels what it just put on screen, so the column is part of the answer.
    const auto index = indexOf(kSample);
    EXPECT_EQ(index.translate(U"đăng nhập", Language::Japanese).language, Language::Japanese);
}

TEST(ConversionIndex, TranslateFindsNothingWhenThereIsNothingToFind) {
    const auto index = indexOf(kSample);
    // A word the glossary has never heard of.
    EXPECT_FALSE(index.translate(U"không có trong từ điển", Language::English).found());
    // A row that leaves that column empty: "kiểm thử" has no Japanese. Saying so is the
    // point - the alternative is a hotkey that appears to do nothing.
    EXPECT_FALSE(index.translate(U"kiểm thử", Language::Japanese).found());
    // Already the language that was asked for.
    EXPECT_FALSE(index.translate(U"login", Language::English).found());
}

TEST(ConversionIndex, MatchesTheLongestRunOfTrailingSyllables) {
    const auto index = indexOf(kSample);
    // The window ends with "mã nguồn mở"; the one-syllable "lỗi" earlier must not win.
    const std::vector<std::u32string> window = {U"lỗi", U"mã", U"nguồn", U"mở"};
    const auto m = index.lookupTyped(Language::Vietnamese, window);
    ASSERT_NE(m.entry, nullptr);
    EXPECT_EQ(m.entry->en, U"open source");
    EXPECT_EQ(m.syllablesMatched, 3);
}

TEST(ConversionIndex, ARowLongerThanTheTypingLimitIsStillReachableBySelection) {
    // "cơ sở dữ liệu" is four syllables. lookupTyped stops at kMaxSyllables, so it is
    // never offered while typing - but the convert-selection hotkey works on whatever the
    // user highlighted and has no such limit. A deliberate gap, not an oversight.
    const auto index = indexOf(kSample);
    const std::vector<std::u32string> window = {U"cơ", U"sở", U"dữ", U"liệu"};
    EXPECT_EQ(index.lookupTyped(Language::Vietnamese, window).entry, nullptr);
    EXPECT_EQ(index.translate(U"cơ sở dữ liệu", Language::English).text, U"database");
}

TEST(ConversionIndex, MatchesASingleSyllableWhenNothingLongerFits) {
    const auto index = indexOf(kSample);
    const std::vector<std::u32string> window = {U"sửa", U"lỗi"};
    const auto m = index.lookupTyped(Language::Vietnamese, window);
    ASSERT_NE(m.entry, nullptr);
    EXPECT_EQ(m.entry->en, U"bug");
    EXPECT_EQ(m.syllablesMatched, 1);
}

TEST(ConversionIndex, NoMatchLeavesTheWindowAlone) {
    const auto index = indexOf(kSample);
    const std::vector<std::u32string> window = {U"hôm", U"nay"};
    EXPECT_EQ(index.lookupTyped(Language::Vietnamese, window).entry, nullptr);
}

// --- the file the user edits by hand ------------------------------------------------------

TEST(ConversionIndex, AcceptsAByteOrderMarkAndCrLf) {
    const auto index = indexOf("\xEF\xBB\xBFvi,en,ja,note\r\nlỗi,bug,バグ,\r\n");
    EXPECT_EQ(index.size(), 1u);
    EXPECT_NE(index.find(Language::Vietnamese, U"lỗi"), nullptr);
}

TEST(ConversionIndex, ReadsQuotedFieldsWithCommasAndQuotes) {
    const auto index = indexOf("vi,en,ja,note\n"
                               "\"máy chủ, máy khách\",\"client/server\",,\"ghi \"\"chú\"\"\"\n");
    ASSERT_EQ(index.size(), 1u);
    const auto* e = index.find(Language::Vietnamese, U"máy chủ, máy khách");
    ASSERT_NE(e, nullptr);
    EXPECT_EQ(e->en, U"client/server");
    EXPECT_EQ(e->note, U"ghi \"chú\"");
}

TEST(ConversionIndex, SkipsBlankLinesAndRowsWithNothingToConvert) {
    const auto index = indexOf("vi,en,ja,note\n"
                               "\n"
                               ",,,chỉ có ghi chú\n"
                               "lỗi,bug,,\n"
                               "   ,  ,  ,\n");
    EXPECT_EQ(index.size(), 1u);
}

TEST(ConversionIndex, TheLastRowWinsWhenAKeyRepeats) {
    const auto index = indexOf("vi,en,ja,note\n"
                               "lỗi,bug,,\n"
                               "lỗi,defect,,\n");
    const auto* e = index.find(Language::Vietnamese, U"lỗi");
    ASSERT_NE(e, nullptr);
    EXPECT_EQ(e->en, U"defect");
}

TEST(ConversionIndex, AFileWithoutTheExpectedHeaderIsRefused) {
    ConversionIndex index;
    const auto r = index.load("tiếng việt;tiếng anh\nlỗi;bug\n");
    EXPECT_FALSE(r.has_value());
}

TEST(ConversionIndex, AnEmptyFileIsAnEmptyDictionary) {
    ConversionIndex index;
    EXPECT_TRUE(index.load("vi,en,ja,note\n").has_value());
    EXPECT_EQ(index.size(), 0u);
    EXPECT_FALSE(index.translate(U"lỗi", Language::English).found());
}

} // namespace
} // namespace lankey::core::convert
