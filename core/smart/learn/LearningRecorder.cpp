#include "core/smart/learn/LearningRecorder.h"

#include <algorithm>
#include <utility>

#include "core/model/Thresholds.h"

namespace lankey::core::smart {

using model::LexiconDelta;
using model::LexiconDeltaBatch;
using model::Phrase;
using model::Thresholds;

LearningRecorder::LearningRecorder(IClock& clock, FlushHandler onFlush)
    : clock_(clock), onFlush_(std::move(onFlush)), lastFlushMs_(clock.nowMonotonicMs()) {}

bool LearningRecorder::learnable(const Phrase& phrase) noexcept {
    // A phrase with a one-letter or absurdly long syllable is noise ("a", pasted junk).
    return std::ranges::all_of(phrase.syllables, [](const model::Syllable& s) {
        const auto n = static_cast<int>(s.text.size());
        return n >= Thresholds::kMinSyllableLength && n <= Thresholds::kMaxSyllableLength;
    });
}

void LearningRecorder::add(const Phrase& phrase, std::int32_t delta) {
    auto& d = pending_[phrase.joined()];
    if (d.phrase.syllables.empty()) {
        d.phrase = phrase;
        d.frequencyDelta = 0; // LexiconDelta defaults to 1; we accumulate explicitly
    }
    d.frequencyDelta += delta;
    d.usedAt = clock_.nowUnixSeconds();
}

void LearningRecorder::record(const model::SyllableCommitted& committed) {
    for (const auto& phrase : committed.window.candidatePhrases()) {
        if (learnable(phrase)) add(phrase, 1);
    }
}

void LearningRecorder::recordSelection(const Phrase& phrase) {
    if (learnable(phrase)) add(phrase, kSelectionWeight);
}

void LearningRecorder::flushIfDue() {
    if (pending_.empty()) return;
    const bool bySize = pending_.size() >= static_cast<std::size_t>(Thresholds::kFlushBatchSize);
    const bool byTime = clock_.nowMonotonicMs() - lastFlushMs_ >= Thresholds::kFlushIntervalMs;
    if (bySize || byTime) flush();
}

void LearningRecorder::flush() {
    lastFlushMs_ = clock_.nowMonotonicMs();
    if (pending_.empty()) return;
    LexiconDeltaBatch batch;
    batch.reserve(pending_.size());
    for (auto& [key, delta] : pending_)
        batch.push_back(std::move(delta));
    pending_.clear();
    if (onFlush_) onFlush_(std::move(batch));
}

} // namespace lankey::core::smart
