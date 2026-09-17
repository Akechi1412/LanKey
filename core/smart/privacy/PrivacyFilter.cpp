#include "core/smart/privacy/PrivacyFilter.h"

#include <algorithm>
#include <cctype>

#include "core/text/VietnameseText.h"

namespace lankey::core::smart {

namespace {

std::string asciiLower(std::string s) {
    for (auto& ch : s) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return s;
}

} // namespace

// -- AppExclusionRule ---------------------------------------------------------------------

std::vector<std::string> AppExclusionRule::defaults() {
    return {
        // password managers
        "keepass.exe",
        "keepassxc.exe",
        "1password.exe",
        "bitwarden.exe",
        "dashlane.exe",
        // remote sessions: keystrokes belong to another machine
        "mstsc.exe",
        "vmconnect.exe",
        // terminals: commands, tokens, passwords typed blind
        "putty.exe",
        "ssh.exe",
        "windowsterminal.exe",
        "cmd.exe",
        "powershell.exe",
        "pwsh.exe",
    };
}

AppExclusionRule::AppExclusionRule(std::vector<std::string> excludedApps) {
    excluded_.reserve(excludedApps.size());
    for (auto& app : excludedApps) {
        excluded_.push_back(asciiLower(std::move(app)));
    }
}

PrivacyVerdict AppExclusionRule::evaluate(const model::SyllableCommitted& c) const {
    const std::string app = asciiLower(c.focus.appName);
    if (std::ranges::find(excluded_, app) != excluded_.end()) {
        return PrivacyVerdict::Reject;
    }
    return PrivacyVerdict::Accept;
}

// -- PasswordFieldRule --------------------------------------------------------------------

PrivacyVerdict PasswordFieldRule::evaluate(const model::SyllableCommitted& c) const {
    return c.focus.isPasswordField ? PrivacyVerdict::Reject : PrivacyVerdict::Accept;
}

// -- LettersOnlyRule ----------------------------------------------------------------------

PrivacyVerdict LettersOnlyRule::evaluate(const model::SyllableCommitted& c) const {
    // Every syllable in the window matters: the phrases built from this event span all of
    // them, so one "abc123" anywhere poisons every candidate.
    for (const auto& s : c.window.committed) {
        if (!text::isAllLetters(s.text)) return PrivacyVerdict::Reject;
    }
    return PrivacyVerdict::Accept;
}

// -- PrivacyFilter ------------------------------------------------------------------------

PrivacyFilter::PrivacyFilter(std::vector<std::unique_ptr<IPrivacyRule>> rules)
    : rules_(std::move(rules)) {}

PrivacyFilter PrivacyFilter::standard(const model::PrivacySettings& settings) {
    auto apps = AppExclusionRule::defaults();
    apps.insert(apps.end(), settings.excludedApps.begin(), settings.excludedApps.end());

    std::vector<std::unique_ptr<IPrivacyRule>> rules;
    rules.push_back(std::make_unique<AppExclusionRule>(std::move(apps)));
    rules.push_back(std::make_unique<PasswordFieldRule>());
    rules.push_back(std::make_unique<LettersOnlyRule>());
    return PrivacyFilter(std::move(rules));
}

PrivacyVerdict PrivacyFilter::evaluate(const model::SyllableCommitted& c) const {
    PrivacyVerdict worst = PrivacyVerdict::Accept;
    for (const auto& rule : rules_) {
        const PrivacyVerdict v = rule->evaluate(c);
        if (v == PrivacyVerdict::Reject) return v; // nothing can override a rejection
        if (v == PrivacyVerdict::NoAutoCorrect) worst = v;
    }
    return worst;
}

} // namespace lankey::core::smart
