// Keystroke-level AutoCorrect evaluation.
//
// autocorrect_eval_test.cpp generates typos in COMPOSED text ("đườngg") and hands them
// straight to AutoCorrectEngine::check(). That measures the ranking, but not the path a
// real slip takes: a typist's finger slips on a KEY, the engine composes whatever that
// sequence means, and only what survives the engine reaches the smart layer. Measured
// 2026-09-25, the two differ enough that the composed-text numbers overstate what a user
// gets - "doubled letter" scored 76.8% there while the doubled KEY "khoongg" was not
// fixed at all.
//
// So this file starts from the key sequence, runs it through the real engine + pipeline +
// AutoCorrect, and compares what ends up on screen with the word the typist meant.
//
// Reported per error class and level:
//   fixed      the screen shows the intended word
//   harmful    the screen shows some other Vietnamese word (worse than doing nothing)
//   untouched  the slip is still on screen, uncorrected
//
// Prints numbers; asserts only the things that must never regress.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <map>
#include <optional>
#include <random>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "core/model/EngineSettings.h"
#include "core/smart/correct/AutoCorrectEngine.h"
#include "core/smart/correct/BaseSyllableSet.h"
#include "core/smart/correct/FuzzyIndex.h"
#include "core/text/Utf.h"
#include "core/text/VietnameseDistance.h"
#include "core/text/VietnameseText.h"

#include "tests/replay/ReplayHarness.h"
#include "tests/support/Typist.h"
#include "tests/unit/engine_registry.h"

