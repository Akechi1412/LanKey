#pragma once

#include <string_view>
#include <vector>

#include "core/interfaces/ISuggestionProvider.h"
#include "core/interfaces/IVietnameseEngine.h"
#include "core/pipeline/InputPipeline.h"

#include "tests/fakes/FakeClock.h"
#include "tests/fakes/FakeFocusObserver.h"
#include "tests/fakes/FakeKeySource.h"
#include "tests/fakes/FakeTextSink.h"
#include "tests/support/Typist.h"

namespace lankey::tests {

// Everything needed to drive an InputPipeline end to end with fakes, plus the "application"
// behaviour for keys that are let through. The engine is injected so the same rig serves
// FakeEngine unit tests and real-engine replay tests.
class PipelineRig {
public:
    explicit PipelineRig(core::IVietnameseEngine& engine,
                         const core::ISuggestionProvider* suggestions = nullptr)
        : pipeline_(
              {engine, sink, focus, clock, suggestions},
              core::pipeline::InputPipeline::Handlers{
                  [this](core::model::SyllableCommitted&& c) { commits.push_back(std::move(c)); },
                  [this](const core::pipeline::PopupState& p) { popups.push_back(p); },
                  [this](const core::model::Phrase& p) { selections.push_back(p); },
                  [this](const std::u32string& w) { correctionsApplied.push_back(w); },
                  [this](const std::u32string& w, const std::u32string& c) {
                      correctionsRejected.push_back(w);
                      correctionsRejectedTo.push_back(c);
                  }}) {
        keys.setHandler([this](const core::model::KeyEvent& k) { return pipeline_.onKey(k); });
        keys.start();
        focus.onChange([this](const core::model::FocusContext& f) { pipeline_.onFocusChanged(f); });
    }

    // Press one key; if the pipeline lets it through, the "app" types it.
    bool press(const core::model::KeyEvent& key) {
        const bool swallowed = keys.press(key);
        if (!swallowed && key.isDown && !key.injectedBySelf) {
            if (key.key == core::model::VirtualKey::Backspace) {
                sink.backspaceThrough();
            } else if (!key.hasSystemModifier()) {
                sink.typeThrough(key.unicode);
            }
        }
        clock.advanceMs(100);
        return swallowed;
    }

    // Same conventions as Typist::type ('\b' Backspace, '\n' Enter, ' ' Space).
    void type(std::string_view text) {
        for (const char c : text)
            press(Typist::toKeyEvent(c));
    }

    // The idle timer "fires": whatever is pending for the current generation gets shown.
    void settle() { pipeline_.promotePending(pipeline_.generation()); }

    [[nodiscard]] core::pipeline::InputPipeline& pipeline() noexcept { return pipeline_; }
    [[nodiscard]] const core::pipeline::InputPipeline& pipeline() const noexcept {
        return pipeline_;
    }
    [[nodiscard]] const std::u32string& screen() const noexcept { return sink.screen(); }

    FakeKeySource keys;
    FakeTextSink sink;
    FakeFocusObserver focus;
    FakeClock clock;
    std::vector<core::model::SyllableCommitted> commits;
    std::vector<core::pipeline::PopupState> popups;
    std::vector<core::model::Phrase> selections;
    std::vector<std::u32string> correctionsApplied;
    std::vector<std::u32string> correctionsRejected;
    std::vector<std::u32string> correctionsRejectedTo;

private:
    core::pipeline::InputPipeline pipeline_;
};

} // namespace lankey::tests
