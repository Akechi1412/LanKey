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
#include "core/model/Correction.h"
#include "core/model/Phrase.h"
#include "core/model/Settings.h"
#include "core/model/SyllableCommitted.h"
#include "core/model/Thresholds.h"
#include "core/smart/correct/AutoCorrectEngine.h"
#include "core/smart/learn/LearningRecorder.h"
#include "core/smart/privacy/PrivacyFilter.h"
#include "core/smart/suggest/SuggestionEngine.h"
#include "core/threading/SpscQueue.h"
#include "core/threading/TaskThread.h"

namespace lankey::core::smart {

// Owns the worker thread and the DB thread (PLAN 7.2) and wires the Smart Layer together:
//
//   hook thread  --SyllableCommitted / selected Phrase / undo-->  [SPSC queue]
//   worker       PrivacyFilter -> AutoCorrectEngine::check -> onCorrection (back to hook)
//                             -> LearningRecorder (the corrected phrase, if any)
//                             -> ManualCorrectionDetector -> DB: correction_map
//                every kSnapshotRebuildIntervalMs, if anything changed:
//                DB thread: loadAll (+corrections, blacklist) -> snapshots -> publish
//   DB thread    also: initial load, base fuzzy index, eraseAll, cleanup
//
// Threading: onCommit()/onSelection() are hook-thread safe (lock-free push + semaphore
// release). Everything else may be called from the UI/main thread.
class SmartWorker {
public:
    struct Dependencies {
        ILexiconStore& store;
        SuggestionEngine& suggestions;
        AutoCorrectEngine& corrector;
        IClock& clock;
    };

    // Delivered on the worker thread; the receiver hands it to the hook thread.
    using CorrectionHandler = std::function<void(model::Correction)>;

    struct Stats {
        std::atomic<std::uint64_t> eventsQueued{0};
        std::atomic<std::uint64_t> eventsDropped{0}; // queue full
        std::atomic<std::uint64_t> eventsProcessed{0};
        std::atomic<std::uint64_t> rejectedByPrivacy{0};
        std::atomic<std::uint64_t> flushes{0};
        std::atomic<std::uint64_t> rebuilds{0};
        std::atomic<std::uint64_t> storeErrors{0};
        std::atomic<std::uint64_t> correctionsProposed{0};
        std::atomic<std::uint64_t> manualFixesLearned{0};
        std::atomic<std::uint64_t> correctionsRejected{0};
    };

    SmartWorker(Dependencies deps, const model::Settings& settings);
    ~SmartWorker();

    SmartWorker(const SmartWorker&) = delete;
    SmartWorker& operator=(const SmartWorker&) = delete;

    // Before start(). Called on the worker thread whenever AutoCorrect proposes a change.
    void setCorrectionHandler(CorrectionHandler handler) { onCorrection_ = std::move(handler); }

    void start();
    // Flushes pending learning to the store, then joins both threads.
    void stop();

    // Hook thread.
    void onCommit(model::SyllableCommitted&& committed) noexcept;
    void onSelection(const model::Phrase& phrase) noexcept;
    void onCorrectionApplied(const std::u32string& wrongKey) noexcept;
    void onCorrectionRejected(const std::u32string& wrongKey,
                              const std::u32string& correctKey) noexcept;

    // UI / main thread.
    void updateSettings(const model::Settings& settings);
    // "Delete all my data": store.eraseAll() on the DB thread, then an empty snapshot.
    void eraseAllData(std::function<void(lk::expected<void>)> done);
    // Force a snapshot rebuild now (tests, after import).
    void requestRebuild();
    // Runs `work` on the DB thread with the store (Settings: read or edit the dictionary).
    // `work` must not block; hand results back through your own thread-safe channel.
    void withStore(std::function<void(ILexiconStore&)> work);
    // Blocks until the DB thread has run everything posted so far (tests).
    void drainForTests();

    [[nodiscard]] const Stats& stats() const noexcept { return stats_; }

private:
    struct CorrectionApplied {
        std::u32string wrongKey;
    };
    struct CorrectionRejected {
        std::u32string wrongKey;
        std::u32string correctKey;
    };
    using Event = std::variant<model::SyllableCommitted, model::Phrase, CorrectionApplied,
                               CorrectionRejected>;

    template <class E>
    void enqueue(E&& event) noexcept;

    void run();
    void handle(const Event& event);
    void handleCommit(const model::SyllableCommitted& committed);
    void flushToStore(model::LexiconDeltaBatch batch);
    void rebuildSnapshot();
    // Age out stale rows on the DB thread; the next rebuild picks up the smaller table.
    void runCleanup();

    Dependencies deps_;
    CorrectionHandler onCorrection_;
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