namespace lankey::tests {
namespace {

using core::smart::BaseSyllableSet;

// --- Telex encoder ------------------------------------------------------------------------

// Toned letter -> (the letter without its tone, the Telex key that adds that tone).
// Built by composing every modifier letter with every tone mark, so the table is exactly
// what this build of the text layer produces - no second hand-written list to drift.
struct ToneSplit {
    char32_t base = 0;
    char key = 0;
};

const std::map<char32_t, ToneSplit>& toneTable() {
    static const std::map<char32_t, ToneSplit> table = [] {
        static constexpr char32_t kMarks[] = {0x0301, 0x0300, 0x0309, 0x0303, 0x0323};
        static constexpr char kKeys[] = {'s', 'f', 'r', 'x', 'j'};
        std::map<char32_t, ToneSplit> out;
        for (const char32_t m : std::u32string(U"a\u0103\u00e2e\u00eaio\u00f4\u01a1u\u01b0y")) {
            for (int i = 0; i < 5; ++i) {
                const std::u32string composed = core::text::nfc(std::u32string{m, kMarks[i]});
                if (composed.size() == 1) out[composed[0]] = {m, kKeys[i]};
            }
        }
        return out;
    }();
    return table;
}

// The tone key for a letter, or 0 when it carries no tone.
char toneKey(char32_t c) {
    const auto it = toneTable().find(c);
    return it == toneTable().end() ? char{0} : it->second.key;
}

// The letter with its tone removed but its hat/horn/breve kept ("\u1edd" -> "\u01a1").
char32_t withoutTone(char32_t c) {
    const auto it = toneTable().find(c);
    return it == toneTable().end() ? c : it->second.base;
}

// The Telex keys that produce one toneless letter. "ư" -> "uw", "ê" -> "ee", "đ" -> "dd".
std::optional<std::string> letterKeys(char32_t c) {
    switch (c) {
    case U'ă':
        return "aw";
    case U'â':
        return "aa";
    case U'ê':
        return "ee";
    case U'ô':
        return "oo";
    case U'ơ':
        return "ow";
    case U'ư':
        return "uw";
    case U'đ':
        return "dd";
    default:
        break;
    }
    if (c >= U'a' && c <= U'z') return std::string(1, static_cast<char>(c));
    return std::nullopt;
}

// The whole key sequence for a syllable: every letter in order, the tone key last (Telex
// accepts it anywhere after the vowel; the end is what most people type).
std::optional<std::string> telexKeys(std::u32string_view syllable) {
    std::string keys;
    char tone = 0;
    for (const char32_t c : syllable) {
        const char t = toneKey(c);
        if (t != 0) {
            if (tone != 0 && tone != t) return std::nullopt; // two tones: not a syllable
            tone = t;
        }
        const auto k = letterKeys(withoutTone(c));
        if (!k) return std::nullopt;
        keys += *k;
    }
    if (keys.empty()) return std::nullopt;
    if (tone != 0) keys.push_back(tone);
    return keys;
}

// --- keystroke slips ------------------------------------------------------------------------

// Physical neighbours on a QWERTY keyboard - the slips a finger actually makes.
const std::map<char, std::string>& neighbours() {
    static const std::map<char, std::string> map = {
        {'q', "wa"},   {'w', "qes"},  {'e', "wrd"},  {'r', "etf"},  {'t', "ryg"},  {'y', "tuh"},
        {'u', "yij"},  {'i', "uok"},  {'o', "ipl"},  {'p', "ol"},   {'a', "qsz"},  {'s', "awdz"},
        {'d', "sefc"}, {'f', "drgv"}, {'g', "fthb"}, {'h', "gyjn"}, {'j', "hukm"}, {'k', "jil"},
        {'l', "kop"},  {'z', "asx"},  {'x', "zsdc"}, {'c', "xdfv"}, {'v', "cfgb"}, {'b', "vghn"},
        {'n', "bhjm"}, {'m', "njk"},
    };
    return map;
}

struct Slip {
    std::string keys;
    const char* kind;
};

// One slip of each kind, at a position chosen by `rng` so the sample is spread over the
// whole word rather than always hitting the first letter.
std::vector<Slip> slipsOf(const std::string& keys, std::mt19937& rng) {
    std::vector<Slip> out;
    if (keys.size() < 2) return out;
    const auto pick = [&](std::size_t limit) {
        return std::uniform_int_distribution<std::size_t>(0, limit - 1)(rng);
    };

    // Adjacent key: the finger landed one key over.
    for (int attempt = 0; attempt < 8 && out.empty(); ++attempt) {
        const std::size_t i = pick(keys.size());
        const auto n = neighbours().find(keys[i]);
        if (n == neighbours().end() || n->second.empty()) continue;
        std::string t = keys;
        t[i] = n->second[pick(n->second.size()) % n->second.size()];
        if (t != keys) out.push_back({t, "adjacent"});
    }
    // Key bounce: the same key registered twice.
    {
        const std::size_t i = pick(keys.size());
        std::string t = keys;
        t.insert(i, 1, keys[i]);
        out.push_back({t, "doubled"});
    }
    // Transposition: two keys in the wrong order.
    if (keys.size() >= 3) {
        const std::size_t i = pick(keys.size() - 1);
        std::string t = keys;
        std::swap(t[i], t[i + 1]);
        if (t != keys) out.push_back({t, "transposed"});
    }
    // A key that never registered.
    if (keys.size() >= 3) {
        const std::size_t i = pick(keys.size());
        std::string t = keys;
        t.erase(i, 1);
        out.push_back({t, "dropped"});
    }
    return out;
}

// --- running one case through everything ------------------------------------------------------

std::string jsonEscape(std::u32string_view s) {
    std::string out;
    for (const char32_t c : s) {
        if (c < 0x80) {
            out.push_back(static_cast<char>(c));
        } else {
            char buf[16];
            std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned>(c));
            out += buf;
        }
    }
    return out;
}

struct Outcome {
    int fixed = 0;
    int harmful = 0;
    int untouched = 0;
    [[nodiscard]] int total() const { return fixed + harmful + untouched; }
};

class KeystrokeEval : public testing::Test {
protected:
    static const BaseSyllableSet& dict() { return BaseSyllableSet::builtin(); }

