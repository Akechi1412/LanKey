// DATA-POLICY.md is a promise to the user; this pins its concrete claims to the code that
// keeps them. When a threshold or a default changes, the document must change with it -
// the build stops until it does.

#include <fstream>
#include <sstream>
#include <string>

#include <gtest/gtest.h>

#include "core/model/Settings.h"
#include "core/model/Thresholds.h"
#include "core/smart/privacy/PrivacyFilter.h"

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
