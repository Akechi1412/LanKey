#include "core/pipeline/InputPipeline.h"

#include <algorithm>
#include <utility>

#include "core/model/Thresholds.h"
#include "core/pipeline/WordBoundaryDetector.h"
#include "core/text/VietnameseText.h"

namespace lankey::core::pipeline {

using model::Correction;
using model::EngineResult;
using model::FocusContext;
using model::KeyEvent;
using model::Modifier;
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
    : InputPipeline(deps, Handlers{std::move(onCommit), nullptr, nullptr, nullptr, nullptr}) {}

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
    // The key right after an AutoCorrect: Backspace or Ctrl+Z means "no, what I typed".
    // Checked before anything else - Ctrl+Z is the one Ctrl combination we take.
    if (tryUndoCorrection(key, generation)) {
        swallowNextKeyUp_ = true;
        return true;
    }
    lastCorrection_.reset(); // any other key closes the undo window

    // Ctrl/Alt/Win combinations belong to the application (shortcuts, Alt+Tab). The
    // composition cannot be trusted afterwards, so start over.
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
        if (key.key == VirtualKey::Backspace && composedWasEmpty) {
            // Deleting into already-committed text: the phrase context is no longer what
            // the screen shows. Drop it - but remember it: the rest of a half-deleted
            // syllable must be glued back, and this may be a manual fix worth learning.
            onBackspaceIntoCommitted();
        }
        buffer_.setComposed(std::move(result.composed));
        window_.current = buffer_.composed().text;
        if (retype_ && retype_->partial && retype_->recommitted == 0) {
            // The engine only sees what was typed after the deletion; the screen shows
            // the untouched head of the syllable in front of it.
            window_.current.insert(0, retype_->prefix);
        }
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
    } else if (key.unicode != 0 && !spans_.empty()) {
        // A second space, punctuation after a space: one more character on screen behind
        // the last syllable.
        ++spans_.back().trailing;
    }
    if (boundary == Boundary::Paragraph) {
        window_.reset();
        spans_.clear();
        abandonRetype();
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

void InputPipeline::commitSyllable(const std::u32string& composed, bool transformApplied,
                                   char32_t terminator, std::uint64_t generation) {
    // The whole syllable as it stands on screen: the head the user kept plus what the
    // engine composed after the deletion.
    std::u32string text;
    if (retype_ && retype_->partial && retype_->recommitted == 0) text = retype_->prefix;
    text += composed;

    window_.commit(Syllable::fromComposed(text));
    spans_.push_back({static_cast<int>(text.size()), terminator != 0 ? 1 : 0, terminator});
    trimSpans();
    ++stats_.commits;
    lastCommitMs_ = deps_.clock.nowMonotonicMs();
    const bool uncertain = uncertain_;
    uncertain_ = false;

    // A retype in progress: this may be the syllable that completes it.
    std::vector<Syllable> retypedFrom;
    if (retype_) {
        if (++retype_->recommitted == retype_->syllablesRemoved) {
            // The user deleted `syllablesRemoved` syllables (the last one maybe only in
            // part) and typed as many again. Put the untouched ones back in front so the
            // phrase context is whole again; hand the old ones to the worker when the edit
            // came soon enough after the commit to be a fix of it.
            const auto removed = static_cast<std::size_t>(retype_->syllablesRemoved);
            auto& old = retype_->window;
            if (retype_->learnable) {
                retypedFrom.assign(old.end() - static_cast<std::ptrdiff_t>(removed), old.end());
            }
            std::vector<Syllable> restored(old.begin(),
                                           old.end() - static_cast<std::ptrdiff_t>(removed));
            std::vector<Span> restoredSpans(retype_->spans.begin(),
                                            retype_->spans.end() -
                                                static_cast<std::ptrdiff_t>(removed));
            restored.insert(restored.end(), window_.committed.begin(), window_.committed.end());
            restoredSpans.insert(restoredSpans.end(), spans_.begin(), spans_.end());
            window_.committed = std::move(restored);
            spans_ = std::move(restoredSpans);
            while (window_.committed.size() >
                   static_cast<std::size_t>(Thresholds::kMaxPhraseSyllables)) {
                window_.committed.erase(window_.committed.begin());
            }
            trimSpans();
            abandonRetype();
            if (!retypedFrom.empty()) ++stats_.retypesDetected;
        }
    }

    if (!handlers_.onCommit) {
        return;
    }
    SyllableCommitted event;
    event.window = window_; // copy: the worker must never see our live window
    event.retypedFrom = std::move(retypedFrom);
    event.uncertain = uncertain;
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
        spans_.push_back({static_cast<int>(syllables[i].text.size()), 1, U' '});
    }
    trimSpans();
    abandonRetype();
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
    spans_.clear();
    abandonRetype();
    uncertain_ = false;
    lastCorrection_.reset();
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

void InputPipeline::trimSpans() {
    while (spans_.size() > window_.committed.size())
        spans_.erase(spans_.begin());
}

void InputPipeline::onBackspaceIntoCommitted() {
    const std::int64_t now = deps_.clock.nowMonotonicMs();
    // Deleting again after having retyped something: a different edit; start over from
    // what is on screen now.
    if (retype_ && retype_->recommitted > 0) abandonRetype();
    if (!retype_) {
        if (window_.committed.empty()) {
            // Text we never saw (typed before LanKey, another app, past the window): the
            // next syllable may be only the tail of a word.
            uncertain_ = true;
            return;
        }
        Retype r;
        r.window = std::move(window_.committed);
        r.spans = std::move(spans_);
        // Only a deletion that starts soon after the commit can be a fix of that commit.
        r.learnable = now - lastCommitMs_ <= Thresholds::kRetypeWindowMs;
        retype_ = std::move(r);
        window_.reset();
        spans_.clear();
    }
    ++retype_->deleted;
    // Where does the deletion sit now? Walk back over the spans: on a syllable edge, or
    // inside a syllable (its remaining head becomes `prefix`).
    retype_->syllablesRemoved = 0;
    retype_->partial = false;
    retype_->prefix.clear();
    int rest = retype_->deleted;
    const auto n = static_cast<int>(retype_->spans.size());
    for (int i = n; i-- > 0;) {
        const Span& span = retype_->spans[static_cast<std::size_t>(i)];
        const int total = span.typedLength + span.trailing;
        if (rest == total) {
            retype_->syllablesRemoved = n - i;
            return;
        }
        if (rest < total) {
            const int intoTyped = std::max(0, rest - span.trailing);
            const auto& typed = retype_->window[static_cast<std::size_t>(i)].typed;
            retype_->partial = true;
            retype_->prefix = typed.substr(0, typed.size() - static_cast<std::size_t>(intoTyped));
            retype_->syllablesRemoved = n - i; // this one will be retyped from `prefix`
            return;
        }
        rest -= total;
    }
    abandonRetype(); // deleted past everything we knew about
    uncertain_ = true;
}

bool InputPipeline::tryUndoCorrection(const KeyEvent& key, std::uint64_t generation) {
    if (!lastCorrection_) return false;
    const bool ctrlZ = key.key == VirtualKey::Z && has(key.modifiers, Modifier::Control) &&
                       !has(key.modifiers, Modifier::Alt) && !has(key.modifiers, Modifier::Win);
    const bool backspace = key.key == VirtualKey::Backspace && !key.hasSystemModifier();
    if (!ctrlZ && !backspace) return false;
    const auto& c = *lastCorrection_;
    // "The very next key": this key bumped the generation exactly once past the apply.
    if (generation != c.generation + 1) return false;
    if (deps_.clock.nowMonotonicMs() - c.appliedMs > Thresholds::kUndoWindowMs) return false;

    TextReplacement cmd;
    cmd.deleteCount = static_cast<int>(c.insertedText.size()) + (c.terminator != 0 ? 1 : 0);
    for (std::size_t i = 0; i < c.original.size(); ++i) {
        if (i > 0) cmd.insert.push_back(c.originalSpans[i - 1].terminator);
        cmd.insert += c.original[i].typed;
    }
    if (c.terminator != 0) cmd.insert.push_back(c.terminator);
    cmd.expectedGeneration = generation;
    cmd.reason = ReplacementReason::Undo;
    deps_.sink.apply(cmd);
    ++stats_.replacementsApplied;
    ++stats_.correctionsUndone;

    // The window goes back to what the user typed.
    const std::size_t n = window_.committed.size();
    const std::size_t k = c.original.size();
    if (k <= n) {
        for (std::size_t i = 0; i < k; ++i) {
            window_.committed[n - k + i] = c.original[i];
            spans_[n - k + i] = c.originalSpans[i];
        }
    }
    if (handlers_.onCorrectionRejected) {
        handlers_.onCorrectionRejected(c.wrongKey, text::caseFold(c.insertedText));
    }
    if (!recent_.empty()) {
        const std::size_t last = (recentNext_ + recent_.size() - 1) % recent_.size();
        if (recent_[last].corrected == c.insertedText) recent_[last].undone = true;
    }
    lastCorrection_.reset();
    hidePopup();
    clearPending();
    if (handlers_.onPopup) handlers_.onPopup(PopupState{}); // takes the notice down too
    return true;
}

bool InputPipeline::applyCorrection(const Correction& correction) {
    if (!buffer_.matches(correction.expectedGeneration)) {
        ++stats_.correctionsDropped;
        return false;
    }
    const auto k = static_cast<std::size_t>(correction.syllableCount);
    const std::size_t n = window_.committed.size();
    if (k == 0 || k > n || correction.corrected.size() != k || spans_.size() != n) {
        ++stats_.correctionsDropped;
        return false;
    }
    // Rebuild the exact span on screen: syllable, separator, syllable ... terminator. A
    // separator wider than one character (", ") cannot be retyped faithfully: give up.
    std::u32string original;
    std::u32string inserted;
    for (std::size_t i = n - k; i < n; ++i) {
        if (spans_[i].trailing != 1 || spans_[i].terminator == 0) {
            ++stats_.correctionsDropped;
            return false;
        }
        if (i > n - k) {
            original.push_back(spans_[i - 1].terminator);
            inserted.push_back(spans_[i - 1].terminator);
        }
        original += window_.committed[i].typed;
        inserted +=
            text::applyCasing(window_.committed[i].typed, correction.corrected[i - (n - k)].text);
    }
    if (inserted == original) return false; // a rule that changes nothing
    const char32_t terminator = spans_[n - 1].terminator;

    TextReplacement cmd;
    cmd.deleteCount = static_cast<int>(original.size()) + 1;
    cmd.insert = inserted;
    cmd.insert.push_back(terminator);
    cmd.expectedGeneration = correction.expectedGeneration;
    cmd.reason = ReplacementReason::AutoCorrect;
    deps_.sink.apply(cmd);
    ++stats_.replacementsApplied;
    ++stats_.correctionsApplied;

    AppliedCorrection applied;
    applied.original.assign(window_.committed.begin() + static_cast<std::ptrdiff_t>(n - k),
                            window_.committed.end());
    applied.originalSpans.assign(spans_.begin() + static_cast<std::ptrdiff_t>(n - k), spans_.end());
    applied.insertedText = inserted;
    applied.terminator = terminator;
    applied.wrongKey = correction.wrongKey;
    // Patch the window so learning and suggestions see the corrected phrase.
    for (std::size_t i = n - k; i < n; ++i) {
        auto& syl = window_.committed[i];
        syl.typed = text::applyCasing(syl.typed, correction.corrected[i - (n - k)].text);
        syl.text = correction.corrected[i - (n - k)].text;
        spans_[i].typedLength = static_cast<int>(syl.typed.size());
    }
    // The screen changed under everything computed so far.
    applied.generation = buffer_.bump();
    applied.appliedMs = deps_.clock.nowMonotonicMs();
    // Ring buffer for Undo diagnostics and the Settings page.
    RecentCorrection entry{original, inserted, applied.appliedMs, false};
    if (recent_.size() < static_cast<std::size_t>(Thresholds::kRecentCorrections)) {
        recent_.push_back(std::move(entry));
        recentNext_ = 0;
    } else {
        recent_[recentNext_] = std::move(entry);
        recentNext_ = (recentNext_ + 1) % recent_.size();
    }
    lastCorrection_ = std::move(applied);
    abandonRetype();
    hidePopup();
    clearPending();
    if (handlers_.onCorrectionApplied) handlers_.onCorrectionApplied(correction.wrongKey);
    if (handlers_.onPopup) {
        // Never correct silently: the UI shows "original -> corrected" for a moment.
        PopupState notice;
        notice.generation = buffer_.generation();
        notice.notice = original + U" \u2192 " + inserted;
        handlers_.onPopup(notice);
    }
    return true;
}

std::vector<InputPipeline::RecentCorrection> InputPipeline::recentCorrections() const {
    std::vector<RecentCorrection> out;
    out.reserve(recent_.size());
    for (std::size_t i = 0; i < recent_.size(); ++i) {
        // Newest first: walk backwards from the slot before recentNext_.
        const std::size_t idx = (recentNext_ + recent_.size() - 1 - i) % recent_.size();
        out.push_back(recent_[idx]);
    }
    return out;
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
