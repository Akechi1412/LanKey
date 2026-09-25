#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "core/interfaces/IClock.h"
#include "core/interfaces/IFocusObserver.h"
#include "core/interfaces/ISuggestionProvider.h"
#include "core/interfaces/ITextSink.h"
#include "core/interfaces/IVietnameseEngine.h"
#include "core/model/Correction.h"
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
    // Non-empty: not a list but a transient note ("đưởng → đường") shown for
    // kCorrectionNoticeMs so a correction never happens silently. Takes no keys.
    std::u32string notice;
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
    using CorrectionHandler = std::function<void(const std::u32string& wrongKey)>;
    using RejectionHandler =
        std::function<void(const std::u32string& wrongKey, const std::u32string& correctKey)>;

    struct Dependencies {
        IVietnameseEngine& engine;
        ITextSink& sink;
        IFocusObserver& focus;
        IClock& clock;
        const ISuggestionProvider* suggestions = nullptr; // optional
    };

    struct Handlers {
        CommitHandler onCommit;                // a syllable ended (worker: learn, correct)
        PopupHandler onPopup;                  // suggestion list changed (UI thread)
        SelectionHandler onSelection;          // user picked a suggestion (worker: learn +2)
        CorrectionHandler onCorrectionApplied; // a correction went on screen (worker: stats)
        RejectionHandler onCorrectionRejected; // the user undid it (worker: penalise, relearn)
    };

    struct Stats {
        std::uint64_t keys = 0;
        std::uint64_t commits = 0;
        std::uint64_t replacementsApplied = 0;
        std::uint64_t replacementsDroppedByGeneration = 0;
        std::uint64_t exceptionsSwallowed = 0;
        std::uint64_t suggestionsShown = 0;
        std::uint64_t suggestionsSelected = 0;
        std::uint64_t correctionsApplied = 0;
        std::uint64_t correctionsUndone = 0;
        std::uint64_t correctionsDropped = 0; // stale generation or unreconstructible span
        std::uint64_t retypesDetected = 0;    // manual fixes handed to the worker
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

    // AutoCorrect result from the worker (via the hook thread's task queue). Applied only
    // if the generation still matches; the on-screen span is rebuilt from what this thread
    // knows was typed (casing, separators). The next Backspace or Ctrl+Z within
    // kUndoWindowMs restores the original.
    bool applyCorrection(const model::Correction& correction);

    // The idle timer fired for the pending list computed at `generation`. Shows it if the
    // user has not typed since; otherwise does nothing (a newer list is pending or none).
    void promotePending(std::uint64_t generation);

    // Suggestions may also be picked with the digit keys 1..5 (SuggestionSettings).
    void setSelectWithDigits(bool enabled) noexcept {
        selectWithDigits_.store(enabled, std::memory_order_release);
    }
    // ... and with Enter. Off by default: Enter sends the message in most applications.
    void setSelectWithEnter(bool enabled) noexcept {
        selectWithEnter_.store(enabled, std::memory_order_release);
    }

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

    // The last kRecentCorrections corrections, newest first (Settings: "what was fixed").
    struct RecentCorrection {
        std::u32string original;
        std::u32string corrected;
        std::int64_t appliedMs = 0;
        bool undone = false;
    };
    [[nodiscard]] std::vector<RecentCorrection> recentCorrections() const;
    [[nodiscard]] std::uint64_t generation() const noexcept { return buffer_.generation(); }
    [[nodiscard]] const Stats& stats() const noexcept { return stats_; }

private:
    bool handleKey(const model::KeyEvent& key, std::uint64_t generation);
    bool handlePopupKey(const model::KeyEvent& key, std::uint64_t generation);
    bool applyEngineResult(const model::EngineResult& result, std::uint64_t generation);
    void keepPrefixInStep(const model::EngineResult& result, const model::KeyEvent& key);
    // True when the pending retype turned out to have changed nothing: the window is
    // restored as it was and the retype is dropped.
    bool restoreUntouchedSyllable(char32_t terminator);
    void commitSyllable(const std::u32string& text, bool transformApplied, char32_t terminator,
                        std::uint64_t generation);
    void refreshSuggestions(std::uint64_t generation, bool afterWordBoundary);
    void selectSuggestion(std::size_t index, std::uint64_t generation);
    void hidePopup();
    void clearPending();
    void resetContext();
    void resetContextNoexcept() noexcept;
    bool tryUndoCorrection(const model::KeyEvent& key, std::uint64_t generation);
    void onBackspaceIntoCommitted();
    void abandonRetype() noexcept { retype_.reset(); }
    void trimSpans();

    // On-screen footprint of each committed syllable, parallel to window_.committed:
    // the characters of the syllable itself plus the boundary characters typed after it.
    // Lets Backspace be matched to syllable edges and corrections retype exact spans.
    struct Span {
        int typedLength = 0;
        int trailing = 0;         // boundary characters after the syllable (usually 1)
        char32_t terminator = 0;  // the first of them
        std::u32string separator; // all of them, in order
        // Whether the engine transformed this syllable. Kept so a syllable that is
        // committed a second time (its separator was deleted and retyped) reports what it
        // reported the first time - AutoCorrect only looks at Vietnamese ones.
        bool transformApplied = false;
    };

    // The user is deleting back into committed text. Two things follow from tracking it:
    //  - the part of a half-deleted syllable still on screen (`prefix`) is glued back in
    //    front of what the engine composes next, so "sủa" -> Backspace x2 -> "ửa" commits
    //    "sửa", not the fragment "ửa" (which would then be "corrected" into nonsense);
    //  - if whole syllables are removed and the same number typed again soon after the
    //    commit, that is a manual fix worth learning (ManualCorrectionDetector).
    struct Retype {
        std::vector<model::Syllable> window; // committed syllables before deleting began
        std::vector<Span> spans;
        int deleted = 0;          // Backspaces so far
        int syllablesRemoved = 0; // syllables to be retyped (the partial one included)
        int recommitted = 0;
        bool partial = false;                // the deletion stopped inside a syllable
        std::u32string prefix;               // what remains of it on screen
        bool prefixTransformApplied = false; // what that syllable reported when committed
        bool learnable = false;              // started within kRetypeWindowMs of the last commit
    };

    // The last AutoCorrect on screen, kept until the next key: Backspace/Ctrl+Z undoes it.
    struct AppliedCorrection {
        std::vector<model::Syllable> original; // what the user typed (folded + typed)
        std::vector<Span> originalSpans;
        std::u32string insertedText; // what replaced it, without the terminator
        char32_t terminator = 0;
        std::u32string wrongKey;
        std::uint64_t generation = 0; // after the correction was applied
        std::int64_t appliedMs = 0;
    };

    Dependencies deps_;
    Handlers handlers_;
    InputBuffer buffer_;
    model::PhraseWindow window_;
    std::vector<Span> spans_;
    std::optional<Retype> retype_;
    bool uncertain_ = false; // deleted into text we never saw: next commit is a fragment
    // Set when the engine restores raw keys at a boundary; consumed by the commit that
    // follows in the same key.
    std::u32string restoredFrom_;
    std::optional<AppliedCorrection> lastCorrection_;
    std::vector<RecentCorrection> recent_; // ring, oldest first
    std::size_t recentNext_ = 0;
    std::int64_t lastCommitMs_ = 0;
    PopupState popup_;              // showing
    PopupState pending_;            // computed, waiting for the idle timer
    bool popupSuppressed_ = false;  // Esc pressed: no popup until this syllable ends
    bool swallowNextKeyUp_ = false; // key-up of a swallowed popup key must not leak either
    std::atomic<bool> vietnameseEnabled_{true};
    std::atomic<bool> selectWithDigits_{false};
    std::atomic<bool> selectWithEnter_{false};
    Stats stats_;
};

} // namespace lankey::core::pipeline
