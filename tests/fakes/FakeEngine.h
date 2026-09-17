#pragma once

#include <string>

#include "core/interfaces/IVietnameseEngine.h"

namespace lankey::tests {

// An engine that knows no Vietnamese: every key passes through and is appended to the
// current syllable; boundaries reset it. Two scripted behaviours make it useful for
// pipeline tests without the real engine:
//   'x'  -> Replace: swap the last character for 'X' (marks the syllable as transformed)
//   '#'  -> Swallow: the key is eaten, nothing changes
// Unlike OpenKeyEngineAdapter, any number of instances may exist.
class FakeEngine final : public core::IVietnameseEngine {
public:
    [[nodiscard]] core::model::EngineResult process(const core::model::KeyEvent& key) override {
        using core::model::EngineResult;
        using core::model::VirtualKey;
        EngineResult r;
        if (key.injectedBySelf || !key.isDown) {
            r.composed = composed_;
            return r;
        }
        if (key.key == VirtualKey::Backspace) {
            if (!composed_.text.empty()) composed_.text.pop_back();
            if (composed_.text.empty()) composed_.vietnameseTransformApplied = false;
        } else if (key.unicode == U'#') {
            r.action = EngineResult::Action::Swallow;
        } else if (key.unicode == U'x' && !composed_.text.empty()) {
            composed_.text.back() = U'X';
            composed_.vietnameseTransformApplied = true;
            r.action = EngineResult::Action::Replace;
            r.deleteCount = 1;
            r.insert = U"X";
        } else if (key.isLetter() || key.isDigit()) {
            composed_.text.push_back(key.unicode);
        } else {
            reset();
        }
        r.composed = composed_;
        return r;
    }

    void reset() override {
        composed_.text.clear();
        composed_.vietnameseTransformApplied = false;
    }
    void configure(const core::model::EngineSettings& s) override { settings_ = s; }
    [[nodiscard]] const core::model::EngineSettings& settings() const override { return settings_; }

private:
    core::model::ComposedText composed_;
    core::model::EngineSettings settings_;
};

} // namespace lankey::tests
