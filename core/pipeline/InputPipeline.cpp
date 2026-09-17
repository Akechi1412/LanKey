#include "core/pipeline/InputPipeline.h"

#include <algorithm>
#include <utility>

#include "core/model/Thresholds.h"
#include "core/pipeline/WordBoundaryDetector.h"
#include "core/text/VietnameseText.h"

namespace lankey::core::pipeline {

using model::EngineResult;
using model::FocusContext;
using model::KeyEvent;
using model::ReplacementReason;
using model::Suggestion;
using model::SuggestionQuery;
using model::Syllable;
using model::SyllableCommitted;
using model::TextReplacement;
using model::Thresholds;
using model::VirtualKey;

InputPipeline::InputPipeline(Dependencies deps, Handlers handlers)
    : deps_(deps), handlers_(std::move(handlers)) {}

InputPipeline::InputPipeline(Dependencies deps, CommitHandler onCommit)
    : InputPipeline(deps, Handlers{std::move(onCommit), nullptr, nullptr}) {}

bool InputPipeline::onKey(const KeyEvent& key) noexcept {
    // Our own SendInput echoes and key-ups never touch the composition and never bump the
    // generation: a key-up is not "the user typed something".
    if (key.injectedBySelf) return false;
    if (!key.isDown) {
        // The release of Tab/arrows/Esc we swallowed must not reach the app either: some
        // controls act on key-up (buttons, menus, games).
        if (swallowNextKeyUp_) {
            swallowNextKeyUp_ = false;
            return true;
        }
        return false;
    }
    ++stats_.keys;
    const std::uint64_t generation = buffer_.bump();
    if (!vietnameseEnabled()) {
        // English mode: transparent. Keep nothing, so switching back starts clean.
        if (!window_.empty() || !popup_.items.empty() || !pending_.items.empty()) {
            resetContextNoexcept();
        }
        return false;
    }
    try {
        return handleKey(key, generation);
    } catch (...) {
        // Nothing may escape the hook callback. Losing the current syllable is the least
        // bad outcome; the key goes through so the user does not lose the keystroke.
        ++stats_.exceptionsSwallowed;
        resetContextNoexcept();
        return false;
    }
}

bool InputPipeline::handleKey(const KeyEvent& key, std::uint64_t generation) {
    // Ctrl/Alt/Win combinations belong to the application (shortcuts, Alt+Tab). The
    // composition cannot be trusted afterwards, so start over. (Ctrl+Z as AutoCorrect Undo
    // is handled before reaching here once that feature exists.)
    if (key.hasSystemModifier()) {
        resetContext();
        return false;
    }

    // Popup keys are swallowed ONLY while the popup is showing; otherwise Tab/arrows/Esc go
    // straight to the application like any other key.
    if (!popup_.items.empty() && handlePopupKey(key, generation)) {
        swallowNextKeyUp_ = true;
        return true;
    }

    const Boundary boundary = WordBoundaryDetector::classify(key);
    // The syllable as it was BEFORE this key: a boundary key commits it, and the engine's
    // own result for a boundary key is already empty. Only the boundary path (below) pays
    // for a copy; the per-keystroke path just remembers whether it was empty.
    const bool composedWasEmpty = buffer_.composed().text.empty();

    EngineResult result = deps_.engine.process(key);
    const bool swallow = applyEngineResult(result, generation);

    if (boundary == Boundary::None) {
        // Backspace with nothing being composed deletes into already-committed text; the
        // phrase context is no longer what the screen shows. Drop it rather than learn a
        // phrase that never existed. (ManualCorrectionDetector will refine this later.)
        if (key.key == VirtualKey::Backspace && composedWasEmpty) {
            window_.reset();
        }
        buffer_.setComposed(std::move(result.composed));
        window_.current = buffer_.composed().text;
        window_.generation = generation;
        refreshSuggestions(generation, /*afterWordBoundary=*/false);
        return swallow;
    }
    const model::ComposedText before = buffer_.composed();

    // Boundary: figure out what actually ended up on screen for the syllable.
    std::u32string text;
    bool transformApplied = false;
    if (result.action == EngineResult::Action::Replace) {
        // The engine rewrote the syllable at the boundary (e.g. restored raw keys because
        // it was not valid Vietnamese: "úe" + space -> "user "). The insert includes the
        // terminator; strip it to get the syllable.
        text = result.insert;
        if (key.unicode != 0 && !text.empty() && text.back() == key.unicode) {
            text.pop_back();
        }
        transformApplied = result.composed.vietnameseTransformApplied;
    } else {
        text = before.text;
        transformApplied = before.vietnameseTransformApplied;
    }

    if (!text.empty()) {
        commitSyllable(text, transformApplied, WordBoundaryDetector::terminatorFor(key),
                       generation);
    }
    if (boundary == Boundary::Paragraph) {
        window_.reset();
        deps_.engine.reset();
    }
    buffer_.clearComposed();
    window_.current.clear();
    window_.generation = generation;
    popupSuppressed_ = false;
    hidePopup();
    clearPending();
    // A space is where the next word starts: predict it. Punctuation, Enter and
    // navigation end the thought, so nothing is proposed there.
    if (key.key == VirtualKey::Space && !window_.committed.empty()) {
        refreshSuggestions(generation, /*afterWordBoundary=*/true);
    }
    return swallow;
}

bool InputPipeline::handlePopupKey(const KeyEvent& key, std::uint64_t generation) {
    const int count = static_cast<int>(popup_.items.size());
    switch (key.key) {
    case VirtualKey::Tab:
    case VirtualKey::Enter:
        selectSuggestion(static_cast<std::size_t>(popup_.selected), generation);
        return true;
    case VirtualKey::ArrowDown:
        popup_.selected = (popup_.selected + 1) % count;
        popup_.generation = generation;
        if (handlers_.onPopup) handlers_.onPopup(popup_);
        return true;
    case VirtualKey::ArrowUp:
        popup_.selected = (popup_.selected + count - 1) % count;
        popup_.generation = generation;
        if (handlers_.onPopup) handlers_.onPopup(popup_);
        return true;
    case VirtualKey::Escape:
        // Dismiss and stay quiet until this syllable ends: the user said "not now".
        popupSuppressed_ = true;
        hidePopup();
        clearPending();
        return true;
    default:
        return false;
    }
}

bool InputPipeline::applyEngineResult(const EngineResult& result, std::uint64_t generation) {
    switch (result.action) {
    case EngineResult::Action::PassThrough:
        return false;
    case EngineResult::Action::Swallow:
        return true;
    case EngineResult::Action::Replace: {
        TextReplacement cmd;
        cmd.deleteCount = result.deleteCount;
        cmd.insert = result.insert;
        cmd.expectedGeneration = generation;
        cmd.reason = ReplacementReason::Engine;
        deps_.sink.apply(cmd);
        ++stats_.replacementsApplied;
        return true;
    }
    }
    return false;
}

void InputPipeline::commitSyllable(const std::u32string& text, bool transformApplied,
                                   char32_t terminator, std::uint64_t generation) {
    window_.commit(Syllable::fromComposed(text));
    ++stats_.commits;
    if (!handlers_.onCommit) {
        return;
    }
    SyllableCommitted event;
    event.window = window_; // copy: the worker must never see our live window
    event.terminator = terminator;
    event.vietnameseTransformApplied = transformApplied;
    if (const auto focus = deps_.focus.current()) {
        event.focus = *focus;
    }
    event.timestampMs = deps_.clock.nowMonotonicMs();
    event.generation = generation;
    handlers_.onCommit(std::move(event)); // one copy in total: the window copy above
}

void InputPipeline::refreshSuggestions(std::uint64_t generation, bool afterWordBoundary) {
    if (deps_.suggestions == nullptr || popupSuppressed_) {
        hidePopup();
        clearPending();
        return;
    }
    if (!afterWordBoundary && window_.current.empty()) {
        hidePopup();
        clearPending();
        return;
    }
    const auto focus = deps_.focus.current();
    if (focus && focus->isPasswordField) {
        hidePopup();
        clearPending();
        return;
    }

    SuggestionQuery query;
    const std::size_t contextCount =
        std::min<std::size_t>(window_.committed.size(),
                              static_cast<std::size_t>(Thresholds::kSuggestionContextSyllables));
    query.context.reserve(contextCount);
    for (std::size_t i = window_.committed.size() - contextCount; i < window_.committed.size();
         ++i) {
        query.context.emplace_back(window_.committed[i].text); // view, no copy
    }
    if (!afterWordBoundary) {
        query.typed = window_.current;
        query.prefix = text::caseFold(text::nfc(window_.current));
    }
    if (focus) query.appName = focus->appName;

    model::SuggestionList items = deps_.suggestions->suggest(query);
    if (items.empty()) {
        hidePopup();
        clearPending();
        return;
    }
    if (!popup_.items.empty()) {
        // Already showing: follow the typing immediately, no second wait.
        popup_.items = std::move(items);
        popup_.selected = 0;
        popup_.generation = generation;
        popup_.pending = false;
        if (handlers_.onPopup) handlers_.onPopup(popup_);
        return;
    }
    // Not showing: park the list and let the UI start the idle timer.
    pending_.items = std::move(items);
    pending_.selected = 0;
    pending_.generation = generation;
    pending_.pending = true;
    if (handlers_.onPopup) handlers_.onPopup(pending_);
}

void InputPipeline::promotePending(std::uint64_t generation) {
    // Only the list computed at exactly this generation, and only if nothing happened
    // since (no key, no click, no focus change): otherwise it is stale by definition.
    if (pending_.items.empty() || pending_.generation != generation ||
        buffer_.generation() != generation) {
        return;
    }
    popup_ = std::move(pending_);
    popup_.pending = false;
    pending_ = PopupState{};
    ++stats_.suggestionsShown;
    if (handlers_.onPopup) handlers_.onPopup(popup_);
}

void InputPipeline::selectSuggestion(std::size_t index, std::uint64_t generation) {
    if (index >= popup_.items.size()) return;
    const Suggestion chosen = popup_.items[index];

    TextReplacement cmd;
    cmd.deleteCount = chosen.deleteCount;
    // A trailing space: the phrase is complete, and it is what lets the next prediction
    // chain straight on (Tab, Tab, Tab through a long phrase).
    cmd.insert = chosen.insert + U' ';
    cmd.expectedGeneration = generation;
    cmd.reason = ReplacementReason::Suggestion;
    deps_.sink.apply(cmd);
    ++stats_.replacementsApplied;
    ++stats_.suggestionsSelected;

    // The whole phrase is now on screen. The syllables the insert added (everything after
    // the context that was already committed) become committed context, so the next
    // suggestion/correction sees "hệ điều hành", not "hệ".
    std::size_t inserted = 1;
    for (const char32_t c : chosen.insert) {
        if (c == U' ') ++inserted;
    }
    const auto& syllables = chosen.phrase.syllables;
    const std::size_t first = syllables.size() > inserted ? syllables.size() - inserted : 0;
    for (std::size_t i = first; i < syllables.size(); ++i) {
        window_.commit(syllables[i]);
    }
    // No SyllableCommitted per syllable: that would count the phrase as typed. The
    // selection handler records it once, with the higher selection weight.
    if (handlers_.onSelection) handlers_.onSelection(chosen.phrase);

    deps_.engine.reset();
    buffer_.clearComposed();
    window_.current.clear();
    window_.generation = generation;
    popupSuppressed_ = false;
    hidePopup();
    clearPending();
    // Chain: what usually follows the phrase just chosen?
    refreshSuggestions(generation, /*afterWordBoundary=*/true);
}

void InputPipeline::hidePopup() {
    if (popup_.items.empty()) return;
    popup_ = PopupState{};
    if (handlers_.onPopup) handlers_.onPopup(popup_);
}

void InputPipeline::clearPending() {
    if (pending_.items.empty()) return;
    pending_ = PopupState{};
    // An empty, pending=false state tells the UI to cancel its timer (and hide, harmless).
    if (handlers_.onPopup) handlers_.onPopup(pending_);
}

void InputPipeline::onFocusChanged(const FocusContext& /*focus*/) {
    buffer_.bump();
    resetContext();
}

void InputPipeline::onPointerClick() {
    buffer_.bump();
    resetContext();
}

void InputPipeline::resetContext() {
    deps_.engine.reset();
    buffer_.clearComposed();
    window_.reset();
    popupSuppressed_ = false;
    swallowNextKeyUp_ = false;
    hidePopup();
    clearPending();
}

void InputPipeline::resetContextNoexcept() noexcept {
    try {
        resetContext();
    } catch (...) {
        // The engine failed even to reset. Nothing more can be done from the hook thread;
        // the next key will try again. Counted so it shows up in diagnostics.
        ++stats_.exceptionsSwallowed;
    }
}

bool InputPipeline::applyReplacement(const TextReplacement& replacement) {
    if (!buffer_.matches(replacement.expectedGeneration)) {
        ++stats_.replacementsDroppedByGeneration;
        return false;
    }
    deps_.sink.apply(replacement);
    ++stats_.replacementsApplied;
    // The screen changed under us; anything else computed against the old generation is
    // now stale too.
    buffer_.bump();
    return true;
}

} // namespace lankey::core::pipeline
