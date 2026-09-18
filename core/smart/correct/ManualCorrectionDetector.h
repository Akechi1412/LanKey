#pragma once

#include <optional>
#include <string>

#include "core/model/SyllableCommitted.h"
#include "core/smart/correct/BaseSyllableSet.h"

namespace lankey::core::smart {

// Learns correction_map from the user's own fixes (PLAN 6.4). The hook thread already
// established the mechanics - the user deleted whole syllables right after typing them
// and retyped the same number (SyllableCommitted::retypedFrom). This decides whether
// that edit looks like a spelling fix rather than a change of mind or a continuation:
//   - the two spans differ, and neither is a prefix of the other ("sa" -> "sau" is the
//     user deleting the space and typing on, not a fix);
//   - every syllable is learnable (no one-letter or absurd syllables);
//   - every new syllable is a real one: in the dictionary, or plain ASCII (English);
//   - a one-syllable rule only when the old syllable is NOT a dictionary word: "có" ->
//     "cơ" is two valid words, i.e. a change of mind, and a rule for it would rewrite the
//     commonest word in the language. Phrases ("sữa lỗi" -> "sửa lỗi") may be valid on
//     both sides: the context is what makes them a fix;
//   - VietnameseDistance(old, new) <= min(2.0, 0.6 * length).
//
// Worker thread. Pure.
class ManualCorrectionDetector {
public:
    struct Fix {
        std::u32string wrong;   // joined, folded - the correction_map key
        std::u32string correct; // joined, folded
    };

    [[nodiscard]] static std::optional<Fix> detect(const model::SyllableCommitted& committed,
                                                   const BaseSyllableSet& dictionary);
};

} // namespace lankey::core::smart
