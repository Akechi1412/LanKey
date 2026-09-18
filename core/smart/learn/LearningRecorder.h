#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <string>

#include "core/interfaces/IClock.h"
#include "core/model/Lexicon.h"
#include "core/model/SyllableCommitted.h"

namespace lankey::core::smart {

// Turns committed syllables into lexicon increments and batches them for the DB thread.
//
// For a window [s-2, s-1, S] every phrase ending in S is counted: (s-2 s-1 S), (s-1 S), (S).
// That is how "chương trình" gets learned without anyone declaring it a word. Increments
// are merged in RAM and handed over as one batch every kFlushIntervalMs, every
// kFlushBatchSize distinct phrases, or on flush() at shutdown.
//
// Threading: worker thread only. The flush handler receives the batch on the same thread
// and is expected to hand it to the DB thread.
class LearningRecorder {
public:
    using FlushHandler = std::function<void(model::LexiconDeltaBatch)>;

    LearningRecorder(IClock& clock, FlushHandler onFlush);

    // Count every candidate phrase of this event. The caller has already run PrivacyFilter.
    void record(const model::SyllableCommitted& committed);

    // A picked suggestion is a stronger signal than a typed phrase.
    void recordSelection(const model::Phrase& phrase);
    // Direct adjustment (Undo of a correction: +1 what the user typed, -1 what we put).
    void adjust(const model::Phrase& phrase, std::int32_t delta);

    // Call periodically (e.g. on every worker loop iteration): flushes when due.
    void flushIfDue();
    void flush();

    [[nodiscard]] std::size_t pending() const noexcept { return pending_.size(); }

    static constexpr std::int32_t kSelectionWeight = 2;

    // A phrase with a one-letter or absurdly long syllable is noise, never worth learning
    // (nor worth a correction rule: ManualCorrectionDetector applies the same test).
    [[nodiscard]] static bool learnable(const model::Phrase& phrase) noexcept;

private:
    void add(const model::Phrase& phrase, std::int32_t delta);

    IClock& clock_;
    FlushHandler onFlush_;
    std::map<std::u32string, model::LexiconDelta> pending_;
    std::int64_t lastFlushMs_ = 0;
};

} // namespace lankey::core::smart
