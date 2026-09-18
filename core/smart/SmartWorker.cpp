#include "core/smart/SmartWorker.h"

#include <chrono>
#include <utility>

#include "core/smart/correct/BaseSyllableSet.h"
#include "core/smart/correct/ManualCorrectionDetector.h"

namespace lankey::core::smart {

using model::LexiconDeltaBatch;
using model::Settings;
using model::Thresholds;

SmartWorker::SmartWorker(Dependencies deps, const Settings& settings) : deps_(deps) {
    settings_.store(std::make_shared<const Settings>(settings));
}

SmartWorker::~SmartWorker() {
    stop();
}

void SmartWorker::start() {
    if (running_.exchange(true)) return;
    db_.start();
    // The base fuzzy index (~7k inserts) is built once, off the UI thread, before the first
    // snapshot: typing works immediately, AutoCorrect a moment later.
    db_.post([this] { deps_.corrector.publishBase(BaseIndex::build(BaseSyllableSet::builtin())); });
    // Whatever was learned before is available as soon as the DB thread has read it; typing
    // works meanwhile (PLAN 7.4: hook first, suggestions a few hundred ms later).
    rebuildRequested_.store(true);
    worker_ = std::thread([this] { run(); });
}

void SmartWorker::stop() {
    if (!running_.exchange(false)) return;
    signal_.release();
    if (worker_.joinable()) worker_.join();
    // run() flushed the recorder on exit; let the DB thread write it before we go.
    db_.stop();
}

template <class E>
void SmartWorker::enqueue(E&& event) noexcept {
    if (queue_.tryPush(Event{std::forward<E>(event)})) {
        stats_.eventsQueued.fetch_add(1, std::memory_order_relaxed);
        signal_.release();
    } else {
        // Full queue: losing a learning event is fine, blocking the hook thread is not.
        stats_.eventsDropped.fetch_add(1, std::memory_order_relaxed);
    }
}

void SmartWorker::onCommit(model::SyllableCommitted&& committed) noexcept {
    enqueue(std::move(committed));
}

void SmartWorker::onSelection(const model::Phrase& phrase) noexcept {
    enqueue(phrase);
}

void SmartWorker::onCorrectionApplied(const std::u32string& wrongKey) noexcept {
    enqueue(CorrectionApplied{wrongKey});
}

void SmartWorker::onCorrectionRejected(const std::u32string& wrongKey,
                                       const std::u32string& correctKey) noexcept {
    enqueue(CorrectionRejected{wrongKey, correctKey});
}

void SmartWorker::updateSettings(const Settings& settings) {
    settings_.store(std::make_shared<const Settings>(settings));
    settingsChanged_.store(true);
    deps_.suggestions.setSettings(settings.suggestions);
    deps_.corrector.setSettings(settings.autoCorrect);
    signal_.release();
}

void SmartWorker::eraseAllData(std::function<void(lk::expected<void>)> done) {
    db_.post([this, done = std::move(done)] {
        auto r = deps_.store.eraseAll();
        if (!r) stats_.storeErrors.fetch_add(1);
        deps_.suggestions.publish(std::make_shared<const PhraseTrie>());
        deps_.corrector.publish(std::make_shared<const CorrectionSnapshot>());
        if (done) done(std::move(r));
    });
}

void SmartWorker::requestRebuild() {
    rebuildRequested_.store(true);
    signal_.release();
}

void SmartWorker::drainForTests() {
    // Wait for one full worker cycle (so everything queued so far has been handled and any
    // DB work it produced has been posted), then let the DB thread run it all.
    const std::uint64_t target = cycles_.load(std::memory_order_acquire) + 1;
    signal_.release();
    while (cycles_.load(std::memory_order_acquire) < target &&
           running_.load(std::memory_order_acquire)) {
        std::this_thread::yield();
    }
    db_.drain();
}

void SmartWorker::run() {
    recorder_ = std::make_unique<LearningRecorder>(
        deps_.clock, [this](LexiconDeltaBatch batch) { flushToStore(std::move(batch)); });
    lastRebuildMs_ = deps_.clock.nowMonotonicMs();
    // Start-up: age out what nobody has typed in months BEFORE the first snapshot, so a
    // long-lived install does not grow without bound (cleanup is otherwise daily).
    runCleanup();
    lastCleanupMs_ = lastRebuildMs_;
    const auto applySettings = [this] {
        const auto s = settings_.load();
        privacy_ = std::make_unique<PrivacyFilter>(PrivacyFilter::standard(s->privacy));
    };
    // Before the first wait: stop() may come before the first wake-up and the shutdown
    // drain below still needs a filter.
    settingsChanged_.store(false);
    applySettings();

    while (running_.load(std::memory_order_acquire)) {
        // Wake on events, or every 500 ms to run the timers.
        (void)signal_.try_acquire_for(std::chrono::milliseconds(500));

        if (settingsChanged_.exchange(false)) applySettings();

        while (auto event = queue_.tryPop()) {
            handle(*event);
            stats_.eventsProcessed.fetch_add(1, std::memory_order_relaxed);
        }

        recorder_->flushIfDue();

        const std::int64_t now = deps_.clock.nowMonotonicMs();
        if (now - lastCleanupMs_ >= Thresholds::kCleanupIntervalMs) {
            runCleanup();
            lastCleanupMs_ = now;
        }
        const bool due = now - lastRebuildMs_ >= Thresholds::kSnapshotRebuildIntervalMs;
        if (rebuildRequested_.exchange(false) || (dirty_.load() && due)) {
            rebuildSnapshot();
            lastRebuildMs_ = now;
        }
        cycles_.fetch_add(1, std::memory_order_release);
    }
    // Shutdown: nothing the hook thread already handed us may be lost.
    while (auto event = queue_.tryPop()) {
        handle(*event);
        stats_.eventsProcessed.fetch_add(1, std::memory_order_relaxed);
    }
    recorder_->flush();
}

void SmartWorker::handle(const Event& event) {
    if (const auto* committed = std::get_if<model::SyllableCommitted>(&event)) {
        if (privacy_->evaluate(*committed) == PrivacyVerdict::Reject) {
            stats_.rejectedByPrivacy.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        handleCommit(*committed);
    } else if (const auto* phrase = std::get_if<model::Phrase>(&event)) {
        recorder_->recordSelection(*phrase);
    } else if (const auto* applied = std::get_if<CorrectionApplied>(&event)) {
        db_.post([this, key = applied->wrongKey] {
            if (!deps_.store.noteCorrectionApplied(key)) stats_.storeErrors.fetch_add(1);
        });
    } else if (const auto* rejected = std::get_if<CorrectionRejected>(&event)) {
        stats_.correctionsRejected.fetch_add(1, std::memory_order_relaxed);
        // The user meant what they typed: learn it, and take back what we put in.
        recorder_->adjust(model::Phrase::fromJoined(rejected->wrongKey), 1);
        recorder_->adjust(model::Phrase::fromJoined(rejected->correctKey), -1);
        db_.post([this, key = rejected->wrongKey] {
            const auto r = deps_.store.rejectCorrection(key, Thresholds::kCorrectionRejectPenalty,
                                                        deps_.clock.nowUnixSeconds());
            if (!r) stats_.storeErrors.fetch_add(1);
        });
        // The penalty (or the blacklist) must take effect before the same fix happens
        // again, not 30 s from now.
        requestRebuild();
    }
}

void SmartWorker::handleCommit(const model::SyllableCommitted& committed) {
    if (committed.uncertain) return; // maybe only the tail of a word: neither learn nor fix
    // A retype the hook thread recognised: did the user fix a spelling?
    if (!committed.retypedFrom.empty()) {
        if (const auto fix =
                ManualCorrectionDetector::detect(committed, BaseSyllableSet::builtin())) {
            stats_.manualFixesLearned.fetch_add(1, std::memory_order_relaxed);
            db_.post([this, fix = *fix] {
                const auto r = deps_.store.reinforceCorrection(
                    fix.wrong, fix.correct, Thresholds::kCorrectionLearnStep,
                    model::CorrectionRuleSource::Learned, deps_.clock.nowUnixSeconds());
                if (!r) stats_.storeErrors.fetch_add(1);
            });
            requestRebuild();
        }
    }

    // AutoCorrect first: when a correction is proposed, what gets learned is the corrected
    // phrase, not the typo - otherwise the typo would become "trusted" after a few
    // repetitions and the correction would silence itself.
    std::optional<model::Correction> correction = deps_.corrector.check(committed);
    if (correction && onCorrection_) {
        stats_.correctionsProposed.fetch_add(1, std::memory_order_relaxed);
        model::SyllableCommitted corrected = committed;
        auto& window = corrected.window.committed;
        const auto k = static_cast<std::size_t>(correction->syllableCount);
        if (k <= window.size() && correction->corrected.size() == k) {
            for (std::size_t i = 0; i < k; ++i) {
                window[window.size() - k + i].text = correction->corrected[i].text;
            }
        }
        recorder_->record(corrected);
        onCorrection_(std::move(*correction));
        return;
    }
    recorder_->record(committed);
}

void SmartWorker::flushToStore(LexiconDeltaBatch batch) {
    stats_.flushes.fetch_add(1, std::memory_order_relaxed);
    dirty_.store(true);
    db_.post([this, batch = std::move(batch)] {
        if (!deps_.store.applyDeltas(batch)) stats_.storeErrors.fetch_add(1);
    });
}

void SmartWorker::runCleanup() {
    db_.post([this] {
        const auto removed = deps_.store.cleanup(deps_.clock.nowUnixSeconds());
        if (!removed) {
            stats_.storeErrors.fetch_add(1);
            return;
        }
        if (*removed > 0) dirty_.store(true); // the snapshot still holds the removed rows
    });
}

void SmartWorker::rebuildSnapshot() {
    dirty_.store(false);
    db_.post([this] {
        auto all = deps_.store.loadAll();
        if (!all) {
            stats_.storeErrors.fetch_add(1);
            return;
        }
        const auto s = settings_.load();
        deps_.suggestions.publish(
            SuggestionEngine::buildSnapshot(*all, deps_.clock.nowUnixSeconds(), s->suggestions));
        auto rules = deps_.store.loadCorrections();
        auto blacklist = deps_.store.loadBlacklist();
        if (!rules || !blacklist) {
            stats_.storeErrors.fetch_add(1);
        } else {
            deps_.corrector.publish(CorrectionSnapshot::build(*all, *rules, *blacklist,
                                                              BaseSyllableSet::builtin(),
                                                              deps_.clock.nowUnixSeconds()));
        }
        stats_.rebuilds.fetch_add(1, std::memory_order_relaxed);
    });
}

} // namespace lankey::core::smart
