// AutoCorrect evaluation and benchmark. Generates the typo classes a Telex typist actually
// produces from every dictionary syllable, runs them through AutoCorrectEngine and reports
// per level: fixed (back to the source word), harmful (changed to another word), left
// alone. The one hard guarantee is the harmful rate; the fix rate is reported so a change
// to the ranking can be judged. Also times BaseIndex::build and check().

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "core/model/Thresholds.h"
#include "core/smart/correct/AutoCorrectEngine.h"
#include "core/text/Utf.h"
#include "core/text/VietnameseDistance.h"
#include "core/text/VietnameseText.h"

namespace lankey::core::smart {
namespace {

using model::AutoCorrectLevel;
using model::AutoCorrectSettings;
using model::LexiconEntry;
using model::Syllable;
using model::SyllableCommitted;

struct Typo {
    std::u32string source;
    std::u32string typo;
    const char* kind;
};

// Every dictionary letter grouped by base vowel: "u" -> {u ú ù ủ ũ ụ ư ứ ...}. Built from
// the dictionary itself so the variants are exactly what Vietnamese uses.
std::map<char32_t, std::vector<char32_t>> vowelVariants(const BaseSyllableSet& dict) {
    std::map<char32_t, std::set<char32_t>> groups;
    dict.forEach([&](std::u32string_view s) {
        for (const char32_t c : s) {
            const char32_t base = text::stripDiacritics(c);
            if (base == U'a' || base == U'e' || base == U'i' || base == U'o' || base == U'u' ||
                base == U'y') {
                groups[base].insert(c);
            }
        }
    });
    std::map<char32_t, std::vector<char32_t>> out;
    for (const auto& [base, set] : groups)
        out[base].assign(set.begin(), set.end());
    return out;
}

// The letter without its tone mark ("ờ" -> "ơ"): the modifier form it composes from.
char32_t toneless(char32_t c) {
    if (!text::hasTone(c)) return c;
    static const std::u32string modifiers = U"aăâeêioôơuưy";
    static constexpr char32_t tones[] = {0x0300, 0x0301, 0x0303, 0x0309, 0x0323};
    for (const char32_t m : modifiers) {
        if (text::stripDiacritics(m) != text::stripDiacritics(c)) continue;
        for (const char32_t tone : tones) {
            const std::u32string composed = text::nfc(std::u32string{m, tone});
            if (composed.size() == 1 && composed[0] == c) return m;
        }
    }
    return text::stripDiacritics(c);
}

std::vector<Typo> generateTypos(const BaseSyllableSet& dict) {
    const auto variants = vowelVariants(dict);
    std::vector<Typo> typos;
    std::set<std::u32string> seen;
    const auto add = [&](std::u32string_view source, std::u32string typo, const char* kind) {
        if (typo == source || dict.contains(typo) || !seen.insert(typo).second) return;
        typos.push_back({std::u32string(source), std::move(typo), kind});
    };
    dict.forEach([&](std::u32string_view w) {
        for (std::size_t i = 0; i < w.size(); ++i) {
            // One-vowel slips, by what the typist did:
            //   tone-swap   pressed the wrong tone key (s/f/r/x/j are neighbours)
            //   tone-added  pressed a tone key after a toneless word
            //   modifier    forgot or added w/aa/oo/ee (ư<->u, â<->a...), tone kept
            // tone-added and tone-swap produce the same typo from different sources, so
            // the two classes can never both be fixed; the engine bets on tone-swap.
            const auto g = variants.find(text::stripDiacritics(w[i]));
            if (g != variants.end()) {
                for (const char32_t v : g->second) {
                    std::u32string t(w);
                    t[i] = v;
                    const bool srcTone = text::hasTone(w[i]);
                    const bool typoTone = text::hasTone(v);
                    const char* kind = "modifier";
                    if (srcTone && typoTone) {
                        kind = toneless(w[i]) == toneless(v) ? "tone-swap" : "modifier";
                    } else if (!srcTone && typoTone) {
                        kind = "tone-added";
                    } else if (srcTone && !typoTone) {
                        kind = "tone-dropped";
                    }
                    add(w, std::move(t), kind);
                }
            }
            // Doubled letter (key bounce) and dropped letter.
            std::u32string doubled(w);
            doubled.insert(i, 1, w[i]);
            add(w, std::move(doubled), "doubled");
            if (w.size() > 2) {
                std::u32string dropped(w);
                dropped.erase(i, 1);
                add(w, std::move(dropped), "dropped");
            }
        }
    });
    return typos;
}

SyllableCommitted committed(std::u32string_view typo) {
    SyllableCommitted c;
    c.window.committed.push_back(Syllable::fromComposed(typo));
    c.terminator = U' ';
    c.vietnameseTransformApplied = true;
    return c;
}

struct Outcome {
    int fixed = 0;
    int harmful = 0;
    int untouched = 0;
    [[nodiscard]] int total() const { return fixed + harmful + untouched; }
};

const char* levelName(AutoCorrectLevel l) {
    switch (l) {
    case AutoCorrectLevel::Off:
        return "off";
    case AutoCorrectLevel::Cautious:
        return "cautious";
    case AutoCorrectLevel::Balanced:
        return "balanced";
    case AutoCorrectLevel::Aggressive:
        return "aggressive";
    }
    return "?";
}

class AutoCorrectEval : public testing::Test {
protected:
    static const BaseSyllableSet& dict() { return BaseSyllableSet::builtin(); }
    static std::shared_ptr<const BaseIndex> base() {
        static const auto index = BaseIndex::build(dict());
        return index;
    }
    static const std::vector<Typo>& typos() {
        static const auto all = generateTypos(dict());
        return all;
    }

