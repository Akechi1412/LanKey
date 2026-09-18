#include "core/smart/correct/AutoCorrectEngine.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <string_view>

#include "core/model/Thresholds.h"
#include "core/text/VietnameseText.h"

namespace lankey::core::smart {

using model::AutoCorrectLevel;
using model::Correction;
using model::CorrectionSource;
using model::Syllable;
using model::SyllableCommitted;
using model::Thresholds;

namespace {

constexpr std::size_t kMaxRuleSyllables = 3;  // correction_map keys are 1..3 syllables
constexpr double kFrequencyBonusScaled = 2.0; // 0.4 in distance units per doubling of use
constexpr double kToneMismatchScaled = 2.0;   // candidate has a tone iff the typo has one
constexpr double kDominanceRatio = 1.5;       // trusted unless a neighbour is typed 1.5x more
constexpr double kScoreEpsilon = 1e-9;

// "ABC", "OK": constants and codes, never a Vietnamese typo.
bool isAllCaps(std::u32string_view typed) noexcept {
    if (typed.size() < 2) return false;
    bool anyLetter = false;
    for (const char32_t c : typed) {
        if (!text::isLetter(c)) continue;
        anyLetter = true;
        if (text::toLower(c) == c) return false;
    }
    return anyLetter;
}

// Joined folded text of the last `count` syllables into `out`.
void joinTail(const std::vector<Syllable>& window, std::size_t count, std::u32string& out) {
    out.clear();
    for (std::size_t i = window.size() - count; i < window.size(); ++i) {
        if (!out.empty()) out.push_back(U' ');
        out += window[i].text;
    }
}

std::string lowerAscii(std::string s) {
    for (auto& ch : s)
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return s;
}

} // namespace

std::shared_ptr<const BaseIndex> BaseIndex::build(const BaseSyllableSet& set) {
    auto index = std::make_shared<BaseIndex>();
    index->syllables = &set;
    set.forEach([&](std::u32string_view s) { index->tree.insert(s); });
    return index;
}

std::shared_ptr<const CorrectionSnapshot>
CorrectionSnapshot::build(const std::vector<model::LexiconEntry>& entries,
                          const std::vector<model::CorrectionRule>& rules,
                          const std::vector<std::u32string>& blacklist, const BaseSyllableSet& base,
                          std::int64_t nowUnixSeconds) {
    auto snap = std::make_shared<CorrectionSnapshot>();
    snap->rules.reserve(rules.size());
    for (const auto& r : rules) {
        snap->rules.emplace(r.wrong, r);
        if (r.timesRejected > 0 && r.confidence < Thresholds::kCorrectionApplyConfidence &&
            nowUnixSeconds - r.updatedAt < Thresholds::kRejectionCooldownSeconds) {
            snap->suppressed.insert(r.wrong);
        }
    }
    snap->blacklist.reserve(blacklist.size());
    for (const auto& b : blacklist)
        snap->blacklist.insert(b);
    for (const auto& e : entries) {
        if (e.syllableCount() != 1 || e.blocked) continue;
        const auto& s = e.phrase.syllables[0].text;
        snap->frequency.emplace(s, e.frequency);
        if (e.frequency < static_cast<std::uint32_t>(Thresholds::kTrustMinFrequency)) continue;
        if (base.contains(s)) continue; // already known; keep the personal tree small
        snap->trusted.insert(s);
        snap->personal.insert(s);
    }
    return snap;
}

AutoCorrectEngine::AutoCorrectEngine() {
    setSettings(model::AutoCorrectSettings{});
}

void AutoCorrectEngine::publishBase(std::shared_ptr<const BaseIndex> base) {
    base_.store(std::move(base));
}

void AutoCorrectEngine::publish(std::shared_ptr<const CorrectionSnapshot> snapshot) {
    snapshot_.store(std::move(snapshot));
}

void AutoCorrectEngine::setSettings(const model::AutoCorrectSettings& settings) {
    auto effective = std::make_shared<EffectiveSettings>();
    effective->settings = settings;
    for (const auto& app : settings.excludedApps)
        effective->excludedApps.insert(lowerAscii(app));
    settings_.store(std::move(effective));
}

int AutoCorrectEngine::maxDistanceScaled(AutoCorrectLevel level) noexcept {
    switch (level) {
    case AutoCorrectLevel::Off:
        return -1;
    case AutoCorrectLevel::Cautious:
        return 3; // 0.6: one tone slip, or one dialect consonant
    case AutoCorrectLevel::Balanced:
        return 5; // 1.0: one edit of any kind
    case AutoCorrectLevel::Aggressive:
        return 7; // 1.5
    }
    return -1;
}

std::optional<Correction> AutoCorrectEngine::check(const SyllableCommitted& committed) const {
    const auto effective = settings_.load();
    if (!effective) return std::nullopt;
    const auto* settings = &effective->settings;
    if (settings->level == AutoCorrectLevel::Off) return std::nullopt;
    const auto base = base_.load();
    if (!base) return std::nullopt;
    const auto& window = committed.window.committed;
    if (window.empty()) return std::nullopt;
    const Syllable& s = window.back();

    // 0. Preconditions. Enter/Tab: the text may already be gone (chat apps send on Enter).
    // Navigation/focus loss (terminator 0): nothing to anchor a retype on.
    const char32_t t = committed.terminator;
    if (t == 0 || t == U'\n' || t == U'\t') return std::nullopt;
    if (committed.focus.isPasswordField) return std::nullopt;
    if (!effective->excludedApps.empty() &&
        effective->excludedApps.contains(lowerAscii(committed.focus.appName))) {
        return std::nullopt;
    }
    if (s.text.empty() || !text::isAllLetters(s.text)) return std::nullopt;
    if (isAllCaps(s.typed)) return std::nullopt;

    const auto snap = snapshot_.load();
    static thread_local std::u32string key;
    const std::size_t maxContext = std::min(window.size(), kMaxRuleSyllables);

    // 1. Blacklist: the syllable or any phrase ending with it.
    if (snap) {
        for (std::size_t k = 1; k <= maxContext; ++k) {
            joinTail(window, k, key);
            if (snap->blacklist.contains(key)) return std::nullopt;
        }
        // 2. What the user taught us, longest context first. Consulted even for syllables
        // the engine left untouched: this is the one source that knows "sữa lỗi" is wrong
        // for this user although every syllable is valid.
        for (std::size_t k = maxContext; k >= 1; --k) {
            joinTail(window, k, key);
            const auto it = snap->rules.find(key);
            if (it == snap->rules.end()) continue;
            const auto& rule = it->second;
            if (rule.correct.empty() || rule.confidence < Thresholds::kCorrectionApplyConfidence) {
                continue;
            }
            auto corrected = model::Phrase::fromJoined(rule.correct);
            if (corrected.syllables.size() != k) continue; // a rule keeps the syllable count
            Correction c;
            c.syllableCount = static_cast<int>(k);
            c.corrected = std::move(corrected.syllables);
            c.wrongKey = key;
            c.expectedGeneration = committed.generation;
            c.source = CorrectionSource::CorrectionMap;
            c.confidence = rule.confidence;
            return c;
        }
    }

    // 3. No Vietnamese transform on this syllable: English, a name, a command. The base
    // dictionary is closed, so "with" and "git" would otherwise both be "typos".
    if (!committed.vietnameseTransformApplied) return std::nullopt;
    // 4. A real syllable.
    if (base->syllables->contains(s.text)) return std::nullopt;
    // 5. A guess the user rejected recently.
    if (snap && snap->suppressed.contains(s.text)) return std::nullopt;

    // 6. Fuzzy: one clear best candidate within the level's reach, or nothing.
    const int maxDistance = maxDistanceScaled(settings->level);
    if (maxDistance < 0) return std::nullopt;
    static thread_local std::vector<FuzzyIndex::Match> matches;
    static thread_local std::vector<FuzzyIndex::Match> personal;
    base->tree.search(s.text, maxDistance, matches);
    if (snap) {
        snap->personal.search(s.text, maxDistance, personal);
        matches.insert(matches.end(), personal.begin(), personal.end());
    }
    // The syllable itself may sit in the personal index (trusted): it is not a candidate.
    std::erase_if(matches, [](const FuzzyIndex::Match& m) { return m.scaledDistance == 0; });
    if (matches.empty()) return std::nullopt;

    // 5b. Something this user types often enough that we believe them - unless a word a
    // slip away is what they type far more often, which makes this the recurring typo.
    if (snap && snap->trusted.contains(s.text)) {
        const auto own = snap->frequency.find(s.text);
        const double ownFreq = own == snap->frequency.end() ? 0.0 : own->second;
        bool dominated = false;
        for (const auto& m : matches) {
            const auto f = snap->frequency.find(std::u32string(m.key));
            if (f != snap->frequency.end() && f->second >= kDominanceRatio * ownFreq) {
                dominated = true;
                break;
            }
        }
        if (!dominated) return std::nullopt;
    }
    // Rank by distance, adjusted by two things the plain metric does not know:
    //   - a bonus for words this user actually writes: -0.4 * log2(1 + frequency). Pure
    //     nearest-neighbour fails on the commonest Telex slip: "nguoif" gives "nguòi", 0.4
    //     from the rare nguồi/nguội and 0.8 from "người";
    //   - a penalty when the candidate has a tone and the typo has none, or vice versa:
    //     the user pressed a tone key (or did not) on purpose. "đưởng" is 0.4 from both
    //     "đường" and "đương"; only "đường" carries a tone like the typo does. Measured on
    //     the dictionary this lifts unique fixes from 72% to 77% with no wrong pick.
    // The winner must be unique (a tie is a coin flip we refuse to make).
    const bool typoHasTone = text::hasTone(s.text);
    const auto scoreOf = [&](const FuzzyIndex::Match& m) {
        double score = static_cast<double>(m.scaledDistance);
        if (text::hasTone(m.key) != typoHasTone) score += kToneMismatchScaled;
        if (snap) {
            const auto f = snap->frequency.find(std::u32string(m.key));
            if (f != snap->frequency.end() && f->second > 0) {
                score -= kFrequencyBonusScaled * std::log2(1.0 + static_cast<double>(f->second));
            }
        }
        return score;
    };
    const FuzzyIndex::Match* best = nullptr;
    double bestScore = 0.0;
    int bestCount = 0;
    for (const auto& m : matches) {
        const double score = scoreOf(m);
        if (best == nullptr || score < bestScore - kScoreEpsilon) {
            best = &m;
            bestScore = score;
            bestCount = 1;
        } else if (std::abs(score - bestScore) <= kScoreEpsilon) {
            ++bestCount;
        }
    }
    if (best == nullptr || bestCount != 1) return std::nullopt; // ambiguous: the user decides

    Correction c;
    c.syllableCount = 1;
    c.corrected.push_back(Syllable{std::u32string(best->key), {}});
    c.wrongKey = s.text;
    c.expectedGeneration = committed.generation;
    c.source = CorrectionSource::BaseDictionary;
    c.confidence = 1.0 - static_cast<double>(best->scaledDistance) /
                             static_cast<double>(Thresholds::kFuzzySearchRadiusScaled);
    return c;
}

} // namespace lankey::core::smart
