#pragma once

#include <string>
#include <vector>

#include "core/interfaces/ITextSink.h"

namespace lankey::tests {

// Plays the role of the focused application's text field: applies every replacement to a
// screen buffer and records the commands so tests can assert on them directly.
//
// Keys that the pipeline does NOT swallow never reach an ITextSink in real life - the OS
// delivers them to the app. Tests simulate that with typeThrough().
class FakeTextSink final : public core::ITextSink {
public:
    void apply(const core::model::TextReplacement& replacement) override {
        applied_.push_back(replacement);
        eraseBeforeCaret(replacement.deleteCount);
        screen_ += replacement.insert;
    }

    // What the application would do with a key the pipeline let through.
    void typeThrough(char32_t unicode) {
        if (unicode != 0) screen_.push_back(unicode);
    }
    void backspaceThrough() { eraseBeforeCaret(1); }

    [[nodiscard]] const std::u32string& screen() const noexcept { return screen_; }
    [[nodiscard]] const std::vector<core::model::TextReplacement>& applied() const noexcept {
        return applied_;
    }
    void clear() {
        screen_.clear();
        applied_.clear();
    }

private:
    void eraseBeforeCaret(int count) {
        const auto n = static_cast<std::size_t>(count < 0 ? 0 : count);
        screen_.erase(screen_.size() - (n > screen_.size() ? screen_.size() : n));
    }

    std::u32string screen_;
    std::vector<core::model::TextReplacement> applied_;
};

} // namespace lankey::tests
