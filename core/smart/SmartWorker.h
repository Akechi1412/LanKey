#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <semaphore>
#include <thread>
#include <variant>

#include "core/interfaces/IClock.h"
#include "core/interfaces/ILexiconStore.h"
#include "core/model/AtomicSnapshot.h"
#include "core/model/Phrase.h"
#include "core/model/Settings.h"
#include "core/model/SyllableCommitted.h"
#include "core/model/Thresholds.h"
#include "core/smart/learn/LearningRecorder.h"
#include "core/smart/privacy/PrivacyFilter.h"
#include "core/smart/suggest/SuggestionEngine.h"
#include "core/threading/SpscQueue.h"
#include "core/threading/TaskThread.h"

namespace lankey::core::smart {

// Owns the worker thread and the DB thread (PLAN 7.2) and wires the Smart Layer together:
//
//   hook thread  --SyllableCommitted / selected Phrase-->  [SPSC queue]
//   worker       PrivacyFilter -> LearningRecorder -> (flush) -> DB thread: applyDeltas
//                every kSnapshotRebuildIntervalMs, if anything changed:
//                DB thread: loadAll -> SuggestionEngine::buildSnapshot -> publish
//   DB thread    also: initial load, eraseAll, cleanup
//
// Threading: onCommit()/onSelection() are hook-thread safe (lock-free push + semaphore
// release). Everything else may be called from the UI/main thread.
class SmartWorker {
public:
    struct Dependencies {
        ILexiconStore& store;
        SuggestionEngine& suggestions;
        IClock& clock;
    };

    struct Stats {
        std::atomic<std::uint64_t> eventsQueued{0};
        std::atomic<std::uint64_t> eventsDropped{0}; // queue full
        std::atomic<std::uint64_t> eventsProcessed{0};
        std::atomic<std::uint64_t> rejectedByPrivacy{0};
        std::atomic<std::uint64_t> flushes{0};
        std::atomic<std::uint64_t> rebuilds{0};
        std::atomic<std::uint64_t> storeErrors{0};
    };

    SmartWorker(Dependencies deps, const model::Settings& settings);
    ~SmartWorker();

    SmartWorker(const SmartWorker&) = delete;
    SmartWorker& operator=(const SmartWorker&) = delete;

    void start();
    // Flushes pending learning to the store, then joins both threads.
    void stop();

    // Hook thread.
    void onCommit(model::SyllableCommitted&& committed) noexcept;
    void onSelection(const model::Phrase& phrase) noexcept;

    // UI / main thread.
    void updateSettings(const model::Settings& settings);
    // "Delete all my data": store.eraseAll() on the DB thread, then an empty snapshot.
    void eraseAllData(std::function<void(lk::expected<void>)> done);
    // Force a snapshot rebuild now (tests, after import).
    void requestRebuild();
    // Blocks until the DB thread has run everything posted so far (tests).
    void drainForTests();

    [[nodiscard]] const Stats& stats() const noexcept { return stats_; }

private:
    using Event = std::variant<model::SyllableCommitted, model::Phrase>;

    void run();
    void handle(const Event& event);
    void flushToStore(model::LexiconDeltaBatch batch);
    void rebuildSnapshot();
    // Age out stale rows on the DB thread; the next rebuild picks up the smaller table.
    void runCleanup();

    Dependencies deps_;
    model::AtomicSnapshot<model::Settings> settings_;
    std::atomic<bool> settingsChanged_{true};

    threading::SpscQueue<Event, model::Thresholds::kHookToWorkerQueueSize> queue_;
    std::counting_semaphore<> signal_{0};
    std::atomic<bool> running_{false};
    std::thread worker_;
    threading::TaskThread db_{"lankey-db"};

    // Worker-thread state.
    std::unique_ptr<PrivacyFilter> privacy_;
    std::unique_ptr<LearningRecorder> recorder_;
    std::atomic<bool> dirty_{false};
    std::atomic<bool> rebuildRequested_{false};
    std::int64_t lastRebuildMs_ = 0;
    std::int64_t lastCleanupMs_ = 0;
    std::atomic<std::uint64_t> cycles_{0}; // loop iterations, for drainForTests()

    Stats stats_;
};

} // namespace lankey::core::smart