    // Every syllable this encoder can type correctly, with its key sequence. Verified by
    // the engine itself: a sequence that does not compose back to the word is dropped, so
    // the sample never credits the corrector for a word we cannot even type.
    struct Word {
        std::u32string text;
        std::string keys;
    };
    static const std::vector<Word>& corpus() {
        static const std::vector<Word> words = [] {
            std::vector<std::u32string> all;
            dict().forEach([&](std::u32string_view s) { all.emplace_back(s); });
            std::sort(all.begin(), all.end());
            std::mt19937 rng(20260925);
            std::shuffle(all.begin(), all.end(), rng);

            const auto engines = registeredEngines();
            std::vector<Word> out;
            if (engines.empty()) return out;
            std::size_t attempted = 0;
            for (const auto& w : all) {
                if (out.size() >= kSampleSize) break;
                ++attempted;
                const auto keys = telexKeys(w);
                if (!keys) continue;
                auto engine = engines.front().make();
                Typist typist(*engine);
                typist.type(*keys);
                if (typist.screen() == w) out.push_back({w, *keys});
            }
            std::printf("corpus: %zu words usable out of %zu tried (dictionary %zu)\n", out.size(),
                        attempted, BaseSyllableSet::builtin().size());
            return out;
        }();
        return words;
    }

    static constexpr std::size_t kSampleSize = 400;

    // Filled once per test: what each slip produces with the corrector off, and the word
    // the typist meant. Keyed by the slip's key sequence.
    std::map<std::string, std::u32string> baseline_;
    std::map<std::string, Word> intended_;
    // The composed syllable the engine gave up on, when it restored raw keys.
    std::map<std::string, std::u32string> restoredForm_;

    // The composed form the engine threw away when it put the raw keys back, or empty
    // when it never gave up on the syllable.
    static std::u32string composedBeforeRestore(const Word& word, const std::string& slipKeys) {
        (void)word;
        const auto engines = registeredEngines();
        if (engines.empty()) return {};
        auto engine = engines.front().make();
        std::u32string restored;
        for (const char c : slipKeys + " ") {
            const auto r = engine->process(Typist::toKeyEvent(c));
            if (!r.restoredFrom.empty()) restored = r.restoredFrom;
        }
        return restored;
    }

