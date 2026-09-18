#include "core/smart/correct/ManualCorrectionDetector.h"

#include <algorithm>

#include "core/model/Thresholds.h"
#include "core/smart/learn/LearningRecorder.h"
#include "core/text/VietnameseDistance.h"
#include "core/text/VietnameseText.h"

namespace lankey::core::smart {

using model::Phrase;
using model::Thresholds;

std::optional<ManualCorrectionDetector::Fix>
ManualCorrectionDetector::detect(const model::SyllableCommitted& committed,
                                 const BaseSyllableSet& dictionary) {
    const auto& old = committed.retypedFrom;
    const auto& window = committed.window.committed;
    const std::size_t k = old.size();
    if (k == 0 || k > window.size()) return std::nullopt;

    Phrase wrong;
    Phrase correct;
    wrong.syllables = old;
    correct.syllables.assign(window.end() - static_cast<std::ptrdiff_t>(k), window.end());
    if (!LearningRecorder::learnable(wrong) || !LearningRecorder::learnable(correct)) {
        return std::nullopt;
    }
    for (const auto& s : correct.syllables) {
        if (!text::isAllLetters(s.text)) return std::nullopt; // codes, numbers: not spelling
        // The target must be a word: a dictionary syllable, or ASCII-only (English).
        const bool ascii = std::ranges::none_of(s.text, [](char32_t c) { return c > 0x7F; });
        if (!ascii && !dictionary.contains(s.text)) return std::nullopt;
    }
    if (k == 1 && dictionary.contains(wrong.syllables[0].text)) return std::nullopt;

    Fix fix{wrong.joined(), correct.joined()};
    if (fix.wrong == fix.correct) return std::nullopt;
    if (fix.correct.starts_with(fix.wrong) || fix.wrong.starts_with(fix.correct)) {
        return std::nullopt; // typed on, or trimmed: not a fix
    }
    // Absolute cap from the plan (2.0), and a relative one so that a short word replaced
    // by another short word ("xe" -> "nha") does not pass as a spelling fix: at most 60%
    // of the longer span may change.
    const auto longer = static_cast<int>(std::max(fix.wrong.size(), fix.correct.size()));
    const int relativeCap = longer * 3; // 0.6 * length, in scaled units (5 per char)
    const int cap = std::min(Thresholds::kManualCorrectionMaxDistanceScaled, relativeCap);
    if (text::VietnameseDistance::scaled(fix.wrong, fix.correct) > cap) {
        return std::nullopt; // a different word, not a fixed one
    }
    return fix;
}

} // namespace lankey::core::smart
