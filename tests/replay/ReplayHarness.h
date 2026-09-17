#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "core/interfaces/IVietnameseEngine.h"
#include "core/model/Error.h"
#include "core/model/KeyEvent.h"
#include "core/model/SyllableCommitted.h"

namespace lankey::tests {

// Runs a recorded key sequence through the whole InputPipeline with fake platform pieces
// and reports what the application would show and which syllables were committed.
//
// Fixture directory layout:
//   keys.keylog    JSON lines, one event per line, '#' comments allowed:
//                    {"k":"c"}                       printable character (case = Shift)
//                    {"k":"Backspace"}               named key: Backspace Enter Tab Escape
//                                                    Space Left Right Up Down Home End Delete
//                    {"k":"Tab","mods":["Alt"]}      with Ctrl/Alt/Win/Shift/CapsLock
//                    {"text":"chaof banj"}           shorthand for a run of {"k":...}
//                    {"focus":{"app":"x.exe","password":false}}
//                    {"click":true}
//                    {"wait":1500}                   advance the fake clock (ms)
//   expected.txt   final screen contents (a single trailing newline is ignored)
//   commits.txt    optional; one line per committed syllable:
//                    <window joined>|<terminator>|<transform 0/1>
//                  terminator is SP, NL, TAB, NONE or the character itself.
struct ReplayResult {
    std::u32string screen;
    std::vector<std::string> commits; // formatted as in commits.txt
};

class ReplayHarness {
public:
    // Parses keys.keylog. Exposed for unit-testing the parser.
    struct Event {
        enum class Kind { Key, Focus, Click, Wait } kind = Kind::Key;
        core::model::KeyEvent key;
        std::string app;
        bool password = false;
        int waitMs = 0;
    };
    [[nodiscard]] static lk::expected<std::vector<Event>> parse(const std::string& keylog);

    [[nodiscard]] static lk::expected<ReplayResult> run(const std::filesystem::path& fixtureDir,
                                                        core::IVietnameseEngine& engine);
    [[nodiscard]] static lk::expected<ReplayResult> run(const std::vector<Event>& events,
                                                        core::IVietnameseEngine& engine);

    [[nodiscard]] static std::string formatCommit(const core::model::SyllableCommitted& c);
    [[nodiscard]] static std::string toUtf8(std::u32string_view s);
    [[nodiscard]] static std::u32string fromUtf8(std::string_view s);
};

} // namespace lankey::tests
