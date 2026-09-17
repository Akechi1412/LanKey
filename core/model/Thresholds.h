#pragma once

#include <cstdint>

namespace lankey::core::model {

// Every tunable threshold in one place. Values that users may change live in Settings
// and default to these; values that are architectural (phrase length, buffer sizes) only
// live here.
struct Thresholds {
    // Suggestions
    static constexpr int kMinPrefixForSuggest = 2;
    static constexpr int kLearnMinFrequency = 3; // entry becomes suggestable
    static constexpr int kMaxSuggestions = 5;
    static constexpr int kSuggestionContextSyllables = 4; // context words used for lookup
    static constexpr int kSuggestionIdleDelayMs = 350;    // show only after typing pauses
    static constexpr int kSuggestionCandidates = 20;      // best-first search cut-off

    // AutoCorrect
    static constexpr int kTrustMinFrequency = 5; // out-of-dictionary syllable is trusted
    static constexpr double kCorrectionApplyConfidence = 0.7;
    static constexpr int kUndoWindowMs = 3000;
    static constexpr int kRejectionsBeforeBlacklist = 2;

    // Phrase / syllable shape
    static constexpr int kMaxPhraseSyllables = 5;
    static constexpr int kMinSyllableLength = 2;
    static constexpr int kMaxSyllableLength = 30;
    static constexpr int kMaxCompositionLength = 64; // engine buffer guard

    // Storage
    static constexpr int kLexiconHardLimit = 100000;
    static constexpr int kFlushIntervalMs = 30000;
    static constexpr int kFlushBatchSize = 200;
    static constexpr int kSnapshotRebuildIntervalMs = 30000;
    static constexpr std::int64_t kCleanupIntervalMs = 24LL * 3600 * 1000; // age-out pass

    // Threading
    static constexpr int kHookToWorkerQueueSize = 1024;
};

} // namespace lankey::core::model
