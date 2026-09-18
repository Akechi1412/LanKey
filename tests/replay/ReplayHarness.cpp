#include "tests/replay/ReplayHarness.h"

#include <fstream>
#include <sstream>
#include <string_view>

#include <nlohmann/json.hpp>

#include "core/model/Correction.h"
#include "core/model/Thresholds.h"
#include "core/smart/correct/AutoCorrectEngine.h"
#include "core/smart/correct/ManualCorrectionDetector.h"
#include "core/text/Utf.h"

#include "tests/fakes/InMemoryLexiconStore.h"
#include "tests/support/PipelineRig.h"
#include "tests/support/Typist.h"

namespace lankey::tests {

using core::model::AutoCorrectLevel;
using core::model::Error;
using core::model::KeyEvent;
using core::model::Modifier;
using core::model::VirtualKey;

namespace {

lk::unexpected fail(std::string msg) {
    return lk::unexpected(Error::make(Error::Code::Corrupted, std::move(msg)));
}

std::optional<VirtualKey> namedKey(std::string_view name) {
    static constexpr std::pair<std::string_view, VirtualKey> kNames[] = {
        {"Backspace", VirtualKey::Backspace},
        {"Enter", VirtualKey::Enter},
        {"Tab", VirtualKey::Tab},
        {"Escape", VirtualKey::Escape},
        {"Space", VirtualKey::Space},
        {"Left", VirtualKey::ArrowLeft},
        {"Right", VirtualKey::ArrowRight},
        {"Up", VirtualKey::ArrowUp},
        {"Down", VirtualKey::ArrowDown},
        {"Home", VirtualKey::Home},
        {"End", VirtualKey::End},
        {"Delete", VirtualKey::Delete},
    };
    for (const auto& [n, k] : kNames) {
        if (n == name) return k;
    }
    return std::nullopt;
}

std::optional<Modifier> namedModifier(std::string_view name) {
    if (name == "Shift") return Modifier::Shift;
    if (name == "Ctrl") return Modifier::Control;
    if (name == "Alt") return Modifier::Alt;
    if (name == "Win") return Modifier::Win;
    if (name == "CapsLock") return Modifier::CapsLock;
    return std::nullopt;
}

lk::expected<KeyEvent> keyFromJson(const nlohmann::json& j) {
    const std::string k = j.at("k").get<std::string>();
    KeyEvent ev;
    if (const auto named = namedKey(k)) {
        ev.key = *named;
        if (*named == VirtualKey::Space) ev.unicode = U' ';
    } else {
        const std::u32string u = ReplayHarness::fromUtf8(k);
        if (u.size() != 1) return fail("keylog: \"k\" must be one character or a key name: " + k);
        if (u[0] < 0x80) {
            ev = Typist::toKeyEvent(static_cast<char>(u[0]));
        } else {
            // Non-ASCII literal (e.g. a pasted "ư"): reaches the engine as punctuation with
            // that code point, which is how a non-QWERTY layout would look to the hook.
            ev.key = VirtualKey::Punctuation;
            ev.unicode = u[0];
        }
    }
    if (j.contains("mods")) {
        for (const auto& m : j.at("mods")) {
            const auto mod = namedModifier(m.get<std::string>());
            if (!mod) return fail("keylog: unknown modifier " + m.get<std::string>());
            ev.modifiers = ev.modifiers | *mod;
        }
    }
    return ev;
}

} // namespace

lk::expected<std::vector<ReplayHarness::Event>> ReplayHarness::parse(const std::string& keylog) {
    std::vector<Event> events;
    std::istringstream in(keylog);
    std::string line;
    int lineNo = 0;
    while (std::getline(in, line)) {
        ++lineNo;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const auto first = line.find_first_not_of(" \t");
        if (first == std::string::npos || line[first] == '#') continue;

        nlohmann::json j;
        try {
            j = nlohmann::json::parse(line);
        } catch (const nlohmann::json::exception& e) {
            return fail("keylog line " + std::to_string(lineNo) + ": " + e.what());
        }
        try {
            if (j.contains("text")) {
                for (const char c : j.at("text").get<std::string>()) {
                    Event e;
                    e.key = Typist::toKeyEvent(c);
                    events.push_back(e);
                }
            } else if (j.contains("k")) {
                auto key = keyFromJson(j);
                if (!key) return lk::unexpected(key.error());
                Event e;
                e.key = *key;
                events.push_back(e);
            } else if (j.contains("focus")) {
                Event e;
                e.kind = Event::Kind::Focus;
                e.app = j.at("focus").value("app", "");
                e.password = j.at("focus").value("password", false);
                events.push_back(e);
            } else if (j.contains("click")) {
                Event e;
                e.kind = Event::Kind::Click;
                events.push_back(e);
            } else if (j.contains("wait")) {
                Event e;
                e.kind = Event::Kind::Wait;
                e.waitMs = j.at("wait").get<int>();
                events.push_back(e);
            } else if (j.contains("autocorrect")) {
                Event e;
                e.kind = Event::Kind::AutoCorrect;
                const auto level = j.at("autocorrect").get<std::string>();
                if (level == "off") {
                    e.level = AutoCorrectLevel::Off;
                } else if (level == "cautious") {
                    e.level = AutoCorrectLevel::Cautious;
                } else if (level == "balanced") {
                    e.level = AutoCorrectLevel::Balanced;
                } else if (level == "aggressive") {
                    e.level = AutoCorrectLevel::Aggressive;
                } else {
                    return fail("keylog line " + std::to_string(lineNo) +
                                ": unknown autocorrect level " + level);
                }
                e.lag = j.value("lag", 0);
                events.push_back(e);
            } else if (j.contains("lexicon")) {
                Event e;
                e.kind = Event::Kind::Lexicon;
                for (const auto& item : j.at("lexicon")) {
                    e.lexicon.emplace_back(fromUtf8(item.at(0).get<std::string>()),
                                           item.at(1).get<std::uint32_t>());
                }
                events.push_back(e);
            } else {
                return fail("keylog line " + std::to_string(lineNo) + ": unknown event");
            }
        } catch (const nlohmann::json::exception& e) {
            return fail("keylog line " + std::to_string(lineNo) + ": " + e.what());
        }
    }
    return events;
}

lk::expected<ReplayResult> ReplayHarness::run(const std::vector<Event>& events,
                                              core::IVietnameseEngine& engine) {
    using core::model::Correction;
    using core::model::Thresholds;
    using core::smart::AutoCorrectEngine;
    using core::smart::BaseIndex;
    using core::smart::BaseSyllableSet;
    using core::smart::CorrectionSnapshot;
    using core::smart::ManualCorrectionDetector;

    PipelineRig rig(engine);
    rig.focus.setFocus({"replay.exe", false, 1});

    // A synchronous stand-in for SmartWorker + DB thread: the real AutoCorrectEngine over
    // the real dictionary, correction_map in memory, answers delivered `lag` key events
    // after the commit so stale-generation drops can be replayed too.
    static const auto baseIndex = BaseIndex::build(BaseSyllableSet::builtin());
    AutoCorrectEngine corrector;
    corrector.publishBase(baseIndex);
    InMemoryLexiconStore store;
    int lag = 0;
    bool autoCorrectOn = false;
    struct Pending {
        Correction correction;
        int keysToGo;
    };
    std::vector<Pending> pending;
    std::size_t seenCommits = 0;
    std::size_t seenRejections = 0;

    const auto republish = [&] {
        corrector.publish(CorrectionSnapshot::build(
            *store.loadAll(), *store.loadCorrections(), *store.loadBlacklist(),
            BaseSyllableSet::builtin(), rig.clock.nowUnixSeconds()));
    };
    const auto afterKey = [&] {
        // Corrections whose "worker" answer arrives now.
        for (auto it = pending.begin(); it != pending.end();) {
            if (it->keysToGo-- <= 0) {
                rig.pipeline().applyCorrection(it->correction);
                it = pending.erase(it);
            } else {
                ++it;
            }
        }
        for (; seenRejections < rig.correctionsRejected.size(); ++seenRejections) {
            (void)store.rejectCorrection(rig.correctionsRejected[seenRejections],
                                         Thresholds::kCorrectionRejectPenalty,
                                         rig.clock.nowUnixSeconds());
            republish();
        }
        for (; seenCommits < rig.commits.size(); ++seenCommits) {
            const auto& c = rig.commits[seenCommits];
            if (!c.retypedFrom.empty()) {
                if (const auto fix =
                        ManualCorrectionDetector::detect(c, BaseSyllableSet::builtin())) {
                    (void)store.reinforceCorrection(
                        fix->wrong, fix->correct, Thresholds::kCorrectionLearnStep,
                        core::model::CorrectionRuleSource::Learned, rig.clock.nowUnixSeconds());
                    republish();
                }
            }
            if (!autoCorrectOn) continue;
            if (auto correction = corrector.check(c)) {
                if (lag == 0) {
                    rig.pipeline().applyCorrection(*correction);
                } else {
                    pending.push_back({std::move(*correction), lag});
                }
            }
        }
    };

    for (const auto& e : events) {
        switch (e.kind) {
        case Event::Kind::Key:
            rig.press(e.key);
            afterKey();
            break;
        case Event::Kind::Focus:
            rig.focus.setFocus({e.app, e.password, 0});
            break;
        case Event::Kind::Click:
            rig.pipeline().onPointerClick();
            break;
        case Event::Kind::Wait:
            rig.clock.advanceMs(e.waitMs);
            break;
        case Event::Kind::AutoCorrect: {
            core::model::AutoCorrectSettings settings;
            settings.level = e.level;
            corrector.setSettings(settings);
            autoCorrectOn = e.level != AutoCorrectLevel::Off;
            lag = e.lag;
            republish();
            break;
        }
        case Event::Kind::Lexicon: {
            core::model::LexiconDeltaBatch batch;
            for (const auto& [syllable, freq] : e.lexicon) {
                core::model::LexiconDelta d;
                d.phrase.syllables.push_back(core::model::Syllable::fromComposed(syllable));
                d.frequencyDelta = static_cast<std::int32_t>(freq);
                d.usedAt = rig.clock.nowUnixSeconds();
                batch.push_back(std::move(d));
            }
            (void)store.applyDeltas(batch);
            republish();
            break;
        }
        }
    }
    ReplayResult result;
    result.screen = rig.screen();
    for (const auto& c : rig.commits)
        result.commits.push_back(formatCommit(c));
    return result;
}

lk::expected<ReplayResult> ReplayHarness::run(const std::filesystem::path& fixtureDir,
                                              core::IVietnameseEngine& engine) {
    const std::ifstream in(fixtureDir / "keys.keylog", std::ios::binary);
    if (!in) {
        return lk::unexpected(
            Error::make(Error::Code::NotFound, "missing keys.keylog in " + fixtureDir.string()));
    }
    std::stringstream buf;
    buf << in.rdbuf();
    auto events = parse(buf.str());
    if (!events) return lk::unexpected(events.error());
    return run(*events, engine);
}

std::string ReplayHarness::formatCommit(const core::model::SyllableCommitted& c) {
    std::string term;
    switch (c.terminator) {
    case U' ':
        term = "SP";
        break;
    case U'\n':
        term = "NL";
        break;
    case U'\t':
        term = "TAB";
        break;
    case 0:
        term = "NONE";
        break;
    default:
        term = toUtf8(std::u32string(1, c.terminator));
    }
    std::u32string joined;
    for (std::size_t i = 0; i < c.window.committed.size(); ++i) {
        if (i > 0) joined.push_back(U' ');
        joined += c.window.committed[i].text;
    }
    return toUtf8(joined) + "|" + term + "|" + (c.vietnameseTransformApplied ? "1" : "0");
}

std::string ReplayHarness::toUtf8(std::u32string_view s) {
    return core::text::toUtf8(s);
}

std::u32string ReplayHarness::fromUtf8(std::string_view s) {
    return core::text::fromUtf8(s);
}

} // namespace lankey::tests
