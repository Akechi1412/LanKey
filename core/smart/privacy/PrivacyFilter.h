#pragma once

#include <memory>
#include <string>
#include <vector>

#include "core/interfaces/IPrivacyRule.h"
#include "core/model/Settings.h"

namespace lankey::core::smart {

// Layer 1: never learn or correct inside password managers, terminals, remote desktops and
// whatever the user adds. Executable names are compared case-insensitively.
class AppExclusionRule final : public IPrivacyRule {
public:
    explicit AppExclusionRule(std::vector<std::string> excludedApps);
    [[nodiscard]] PrivacyVerdict evaluate(const model::SyllableCommitted& c) const override;

    // The built-in list; user additions from PrivacySettings are appended by the caller.
    [[nodiscard]] static std::vector<std::string> defaults();

private:
    std::vector<std::string> excluded_; // lowercase
};

// Layer 2: nothing typed into a password field is ever recorded.
class PasswordFieldRule final : public IPrivacyRule {
public:
    [[nodiscard]] PrivacyVerdict evaluate(const model::SyllableCommitted& c) const override;
};

// Layer 3: shapes that are never prose, judged on the on-screen tokens (syllables glued
// back together across "@", ".", "/", "-" separators): e-mail addresses, anything with
// 3+ digits next to symbols, 13-19 digit runs (cards), 9/12 digits (ID numbers), tokens of
// 20+ characters, and high-entropy strings (> 3.5 bits per character, random passwords
// and keys). PLAN 8.2.
class ContentHeuristicRule final : public IPrivacyRule {
public:
    [[nodiscard]] PrivacyVerdict evaluate(const model::SyllableCommitted& c) const override;

    // One token as it stands on screen. Exposed for tests.
    [[nodiscard]] static bool looksSensitive(std::u32string_view token);
};

// Layer 4: only strings made purely of letters are learnable. Digits, punctuation and
// symbols inside a syllable mean codes, identifiers, passwords - never Vietnamese words.
// Cheapest rule and the one that removes most risk; on from the first day of dogfooding.
class LettersOnlyRule final : public IPrivacyRule {
public:
    [[nodiscard]] PrivacyVerdict evaluate(const model::SyllableCommitted& c) const override;
};

// Runs the rules in order and returns the most restrictive verdict encountered
// (Reject > NoAutoCorrect > Accept). Adding a rule never means editing another.
//
// Threading: worker thread. Immutable after construction; rebuilt when settings change.
class PrivacyFilter {
public:
    explicit PrivacyFilter(std::vector<std::unique_ptr<IPrivacyRule>> rules);

    // The v0.1 stack: AppExclusion (defaults + settings), PasswordField, LettersOnly.
    [[nodiscard]] static PrivacyFilter standard(const model::PrivacySettings& settings);

    [[nodiscard]] PrivacyVerdict evaluate(const model::SyllableCommitted& c) const;

private:
    std::vector<std::unique_ptr<IPrivacyRule>> rules_;
};

} // namespace lankey::core::smart
