#include <gtest/gtest.h>

#include "core/smart/privacy/PrivacyFilter.h"

namespace lankey::core::smart {
namespace {

using model::Syllable;
using model::SyllableCommitted;

SyllableCommitted event(std::initializer_list<const char32_t*> syllables,
                        std::string app = "notepad.exe", bool password = false) {
    SyllableCommitted c;
    for (const auto* s : syllables)
        c.window.commit(Syllable::fromComposed(s));
    c.focus.appName = std::move(app);
    c.focus.isPasswordField = password;
    return c;
}

TEST(AppExclusionRule, RejectsDefaultsCaseInsensitively) {
    const AppExclusionRule rule(AppExclusionRule::defaults());
    EXPECT_EQ(rule.evaluate(event({U"abc"}, "KeePass.exe")), PrivacyVerdict::Reject);
    EXPECT_EQ(rule.evaluate(event({U"abc"}, "POWERSHELL.EXE")), PrivacyVerdict::Reject);
    EXPECT_EQ(rule.evaluate(event({U"abc"}, "winword.exe")), PrivacyVerdict::Accept);
    EXPECT_EQ(rule.evaluate(event({U"abc"}, "")), PrivacyVerdict::Accept);
}

TEST(AppExclusionRule, UserListIsHonoured) {
    const AppExclusionRule rule({"MyBank.exe"});
    EXPECT_EQ(rule.evaluate(event({U"abc"}, "mybank.exe")), PrivacyVerdict::Reject);
}

TEST(PasswordFieldRule, RejectsPasswordFields) {
    const PasswordFieldRule rule;
    EXPECT_EQ(rule.evaluate(event({U"abc"}, "chrome.exe", true)), PrivacyVerdict::Reject);
    EXPECT_EQ(rule.evaluate(event({U"abc"}, "chrome.exe", false)), PrivacyVerdict::Accept);
}

TEST(LettersOnlyRule, AcceptsVietnameseRejectsAnythingElse) {
    const LettersOnlyRule rule;
    EXPECT_EQ(rule.evaluate(event({U"chương", U"trình"})), PrivacyVerdict::Accept);
    EXPECT_EQ(rule.evaluate(event({U"P@ssw0rd123"})), PrivacyVerdict::Reject);
    EXPECT_EQ(rule.evaluate(event({U"4111111111111111"})), PrivacyVerdict::Reject);
    EXPECT_EQ(rule.evaluate(event({U"user", U"abc123"})), PrivacyVerdict::Reject);
    // One bad syllable anywhere in the window poisons every phrase built from it.
    EXPECT_EQ(rule.evaluate(event({U"x1", U"chương"})), PrivacyVerdict::Reject);
}

TEST(ContentHeuristicRule, TokenShapesThatAreNeverProse) {
    using R = ContentHeuristicRule;
    EXPECT_FALSE(R::looksSensitive(U"chương"));
    EXPECT_FALSE(R::looksSensitive(U"hello"));
    EXPECT_TRUE(R::looksSensitive(U"internationalization")); // 20 chars: a key more often
    EXPECT_TRUE(R::looksSensitive(U"user@example.com"));
    EXPECT_TRUE(R::looksSensitive(U"4111111111111111"));            // 16 digits
    EXPECT_TRUE(R::looksSensitive(U"079123456789"));                // 12-digit CCCD
    EXPECT_TRUE(R::looksSensitive(U"123456789"));                   // 9-digit CMND
    EXPECT_TRUE(R::looksSensitive(U"P@ss-w0rd12"));                 // digits + symbols
    EXPECT_TRUE(R::looksSensitive(U"xK9#mQ2$vL7pR4wZ"));            // high entropy
    EXPECT_TRUE(R::looksSensitive(U"https://github.com/lankey/x")); // >= 20 chars
    EXPECT_FALSE(R::looksSensitive(U"2024"));                       // a year on its own
}

TEST(ContentHeuristicRule, GluesSplitTokensBackTogether) {
    // "user@example.com" reaches the worker as three syllables with "@" and "." between.
    ContentHeuristicRule rule;
    auto e = event({U"user", U"example", U"com"});
    e.separators = {U"@", U"."};
    EXPECT_EQ(rule.evaluate(e), PrivacyVerdict::Reject);
    // The same syllables as words in a sentence are fine.
    e.separators = {U" ", U" "};
    EXPECT_EQ(rule.evaluate(e), PrivacyVerdict::Accept);
    // No separators known: treated as words.
    EXPECT_EQ(rule.evaluate(event({U"user", U"example", U"com"})), PrivacyVerdict::Accept);
}

TEST(PrivacyFilter, StandardStackCombinesRules) {
    model::PrivacySettings settings;
    settings.excludedApps = {"secret.exe"};
    const auto filter = PrivacyFilter::standard(settings);
    EXPECT_EQ(filter.evaluate(event({U"chương"})), PrivacyVerdict::Accept);
    EXPECT_EQ(filter.evaluate(event({U"chương"}, "secret.exe")), PrivacyVerdict::Reject);
    EXPECT_EQ(filter.evaluate(event({U"chương"}, "cmd.exe")), PrivacyVerdict::Reject);
    EXPECT_EQ(filter.evaluate(event({U"chương"}, "notepad.exe", true)), PrivacyVerdict::Reject);
    EXPECT_EQ(filter.evaluate(event({U"ch1"})), PrivacyVerdict::Reject);
    auto mail = event({U"me", U"mail", U"vn"});
    mail.separators = {U"@", U"."};
    EXPECT_EQ(filter.evaluate(mail), PrivacyVerdict::Reject);
}

TEST(PrivacyFilter, RejectWinsOverNoAutoCorrect) {
    struct NoCorrect : IPrivacyRule {
        PrivacyVerdict evaluate(const SyllableCommitted&) const override {
            return PrivacyVerdict::NoAutoCorrect;
        }
    };
    struct Rejecter : IPrivacyRule {
        PrivacyVerdict evaluate(const SyllableCommitted&) const override {
            return PrivacyVerdict::Reject;
        }
    };
    std::vector<std::unique_ptr<IPrivacyRule>> rules;
    rules.push_back(std::make_unique<NoCorrect>());
    const PrivacyFilter onlyNoCorrect(std::move(rules));
    EXPECT_EQ(onlyNoCorrect.evaluate(event({U"a"})), PrivacyVerdict::NoAutoCorrect);

    std::vector<std::unique_ptr<IPrivacyRule>> rules2;
    rules2.push_back(std::make_unique<NoCorrect>());
    rules2.push_back(std::make_unique<Rejecter>());
    const PrivacyFilter both(std::move(rules2));
    EXPECT_EQ(both.evaluate(event({U"a"})), PrivacyVerdict::Reject);
}

} // namespace
} // namespace lankey::core::smart