    // `history`: the source word was typed `historyFreq` times before (0 = new user).
    std::map<std::string, Outcome> evaluate(AutoCorrectLevel level, std::uint32_t historyFreq) {
        AutoCorrectEngine engine;
        engine.publishBase(base());
        AutoCorrectSettings s;
        s.level = level;
        engine.setSettings(s);
        std::map<std::string, Outcome> byKind;
        std::vector<LexiconEntry> history(1);
        for (const auto& t : typos()) {
            if (historyFreq > 0) {
                history[0].phrase.syllables = {Syllable::fromComposed(t.source)};
                history[0].frequency = historyFreq;
                engine.publish(CorrectionSnapshot::build(history, {}, {}, dict(), 0));
            } else {
                engine.publish(CorrectionSnapshot::build({}, {}, {}, dict(), 0));
            }
            auto& o = byKind[t.kind];
            const auto c = engine.check(committed(t.typo));
            if (!c) {
                ++o.untouched;
            } else if (c->corrected[0].text == t.source) {
                ++o.fixed;
            } else {
                ++o.harmful;
                if (o.harmful <= 4) {
                    std::printf("[eval]   harmful %s: %s -> %s (meant %s) d(src)=%d d(pick)=%d\n",
                                t.kind, text::toUtf8(t.typo).c_str(),
                                text::toUtf8(c->corrected[0].text).c_str(),
                                text::toUtf8(t.source).c_str(),
                                text::VietnameseDistance::scaled(t.typo, t.source),
                                text::VietnameseDistance::scaled(t.typo, c->corrected[0].text));
                }
            }
        }
        return byKind;
    }

    static void report(const char* title, const std::map<std::string, Outcome>& byKind) {
        std::printf("[eval] %s\n", title);
        Outcome all;
        for (const auto& [kind, o] : byKind) {
            std::printf("[eval]   %-11s n=%6d fixed=%5.1f%% harmful=%5.2f%% untouched=%5.1f%%\n",
                        kind.c_str(), o.total(), 100.0 * o.fixed / o.total(),
                        100.0 * o.harmful / o.total(), 100.0 * o.untouched / o.total());
            all.fixed += o.fixed;
            all.harmful += o.harmful;
            all.untouched += o.untouched;
        }
        std::printf("[eval]   %-11s n=%6d fixed=%5.1f%% harmful=%5.2f%% untouched=%5.1f%%\n", "all",
                    all.total(), 100.0 * all.fixed / all.total(), 100.0 * all.harmful / all.total(),
                    100.0 * all.untouched / all.total());
    }

