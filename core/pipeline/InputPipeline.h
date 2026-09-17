#pragma once

#include <atomic>
#include <cstdint>
#include <functional>

#include "core/interfaces/IClock.h"
#include "core/interfaces/IFocusObserver.h"
#include "core/interfaces/ISuggestionProvider.h"
#include "core/interfaces/ITextSink.h"
#include "core/interfaces/IVietnameseEngine.h"
#include "core/model/KeyEvent.h"
#include "core/model/Phrase.h"
#include "core/model/Suggestion.h"
#include "core/model/SyllableCommitted.h"
#include "core/model/TextReplacement.h"
#include "core/pipeline/InputBuffer.h"

namespace lankey::core::pipeline {

// What the UI needs to draw the suggestion popup.
//   items empty          -> hide (and cancel any pending timer)
//   pending == true      -> do NOT draw yet: start the idle timer; when it fires, call
//                           InputPipeline::promotePending(generation) on the hook thread
//   pending == false     -> draw now (the pipeline has decided the popup is showing)
// The two-step dance is the flicker guard: suggestions are computed on every key but only
// become visible once the user pauses, and keys are only swallowed once they are visible.
struct PopupState {
    model::SuggestionList items;
    int selected = 0;
    std::uint64_t generation = 0;
    bool pending = false;
};

// The hook-thread coordinator: takes raw keys, runs the engine, keeps the syllable/phrase
// context, applies engine output through ITextSink, asks for suggestions, and hands
// finished syllables to the worker through the commit handler.
//
// Threading: every public method except applyReplacement() and setVietnameseEnabled()
// must be called on the hook thread. applyReplacement() is also hook-thread only, but the
// TextReplacement it receives was produced elsewhere - that is exactly why it checks the
// generation first. setVietnameseEnabled() may be called from any thread.
// onKey() never throws: an exception inside resets the composition and lets the key pass.
class InputPipeline {
public:
    // Receives the event by value: the pipeline moves it out, the receiver moves it on.
    using CommitHandler = std::function<void(model::SyllableCommitted&&)>;
    using PopupHandler = std::function<void(const PopupState&)>;
    using SelectionHandler = std::function<void(const model::Phrase&)>;

    struct Dependencies {
        IVietnameseEngine& engine;
        ITextSink& sink;
        IFocusObserver& focus;
        IClock& clock;
        const ISuggestionProvider* suggestions = nullptr; // optional
    };

    struct Handlers {
        CommitHandler onCommit;       // a syllable ended (worker: learn, correct)
        PopupHandler onPopup;         // suggestion list changed (UI thread)
        SelectionHandler onSelection; // user picked a suggestion (worker: learn +2)
    };

    struct Stats {
        std::uint64_t keys = 0;
        std::uint64_t commits = 0;
        std::uint64_t replacementsApplied = 0;
        std::uint64_t replacementsDroppedByGeneration = 0;
        std::uint64_t exceptionsSwallowed = 0;
        std::uint64_t suggestionsShown = 0;
        std::uint64_t suggestionsSelected = 0;
    };

    InputPipeline(Dependencies deps, Handlers handlers);
    // Convenience for tests and callers without popup/selection interest.
    InputPipeline(Dependencies deps, CommitHandler onCommit);

    // IKeySource handler. Returns true to swallow the key.
    [[nodiscard]] bool onKey(const model::KeyEvent& key) noexcept;

    // Context breaks that do not come through the keyboard. The syllable in progress is
    // discarded, not committed: the user moved away from it.
    void onFocusChanged(const model::FocusContext& focus);
    void onPointerClick();

    // Apply a command computed off the hook thread. Returns false (and applies nothing)
    // when the user has typed since the command was computed.
    bool applyReplacement(const model::TextReplacement& replacement);

    // The idle timer fired for the pending list computed at `generation`. Shows it if the
    // user has not typed since; otherwise does nothing (a newer list is pending or none).
    void promotePending(std::uint64_t generation);

    // Vietnamese on/off (tray toggle, hotkey). Off = every key passes through untouched.
    void setVietnameseEnabled(bool enabled) noexcept {
        vietnameseEnabled_.store(enabled, std::memory_order_release);
    }
    [[nodiscard]] bool vietnameseEnabled() const noexcept {
        return vietnameseEnabled_.load(std::memory_order_acquire);
    }

    [[nodiscard]] const model::PhraseWindow& window() const noexcept { return window_; }
    // The popup as SHOWN (items empty = not showing).
    [[nodiscard]] const PopupState& popup() const noexcept { return popup_; }
    // The list waiting for the idle timer (items empty = nothing pending).
    [[nodiscard]] const PopupState& pendingPopup() const noexcept { return pending_; }
    [[nodiscard]] std::uint64_t generation() const noexcept { return buffer_.generation(); }
    [[nodiscard]] const Stats& stats() const noexcept { return stats_; }

private:
    bool handleKey(const model::KeyEvent& key, std::uint64_t generation);
    bool handlePopupKey(const model::KeyEvent& key, std::uint64_t generation);
    bool applyEngineResult(const model::EngineResult& result, std::uint64_t generation);
    void commitSyllable(const std::u32string& text, bool transformApplied, char32_t terminator,
                        std::uint64_t generation);
    void refreshSuggestions(std::uint64_t generation, bool afterWordBoundary);
    void selectSuggestion(std::size_t index, std::uint64_t generation);
    void hidePopup();
    void clearPending();
    void resetContext();
    void resetContextNoexcept() noexcept;

    Dependencies deps_;
    Handlers handlers_;
    InputBuffer buffer_;
    model::PhraseWindow window_;
    PopupState popup_;              // showing
    PopupState pending_;            // computed, waiting for the idle timer
    bool popupSuppressed_ = false;  // Esc pressed: no popup until this syllable ends
    bool swallowNextKeyUp_ = false; // key-up of a swallowed popup key must not leak either
    std::atomic<bool> vietnameseEnabled_{true};
    Stats stats_;
};

} // namespace lankey::core::pipeline
