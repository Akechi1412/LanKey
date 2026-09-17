#include "core/smart/SmartWorker.h"

#include <chrono>
#include <utility>

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

void SmartWorker::onCommit(model::SyllableCommitted&& committed) noexcept {
    if (queue_.tryPush(Event{std::move(committed)})) {
        stats_.eventsQueued.fetch_add(1, std::memory_order_relaxed);
        signal_.release();
    } else {
        // Full queue: losing a learning event is fine, blocking the hook thread is not.
        stats_.eventsDropped.fetch_add(1, std::memory_order_relaxed);
    }
}

void SmartWorker::onSelection(const model::Phrase& phrase) noexcept {
    if (queue_.tryPush(Event{phrase})) {
        stats_.eventsQueued.fetch_add(1, std::memory_order_relaxed);
        signal_.release();
    } else {
        stats_.eventsDropped.fetch_add(1, std::memory_order_relaxed);
    }
}

void SmartWorker::updateSettings(const Settings& settings) {
    settings_.store(std::make_shared<const Settings>(settings));
    settingsChanged_.store(true);
    deps_.suggestions.setSettings(settings.suggestions);
    signal_.release();
}

void SmartWorker::eraseAllData(std::function<void(lk::expected<void>)> done) {
    db_.post([this, done = std::move(done)] {
        auto r = deps_.store.eraseAll();
        if (!r) stats_.storeErrors.fetch_add(1);
        deps_.suggestions.publish(std::make_shared<const PhraseTrie>());
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
        recorder_->record(*committed);
    } else if (const auto* phrase = std::get_if<model::Phrase>(&event)) {
        recorder_->recordSelection(*phrase);
    }
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
        stats_.rebuilds.fetch_add(1, std::memory_order_relaxed);
    });
}

} // namespace lankey::core::smart