    // What ends up on screen after typing `slipKeys` and a space.
    static std::u32string screenAfter(const Word& word, const std::string& slipKeys,
                                      const char* level, std::uint32_t historyFreq) {
        const auto engines = registeredEngines();
        if (engines.empty()) return {};
        auto engine = engines.front().make();
        std::string keylog;
        if (historyFreq > 0) {
            keylog += "{\"lexicon\":[[\"" + jsonEscape(word.text) + "\"," +
                      std::to_string(historyFreq) + "]]}\n";
        }
        keylog += std::string("{\"autocorrect\":\"") + level + "\"}\n";
        keylog += "{\"text\":\"" + slipKeys + " \"}";
        const auto events = ReplayHarness::parse(keylog);
        if (!events) return {};
        const auto r = ReplayHarness::run(*events, *engine);
        if (!r) return {};
        std::u32string screen = core::text::nfc(r->screen);
        while (!screen.empty() && screen.back() == U' ')
            screen.pop_back();
        return screen;
    }
};

TEST_F(KeystrokeEval, SlipsThroughTheWholePath) {
    if (registeredEngines().empty()) GTEST_SKIP() << "no engine adapter built";
    ASSERT_FALSE(corpus().empty());

    struct Run {
        const char* level;
        std::uint32_t history;
    };
    const Run runs[] = {
        {"cautious", 0}, {"balanced", 0}, {"aggressive", 0}, {"balanced", 5}, {"aggressive", 5},
    };

    // A slip the engine itself absorbs is not a test of the corrector: Telex takes the tone
    // key anywhere after the vowel, so swapping it with a neighbour often spells the same
    // word. Those are counted apart and excluded from the rates.
    std::map<std::string, int> absorbed;
    std::map<std::string, std::vector<Slip>> real; // by kind, only the slips that do change
    {
        std::mt19937 rng(424242);
        for (const auto& w : corpus()) {
            for (const auto& slip : slipsOf(w.keys, rng)) {
                const std::u32string off = screenAfter(w, slip.keys, "off", 0);
                if (off == w.text) {
                    ++absorbed[slip.kind];
                    continue;
                }
                real[slip.kind].push_back({slip.keys, slip.kind});
                baseline_[slip.keys] = off;
                intended_[slip.keys] = w;
                if (const auto composed = composedBeforeRestore(w, slip.keys); !composed.empty()) {
                    restoredForm_[slip.keys] = composed;
                }
            }
        }
    }
    // How far is the intended word from what the corrector actually gets to look at? The
    // levels reach 3, 5 and 7 scaled units (0.6, 1.0, 1.5). A slip whose intended word
    // sits beyond 7 cannot be reached by ANY ranking change - only by giving the corrector
    // a different view of the input. That is the headroom question for a keyboard-adjacency
    // model, answered with numbers instead of intuition.
    std::printf("\nDistance from what the corrector sees to the intended word\n");
    std::printf("%-12s %8s %8s %8s %8s %8s\n", "class", "n", "<=3", "<=5", "<=7", ">7");
    for (const auto& [kind, slips] : real) {
        int within3 = 0, within5 = 0, within7 = 0, beyond = 0;
        for (const auto& slip : slips) {
            const Word& w = intended_.at(slip.keys);
            // What check() is handed: the composed form the engine gave up on when it
            // restored raw keys, otherwise the syllable as it stands.
            const std::u32string& seen = restoredForm_.count(slip.keys) != 0
                                             ? restoredForm_.at(slip.keys)
                                             : baseline_.at(slip.keys);
            const int d = core::text::VietnameseDistance::scaled(seen, w.text);
            if (d <= 3)
                ++within3;
            else if (d <= 5)
                ++within5;
            else if (d <= 7)
                ++within7;
            else
                ++beyond;
        }
        const auto n = static_cast<double>(slips.size());
        std::printf("%-12s %8zu %7.1f%% %7.1f%% %7.1f%% %7.1f%%\n", kind.c_str(), slips.size(),
                    100.0 * within3 / n, 100.0 * within5 / n, 100.0 * within7 / n,
                    100.0 * beyond / n);
    }

    std::printf("\nSlips the engine absorbs by itself (excluded):");
    for (const auto& [kind, n] : absorbed)
        std::printf("  %s=%d", kind.c_str(), n);
    std::printf("\n");

    for (const auto& run : runs) {
        std::map<std::string, Outcome> byKind;
        for (const auto& [kind, slips] : real) {
            for (const auto& slip : slips) {
                const Word& w = intended_.at(slip.keys);
                const std::u32string on = screenAfter(w, slip.keys, run.level, run.history);
                auto& o = byKind[kind];
                if (on == w.text) {
                    ++o.fixed;
                } else if (on == baseline_.at(slip.keys)) {
                    ++o.untouched; // the corrector left it exactly as the engine produced it
                } else {
                    ++o.harmful; // the corrector changed it, and not into the right word
                }
            }
        }
        std::printf("\n=== %s, history=%u ===\n", run.level, run.history);
        std::printf("%-12s %8s %8s %9s %8s\n", "class", "n", "fixed", "harmful", "left");
        for (const auto& [kind, o] : byKind) {
            const double harmful = 100.0 * o.harmful / o.total();
            std::printf("%-12s %8d %7.1f%% %8.1f%% %7.1f%%\n", kind.c_str(), o.total(),
                        100.0 * o.fixed / o.total(), harmful, 100.0 * o.untouched / o.total());
            // The one hard guarantee, as in autocorrect_eval_test: a slip may be left
            // alone, but it must rarely be turned into a DIFFERENT word. 8% is the
            // ceiling measured on 2026-09-25 (dropped keys, cautious, no history) plus
            // room for sampling noise - a floor to build on, not a target. Bringing it
            // down is what the margin rule did (8.0 -> 3.3 measured); pushing it up means a change
            // made the corrector less trustworthy, whatever it did to the fix rate.
            EXPECT_LE(harmful, 4.0) << run.level << " history=" << run.history << " " << kind;
        }
    }
}

// Why is a slip whose intended word IS within reach still not fixed? Three answers, and
// only one of them is a defect:
//   ambiguous  several candidates score within the margin - the corrector refuses on
//              purpose, and guessing would be worse than leaving it alone
//   misranked  another candidate scores clearly better than the intended word
//   reachable  the intended word wins by the margin, so it should already be fixed
// Splitting the headroom this way says whether better ranking is worth building at all.
TEST_F(KeystrokeEval, WhyReachableSlipsAreNotFixed) {
    if (registeredEngines().empty()) GTEST_SKIP() << "no engine adapter built";
    const auto base = core::smart::BaseIndex::build(dict());
    ASSERT_TRUE(base);

    constexpr int kReach = 7; // the Aggressive level
    constexpr double kToneMismatch = 2.0;
    constexpr double kFrequencyBonus = 2.0;
    constexpr double kMarginKnown = 1.0;
    constexpr std::uint32_t kHistory = 5; // the intended word, typed five times

    std::mt19937 rng(424242);
    std::map<std::string, std::map<std::string, int>> byKind;
    std::vector<core::smart::FuzzyIndex::Match> matches;
    for (const auto& w : corpus()) {
        for (const auto& slip : slipsOf(w.keys, rng)) {
            const std::u32string off = screenAfter(w, slip.keys, "off", 0);
            if (off == w.text) continue; // absorbed by the engine
            const std::u32string composed = composedBeforeRestore(w, slip.keys);
            const std::u32string& seen = composed.empty() ? off : composed;
            auto& bucket = byKind[slip.kind];

            // Step 4 of check(): the slip is itself a real word. Dropping a key very often
            // spells another one, and correcting a valid word is the one thing the design
            // will never do - there is no way to turn a real word into another without
            // breaking everybody who meant the first.
            if (dict().contains(seen)) {
                ++bucket["a real word"];
                continue;
            }

            base->tree.search(seen, kReach, matches);
            std::erase_if(matches, [](const auto& m) { return m.scaledDistance == 0; });
            if (matches.empty()) {
                ++bucket["out of reach"];
                continue;
            }
            const bool seenHasTone = core::text::hasTone(seen);
            const auto scoreOf = [&](const core::smart::FuzzyIndex::Match& m) {
                double score = static_cast<double>(m.scaledDistance);
                if (core::text::hasTone(m.key) != seenHasTone) score += kToneMismatch;
                if (m.key == w.text) {
                    score -= kFrequencyBonus * std::log2(1.0 + static_cast<double>(kHistory));
                }
                return score;
            };
            double bestScore = 0.0;
            double intendedScore = 0.0;
            bool intendedFound = false;
            const core::smart::FuzzyIndex::Match* best = nullptr;
            double runnerUp = std::numeric_limits<double>::infinity();
            for (const auto& m : matches) {
                const double sc = scoreOf(m);
                if (m.key == w.text) {
                    intendedFound = true;
                    intendedScore = sc;
                }
                if (best == nullptr || sc < bestScore) {
                    if (best != nullptr) runnerUp = bestScore;
                    best = &m;
                    bestScore = sc;
                } else if (sc < runnerUp) {
                    runnerUp = sc;
                }
            }
            if (!intendedFound) {
                ++bucket["out of reach"];
            } else if (intendedScore > bestScore) {
                ++bucket["misranked"];
            } else if (runnerUp - bestScore < kMarginKnown) {
                ++bucket["ambiguous"];
            } else {
                ++bucket["reachable"];
            }
        }
    }

    std::printf("\nWhy a slip is not fixed (Aggressive reach, word typed 5x)\n");
    std::printf("%-12s %10s %10s %10s %12s %12s\n", "class", "reachable", "ambiguous", "misranked",
                "out of reach", "a real word");
    for (const auto& [kind, bucket] : byKind) {
        int total = 0;
        for (const auto& [_, n] : bucket)
            total += n;
        const auto pct = [&](const char* k) {
            const auto it = bucket.find(k);
            return 100.0 * (it == bucket.end() ? 0 : it->second) / total;
        };
        std::printf("%-12s %9.1f%% %9.1f%% %9.1f%% %11.1f%% %11.1f%%\n", kind.c_str(),
                    pct("reachable"), pct("ambiguous"), pct("misranked"), pct("out of reach"),
                    pct("a real word"));
    }
}

} // namespace
} // namespace lankey::tests
