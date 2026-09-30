// DATA-POLICY.md is a promise to the user; this pins its concrete claims to the code that
// keeps them. When a threshold or a default changes, the document must change with it -
// the build stops until it does.

#include <fstream>
#include <sstream>
#include <string>

#include <gtest/gtest.h>

#include "core/clipboard/ClipboardStore.h"
#include "core/model/Settings.h"
#include "core/model/Thresholds.h"
#include "core/smart/privacy/PrivacyFilter.h"
#include "core/snippet/SnippetIndex.h"
#include "core/storage/JsonSettingsStore.h"

#ifndef LANKEY_SOURCE_DIR
#error "LANKEY_SOURCE_DIR must be defined by CMake"
#endif

namespace lankey::core {
namespace {

std::string policy() {
    std::ifstream in(std::string(LANKEY_SOURCE_DIR) + "/DATA-POLICY.md", std::ios::binary);
    std::stringstream buf;
    buf << in.rdbuf();
    return buf.str();
}

bool mentions(const std::string& text, const char* fragment) {
    return text.find(fragment) != std::string::npos;
}

// The one line of the "Dữ liệu nằm ở đâu" table that starts with `file`, so a claim about
// one file can be checked where it is made instead of anywhere in the document.
std::string storageRow(const char* file) {
    const std::string p = policy();
    const std::size_t at = p.find(std::string("| ") + file);
    if (at == std::string::npos) return {};
    const std::size_t end = p.find('\n', at);
    return p.substr(at, end == std::string::npos ? std::string::npos : end - at);
}

TEST(DataPolicy, ExistsAndIsVietnamese) {
    const std::string p = policy();
    ASSERT_FALSE(p.empty());
    EXPECT_TRUE(mentions(p, "# Dữ liệu của bạn"));
}

TEST(DataPolicy, PhraseLengthMatchesTheCode) {
    static_assert(model::Thresholds::kMaxPhraseSyllables == 5,
                  "update DATA-POLICY.md (\"tối đa 5 âm tiết\") together with this constant");
    EXPECT_TRUE(mentions(policy(), "tối đa 5 âm tiết"));
}

TEST(DataPolicy, RetentionLimitsMatchTheCode) {
    static_assert(model::Thresholds::kLexiconHardLimit == 100000,
                  "update DATA-POLICY.md (\"100 000\") together with this constant");
    const std::string p = policy();
    EXPECT_TRUE(mentions(p, "100 000"));
    EXPECT_TRUE(mentions(p, "45–180 ngày")); // SqliteLexiconStore::cleanup: 45..180 days
}

TEST(DataPolicy, StorageClaimsMatchTheCode) {
    const std::string p = policy();
    EXPECT_TRUE(mentions(p, "user_lexicon.enc"));
    EXPECT_TRUE(mentions(p, "DPAPI"));
    EXPECT_TRUE(mentions(p, "settings.json"));
    EXPECT_TRUE(mentions(p, "lankey.log"));
    EXPECT_TRUE(mentions(p, "%APPDATA%\\LanKey\\"));
}

TEST(DataPolicy, TheGlossaryAndSnippetsAreDeclaredWhereTheyActuallyLive) {
    // They moved out of dictionary.csv / snippets.json and into settings.json, which is
    // NOT encrypted - unlike the learned lexicon. The document has to say so, because the
    // user puts their own e-mail addresses and letter templates in there.
    model::Settings s;
    s.glossary.push_back({"vi", "en", "", ""});
    s.snippets.items.push_back({"abbr", "body", false});
    const std::string written = storage::JsonSettingsStore::serialize(s);
    ASSERT_TRUE(written.find("glossary") != std::string::npos);
    ASSERT_TRUE(written.find("snippets") != std::string::npos);

    // Checked against the settings.json ROW of the storage table, not against the document
    // as a whole: "gõ tắt" appears in several places, so a loose search would keep passing
    // after somebody deleted the claim that matters.
    const std::string row = storageRow("`settings.json`");
    ASSERT_FALSE(row.empty()) << "the storage table no longer has a settings.json row";
    EXPECT_TRUE(mentions(row, "gõ tắt")) << row;
    EXPECT_TRUE(mentions(row, "ừ điển riêng")) << row; // "Từ" or "từ"
    EXPECT_TRUE(mentions(row, "hông mã hoá")) << row;  // "Không" or "không"
}

TEST(DataPolicy, ClipboardWatchingIsDeclaredAndIsOptIn) {
    // Watching the clipboard is something the user's own snippets ask for; nothing here
    // may start doing it quietly.
    snippet::SnippetIndex plain;
    ASSERT_TRUE(plain.load(R"({"snippets": [{"abbr": "a", "body": "nothing here"}]})").has_value());
    EXPECT_FALSE(plain.usesClipboard()) << "nothing asked, so nothing is watched";

    snippet::SnippetIndex pasting;
    ASSERT_TRUE(
        pasting.load(R"({"snippets": [{"abbr": "a", "body": "-> {clipboard}"}]})").has_value());
    EXPECT_TRUE(pasting.usesClipboard());

    const std::string p = policy();
    EXPECT_TRUE(mentions(p, "{clipboard}")) << "the hole that turns the watching on";
    EXPECT_TRUE(mentions(p, "ExcludeClipboardContentFromMonitorProcessing"));
}

TEST(DataPolicy, ClipboardHistoryIsOffUntilAskedFor) {
    // The document leads with "tắt sẵn". If that default ever flips, the sentence becomes
    // a lie about the most sensitive thing this program can hold.
    EXPECT_FALSE(model::ClipboardSettings{}.enabled);
    const std::string p = policy();
    EXPECT_TRUE(mentions(p, "Lịch sử clipboard")) << "the section itself";
    EXPECT_TRUE(mentions(p, "tắt sẵn"));
}

TEST(DataPolicy, ClipboardLimitsMatchTheCode) {
    static_assert(model::Thresholds::kClipboardHistoryItems == 50,
                  "update DATA-POLICY.md (\"50 mục\") together with this constant");
    static_assert(model::Thresholds::kClipboardMaxItemChars == 8192,
                  "update DATA-POLICY.md (\"8192 ký tự\") together with this constant");
    const std::string p = policy();
    EXPECT_TRUE(mentions(p, "50 mục"));
    EXPECT_TRUE(mentions(p, "8192 ký tự"));
}

TEST(DataPolicy, OnlyPinnedClipboardItemsReachTheDiskAndTheySaySealed) {
    // The row in the storage table is where the claim is made, so that is where it is
    // checked - the word "clipboard.enc" appears elsewhere in the prose too.
    clipboard::ClipboardStore store;
    store.add(U"không ghim", 1);
    store.add(U"có ghim", 2);
    ASSERT_TRUE(store.setPinned(0, true)); // the newest, "có ghim"
    const std::string written = store.serializePinned();
    EXPECT_TRUE(written.find("kh\\u00f4ng ghim") == std::string::npos &&
                written.find("không ghim") == std::string::npos)
        << "an unpinned item must never be written: " << written;

    const std::string row = storageRow("`clipboard.enc`");
    ASSERT_FALSE(row.empty()) << "the storage table has no clipboard.enc row";
    EXPECT_TRUE(mentions(row, "ghim")) << row;
    EXPECT_TRUE(mentions(row, "DPAPI")) << row;
}

TEST(DataPolicy, ExcludedAppsNamedInThePolicyAreInTheDefaults) {
    const std::string p = policy();
    const auto defaults = smart::AppExclusionRule::defaults();
    const auto has = [&](const char* exe) {
        for (const auto& d : defaults) {
            if (d == exe) return true;
        }
        return false;
    };
    // The policy names these product families; the code must exclude them by default.
    EXPECT_TRUE(mentions(p, "KeePass") && has("keepass.exe"));
    EXPECT_TRUE(mentions(p, "1Password") && has("1password.exe"));
    EXPECT_TRUE(mentions(p, "Bitwarden") && has("bitwarden.exe"));
    EXPECT_TRUE(mentions(p, "Remote Desktop") && has("mstsc.exe"));
    EXPECT_TRUE(mentions(p, "PowerShell") && has("powershell.exe"));
    EXPECT_TRUE(mentions(p, "PuTTY") && has("putty.exe"));
}

TEST(DataPolicy, ContentRulesNamedInThePolicyExist) {
    const std::string p = policy();
    EXPECT_TRUE(mentions(p, "e-mail"));
    EXPECT_TRUE(mentions(p, "CMND/CCCD"));
    EXPECT_TRUE(mentions(p, "20 ký tự"));
    // The rule behind those sentences.
    EXPECT_TRUE(smart::ContentHeuristicRule::looksSensitive(U"user@example.com"));
    EXPECT_TRUE(smart::ContentHeuristicRule::looksSensitive(U"079123456789"));
    EXPECT_TRUE(smart::ContentHeuristicRule::looksSensitive(U"abcdefghijklmnopqrstu"));
}

TEST(DataPolicy, AutoCorrectExclusionsDefaultToNoneAsTheDocumentImplies) {
    // The policy says only the user's own additions matter for autocorrect exclusion.
    EXPECT_TRUE(model::AutoCorrectSettings{}.excludedApps.empty());
}

} // namespace
} // namespace lankey::core
