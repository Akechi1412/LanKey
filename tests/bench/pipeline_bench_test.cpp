// Hot-path budget check: the whole per-keystroke path (pipeline + suggestions over a
// 10k-phrase lexicon) must stay far below the 1 ms hook budget. Numbers are printed so
// regressions are visible in CI logs; the assertion is deliberately loose (CI runners are
// noisy) and only catches gross regressions.

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "core/smart/suggest/SuggestionEngine.h"

#include "tests/fakes/FakeEngine.h"
#include "tests/support/PipelineRig.h"

namespace lankey::core::pipeline {
namespace {

using model::LexiconEntry;
using model::Syllable;

std::vector<LexiconEntry> syntheticLexicon(int count) {
    // Pseudo-Vietnamese: 2-3 syllable phrases over a small syllable alphabet so prefixes
    // collide heavily (worst case for the best-first search).
    static const char32_t* kSyllables[] = {
        U"chương", U"trình", U"hệ",   U"điều",  U"hành", U"máy",  U"tính", U"phần", U"mềm",
        U"cứng",   U"công",  U"việc", U"người", U"dùng", U"thời", U"gian", U"chưa", U"chuyển"};
    constexpr int kN = static_cast<int>(sizeof(kSyllables) / sizeof(kSyllables[0]));
    std::vector<LexiconEntry> out;
    out.reserve(static_cast<std::size_t>(count));
    std::uint32_t seed = 12345;
    for (int i = 0; i < count; ++i) {
        LexiconEntry e;
        const int len = 1 + static_cast<int>(seed % 3);
        for (int k = 0; k < len; ++k) {
            seed = seed * 1103515245u + 12345u;
            e.phrase.syllables.push_back(Syllable::fromComposed(kSyllables[(seed >> 8) % kN]));
        }
        seed = seed * 1103515245u + 12345u;
        e.frequency = 3 + (seed >> 16) % 50;
        e.lastUsedAt = 1'700'000'000 - static_cast<std::int64_t>((seed >> 8) % 30) * 86400;
        out.push_back(std::move(e));
    }
    return out;
}

TEST(PipelineBench, PerKeystrokeCostWithSuggestions) {
    tests::FakeEngine engine;
    smart::SuggestionEngine suggestions;
    suggestions.publish(
        smart::SuggestionEngine::buildSnapshot(syntheticLexicon(10000), 1'700'000'000, {}));
    tests::PipelineRig rig(engine, &suggestions);

    // Realistic mix: words of 2-6 letters, spaces, an occasional full stop.
    const std::string text = "chuong trinh he dieu hanh may tinh phan mem cong viec nguoi dung "
                             "thoi gian chua chuyen. ";
    constexpr int kRounds = 200; // ~16k keystrokes
    std::vector<double> perKeyMicros;
    perKeyMicros.reserve(text.size() * kRounds);

    for (int r = 0; r < kRounds; ++r) {
        for (const char c : text) {
            const auto t0 = std::chrono::steady_clock::now();
            rig.press(tests::Typist::toKeyEvent(c));
            const auto t1 = std::chrono::steady_clock::now();
            perKeyMicros.push_back(std::chrono::duration<double, std::micro>(t1 - t0).count());
        }
        rig.commits.clear(); // keep the rig's own bookkeeping from growing
        rig.popups.clear();
    }

    std::ranges::sort(perKeyMicros);
    const auto pct = [&](double p) {
        return perKeyMicros[static_cast<std::size_t>(p *
                                                     static_cast<double>(perKeyMicros.size() - 1))];
    };
    double sum = 0;
    for (const double v : perKeyMicros)
        sum += v;
    const double avg = sum / static_cast<double>(perKeyMicros.size());
    std::printf("[bench] keys=%zu avg=%.1fus p50=%.1fus p95=%.1fus p99=%.1fus max=%.1fus\n",
                perKeyMicros.size(), avg, pct(0.50), pct(0.95), pct(0.99), pct(1.0));
    RecordProperty("avg_us", static_cast<int>(avg));
    RecordProperty("p99_us", static_cast<int>(pct(0.99)));

    // Gross-regression guard only: the real budget (< 1000 us incl. SendInput) is checked
    // on a quiet machine via the printed numbers. Debug builds are ~8x slower (no inlining,
    // checked containers) and are not what ships.
#ifdef NDEBUG
    EXPECT_LT(pct(0.99), 200.0);
#else
    EXPECT_LT(pct(0.99), 2000.0);
#endif
}

} // namespace
} // namespace lankey::core::pipeline
