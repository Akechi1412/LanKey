// Tests the Typist simulator itself with a minimal fake engine, so that when a
// conformance test fails we know the bug is in the adapter, not in the harness.

#include <gtest/gtest.h>

#include "core/interfaces/IVietnameseEngine.h"
#include "tests/support/Typist.h"

namespace lankey::tests {
namespace {

using core::model::EngineResult;
using core::model::EngineSettings;
using core::model::KeyEvent;
using core::model::VirtualKey;

// An engine that knows no Vietnamese: passes every key through, only tracks the current
// syllable. The 'x' key is used to simulate a Replace: swaps the last char for 'X'.
class EchoEngine final : public core::IVietnameseEngine {
public:
    EngineResult process(const KeyEvent& key) override {
        EngineResult r;
        if (key.key == VirtualKey::Space) {
            reset();
            return r;
        }
        if (key.key == VirtualKey::Backspace) {
            if (!current_.empty()) current_.pop_back();
        } else if (key.unicode == U'x') {
            if (!current_.empty()) current_.pop_back();
            current_ += U'X';
            r.action = EngineResult::Action::Replace;
            r.deleteCount = 1;
            r.insert = U"X";
        } else if (key.unicode != 0) {
            current_ += key.unicode;
        }
        r.composed.text = current_;
        return r;
    }
    void reset() override { current_.clear(); }
    void configure(const EngineSettings& s) override { settings_ = s; }
    const EngineSettings& settings() const override { return settings_; }

private:
    std::u32string current_;
    EngineSettings settings_;
};

TEST(Typist, PassThroughAppendsCharacters) {
    EchoEngine engine;
    Typist t(engine);
    t.type("abc");
    EXPECT_EQ(t.screen(), U"abc");
    EXPECT_EQ(t.lastComposed().text, U"abc");
}

TEST(Typist, BackspacePassThroughErasesOne) {
    EchoEngine engine;
    Typist t(engine);
    t.type("abc\b");
    EXPECT_EQ(t.screen(), U"ab");
}

TEST(Typist, ReplaceDeletesThenInserts) {
    EchoEngine engine;
    Typist t(engine);
    t.type("abx");
    EXPECT_EQ(t.screen(), U"aX");
}

TEST(Typist, SpaceResetsSyllableButStaysOnScreen) {
    EchoEngine engine;
    Typist t(engine);
    t.type("ab cd");
    EXPECT_EQ(t.screen(), U"ab cd");
    EXPECT_EQ(t.lastComposed().text, U"cd");
}

TEST(Typist, UppercaseLetterCarriesShift) {
    const KeyEvent ev = Typist::toKeyEvent('V');
    EXPECT_EQ(ev.key, VirtualKey::V);
    EXPECT_EQ(ev.unicode, U'V');
    EXPECT_TRUE(has(ev.modifiers, core::model::Modifier::Shift));
    EXPECT_TRUE(ev.isLetter());
}

TEST(Typist, DigitMapsToDigitKey) {
    const KeyEvent ev = Typist::toKeyEvent('6');
    EXPECT_EQ(ev.key, VirtualKey::Digit6);
    EXPECT_TRUE(ev.isDigit());
}

TEST(Utf8, EncodesVietnamese) {
    EXPECT_EQ(toUtf8(U"tiếng"), "tiếng");
}

}  // namespace
}  // namespace lankey::tests
