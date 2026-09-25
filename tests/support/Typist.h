#pragma once

#include <cctype>
#include <cstring>
#include <string>
#include <string_view>

#include "core/interfaces/IVietnameseEngine.h"
#include "core/text/Utf.h"

namespace lankey::tests {

// Simulates a text field + a typist, independent of any adapter.
//
// Takes an ASCII key string ("tieengs"), builds the matching KeyEvents, feeds them through
// the engine and applies each EngineResult to a fake "screen". Tests only compare the final
// screen contents, so the same suite works for every adapter even though each engine has
// its own backspace/retype strategy.
//
// Special characters in the key string:
//   '\b'  Backspace
//   ' '   Space (engines usually reset the syllable here)
//   '\n'  Enter
class Typist {
public:
    explicit Typist(core::IVietnameseEngine& engine) : engine_(engine) {}

    void type(std::string_view keys) {
        for (const char c : keys) {
            press(toKeyEvent(c));
        }
    }

    void press(const core::model::KeyEvent& key) {
        using core::model::EngineResult;
        using core::model::VirtualKey;

        const EngineResult result = engine_.process(key);
        last_ = result.composed;

        switch (result.action) {
        case EngineResult::Action::Swallow:
            break;
        case EngineResult::Action::Replace:
            eraseBeforeCaret(result.deleteCount);
            screen_ += result.insert;
            break;
        case EngineResult::Action::PassThrough:
            if (key.key == VirtualKey::Backspace) {
                eraseBeforeCaret(1);
            } else if (key.unicode != 0) {
                screen_ += key.unicode;
            }
            break;
        }
    }

    [[nodiscard]] const std::u32string& screen() const noexcept { return screen_; }
    [[nodiscard]] const core::model::ComposedText& lastComposed() const noexcept { return last_; }

    static core::model::KeyEvent toKeyEvent(char c) {
        using core::model::KeyEvent;
        using core::model::Modifier;
        using core::model::VirtualKey;

        KeyEvent ev;
        ev.timestampMs = 0;
        const auto uc = static_cast<unsigned char>(c);

        if (c == '\b') {
            ev.key = VirtualKey::Backspace;
        } else if (c == '\n') {
            ev.key = VirtualKey::Enter;
        } else if (c == ' ') {
            ev.key = VirtualKey::Space;
            ev.unicode = U' ';
        } else if (std::isalpha(uc) != 0) {
            const int offset = std::toupper(uc) - 'A';
            ev.key = static_cast<VirtualKey>(static_cast<int>(VirtualKey::A) + offset);
            ev.unicode = static_cast<char32_t>(uc);
            if (std::isupper(uc) != 0) {
                ev.modifiers = Modifier::Shift;
            }
        } else if (std::isdigit(uc) != 0) {
            const int offset = c - '0';
            ev.key = static_cast<VirtualKey>(static_cast<int>(VirtualKey::Digit0) + offset);
            ev.unicode = static_cast<char32_t>(uc);
        } else {
            ev.key = VirtualKey::Punctuation;
            ev.unicode = static_cast<char32_t>(uc);
            // Shifted symbols of the US layout carry Shift, as the hook reports them.
            if (std::strchr("~!@#$%^&*()_+{}|:\"<>?", c) != nullptr) {
                ev.modifiers = Modifier::Shift;
            }
        }
        return ev;
    }

private:
    void eraseBeforeCaret(int count) {
        const auto n = static_cast<std::size_t>(count < 0 ? 0 : count);
        screen_.erase(screen_.size() - (n > screen_.size() ? screen_.size() : n));
    }

    core::IVietnameseEngine& engine_;
    std::u32string screen_;
    core::model::ComposedText last_;
};

// Lets gtest print u32 strings when an assertion fails.
inline std::string toUtf8(std::u32string_view s) {
    return core::text::toUtf8(s);
}

} // namespace lankey::tests