    static double harmfulRate(const std::map<std::string, Outcome>& byKind) {
        Outcome all;
        for (const auto& [kind, o] : byKind) {
            all.harmful += o.harmful;
            all.fixed += o.fixed;
            all.untouched += o.untouched;
        }
        return static_cast<double>(all.harmful) / all.total();
    }
};

TEST_F(AutoCorrectEval, NewUserIsNeverHarmedAtCautious) {
    const auto r = evaluate(AutoCorrectLevel::Cautious, 0);
    report("cautious, no history", r);
    // A wrong correction is the one thing worse than none. For a brand-new user the
    // engine may change a typo only when the intended word is the clear winner. Judged
    // on the classes where the source is knowable (a wrong tone key, a modifier slip, a
    // bounced key); "tone-added" is the mirror image of "tone-swap" and "dropped" of a
    // tone slip of another word, so their harm is the price of fixing those.
    for (const char* kind : {"tone-swap", "modifier", "doubled"}) {
        const auto it = r.find(kind);
        ASSERT_NE(it, r.end()) << kind;
        const auto& o = it->second;
        EXPECT_LT(static_cast<double>(o.harmful) / o.total(), 0.005) << kind;
    }
}

TEST_F(AutoCorrectEval, HistoryMakesCorrectionsMoreLikelyNotLessSafe) {
    const auto without = evaluate(AutoCorrectLevel::Balanced, 0);
    const auto with = evaluate(AutoCorrectLevel::Balanced, 5);
    report("balanced, no history", without);
    report("balanced, source typed 5x before", with);
    EXPECT_LE(harmfulRate(with), harmfulRate(without));
    Outcome a;
    Outcome b;
    for (const auto& [k, o] : without)
        a.fixed += o.fixed;
    for (const auto& [k, o] : with)
        b.fixed += o.fixed;
    EXPECT_GT(b.fixed, a.fixed);
}

TEST_F(AutoCorrectEval, ValidWordsAreNeverTouched) {
    AutoCorrectEngine engine;
    engine.publishBase(base());
    AutoCorrectSettings s;
    s.level = AutoCorrectLevel::Aggressive;
    engine.setSettings(s);
    engine.publish(CorrectionSnapshot::build({}, {}, {}, dict(), 0));
    int touched = 0;
    dict().forEach([&](std::u32string_view w) {
        if (engine.check(committed(w))) ++touched;
    });
    EXPECT_EQ(touched, 0);
}

TEST_F(AutoCorrectEval, Timing) {
    using clock = std::chrono::steady_clock;
    const auto t0 = clock::now();
    const auto index = BaseIndex::build(dict());
    const auto buildMs = std::chrono::duration<double, std::milli>(clock::now() - t0).count();

    AutoCorrectEngine engine;
    engine.publishBase(index);
    engine.publish(CorrectionSnapshot::build({}, {}, {}, dict(), 0));
    for (const auto level :
         {AutoCorrectLevel::Cautious, AutoCorrectLevel::Balanced, AutoCorrectLevel::Aggressive}) {
        AutoCorrectSettings s;
        s.level = level;
        engine.setSettings(s);
        // Only typos reach the fuzzy step (dictionary words return at step 4); a strided
        // sample of ~2000 of them.
        const auto& all = typos();
        const std::size_t stride = std::max<std::size_t>(1, all.size() / 2000);
        int n = 0;
        const auto t1 = clock::now();
        for (std::size_t i = 0; i < all.size(); i += stride, ++n) {
            (void)engine.check(committed(all[i].typo));
        }
        const auto us = std::chrono::duration<double, std::micro>(clock::now() - t1).count() / n;
        std::printf("[bench] autocorrect check (fuzzy path) %-10s avg=%.1fus over %d typos\n",
                    levelName(level), us, n);
        EXPECT_LT(us, 1000.0) << "PLAN 6.2: lookup < 1 ms on the worker thread";
    }
    std::printf("[bench] BaseIndex::build %.1fms for %zu syllables (%zu nodes)\n", buildMs,
                dict().size(), index->tree.size());
    EXPECT_LT(buildMs, 500.0);
}

} // namespace
} // namespace lankey::core::smart
