#include "core/smart/privacy/PrivacyFilter.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>

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

// -- ContentHeuristicRule ----------------------------------------------------------------------

bool ContentHeuristicRule::looksSensitive(std::u32string_view token) {
    if (token.empty()) return false;
    int digits = 0;
    int longestDigitRun = 0;
    int run = 0;
    bool symbol = false;
    bool at = false;
    bool dot = false;
    for (const char32_t c : token) {
        const bool digit = c >= U'0' && c <= U'9';
        digits += digit ? 1 : 0;
        run = digit ? run + 1 : 0;
        longestDigitRun = std::max(longestDigitRun, run);
        if (c == U'@') at = true;
        if (c == U'.') dot = true;
        if (!digit && !text::isLetter(c) && c != U' ') symbol = true;
    }
    if (at && dot) return true;                                      // e-mail
    if (token.size() >= 20) return true;                             // key, hash, URL
    if (longestDigitRun >= 13 && longestDigitRun <= 19) return true; // card number
    if (longestDigitRun == 9 || longestDigitRun == 12) return true;  // CMND / CCCD
    if (digits >= 3 && symbol) return true;                          // "P@ss-w0rd1"
    if (token.size() >= 8) {
        // Shannon entropy in bits per character over the token's own alphabet.
        std::map<char32_t, int> counts;
        for (const char32_t c : token)
            ++counts[c];
        double entropy = 0.0;
        const auto n = static_cast<double>(token.size());
        for (const auto& [c, k] : counts) {
            const double p = static_cast<double>(k) / n;
            entropy -= p * std::log2(p);
        }
        if (entropy > 3.5) return true;
    }
    return false;
}

PrivacyVerdict ContentHeuristicRule::evaluate(const model::SyllableCommitted& c) const {
    // Rebuild the on-screen tokens: syllables joined by non-space separators are one.
    const auto& window = c.window.committed;
    std::u32string token;
    for (std::size_t i = 0; i < window.size(); ++i) {
        token += window[i].typed.empty() ? window[i].text : window[i].typed;
        const bool last = i + 1 == window.size();
        const std::u32string_view sep =
            !last && i < c.separators.size() ? std::u32string_view(c.separators[i]) : U" ";
        const bool glued = !last && !sep.empty() &&
                           std::ranges::none_of(sep, [](char32_t ch) { return ch == U' '; });
        if (glued) {
            token += sep;
            continue;
        }
        if (looksSensitive(token)) return PrivacyVerdict::Reject;
        token.clear();
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
    rules.push_back(std::make_unique<ContentHeuristicRule>());
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
