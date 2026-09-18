#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "core/interfaces/ICorrector.h"
#include "core/model/AtomicSnapshot.h"
#include "core/model/Correction.h"
#include "core/model/Lexicon.h"
#include "core/model/Settings.h"
#include "core/smart/correct/BaseSyllableSet.h"
#include "core/smart/correct/FuzzyIndex.h"

namespace lankey::core::smart {

// Read-only index over the base dictionary. Built once (DB thread, tens of ms) and never
// rebuilt: the dictionary is compiled in.
struct BaseIndex {
    const BaseSyllableSet* syllables = nullptr;
    FuzzyIndex tree;

    [[nodiscard]] static std::shared_ptr<const BaseIndex> build(const BaseSyllableSet& set);
};

// What the user has taught us, rebuilt with every lexicon snapshot.
struct CorrectionSnapshot {
    std::unordered_map<std::u32string, model::CorrectionRule> rules; // by wrong key
    std::unordered_set<std::u32string> blacklist;
    // Single syllables outside the base dictionary the user types often enough to trust
    // (frequency >= kTrustMinFrequency): names, jargon, English.
    std::unordered_set<std::u32string> trusted;
    FuzzyIndex personal; // the same trusted syllables, for fuzzy matching
    // Every single syllable the user has typed, with its frequency: breaks ties between
    // equally distant dictionary candidates in favour of what this user actually writes.
    std::unordered_map<std::u32string, std::uint32_t> frequency;
    // Dictionary guesses the user undid within kRejectionCooldownSeconds: not tried again
    // until the cooldown passes (a second Undo then blacklists for good).
    std::unordered_set<std::u32string> suppressed;

    [[nodiscard]] static std::shared_ptr<const CorrectionSnapshot>
    build(const std::vector<model::LexiconEntry>& entries,
          const std::vector<model::CorrectionRule>& rules,
          const std::vector<std::u32string>& blacklist, const BaseSyllableSet& base,
          std::int64_t nowUnixSeconds);
};

// F2 decision procedure (PLAN 4.2.3), in this exact order:
//   0. never after Enter/Tab, in a password field, in an app the user excluded, for text
//      with digits/symbols/ALL CAPS
//   1. blacklisted (the syllable or any phrase ending with it)      -> no
//   2. correction_map rule with confidence >= kCorrectionApplyConfidence,
//      longest context first                                        -> correct
//   3. the engine applied no Vietnamese transform to this syllable  -> no  (English etc.)
//   4. syllable in the base dictionary                              -> no
//   5. syllable trusted from the user's own lexicon (typed >= 5 times and not dominated
//      by a dictionary word a slip away that the user types much more: "đưởng" x6 next
//      to "đường" x11 is a repeated typo, not a word), or a guess undone lately   -> no
//   6. fuzzy: one best candidate within the level's distance (ranked by distance, tone
//      presence and the user's own frequency; a tie is ambiguous)  -> correct
//
// Worker thread. Lock-free reads of the two snapshots; no allocation on the common path
// beyond the returned Correction.
class AutoCorrectEngine final : public ICorrector {
public:
    AutoCorrectEngine();

    void publishBase(std::shared_ptr<const BaseIndex> base);
    void publish(std::shared_ptr<const CorrectionSnapshot> snapshot);
    void setSettings(const model::AutoCorrectSettings& settings);

    [[nodiscard]] std::optional<model::Correction>
    check(const model::SyllableCommitted& committed) const override;

    // Maximum fuzzy distance (scaled) accepted at a level; -1 when off.
    [[nodiscard]] static int maxDistanceScaled(model::AutoCorrectLevel level) noexcept;

private:
    model::AtomicSnapshot<BaseIndex> base_;
    model::AtomicSnapshot<CorrectionSnapshot> snapshot_;
    struct EffectiveSettings {
        model::AutoCorrectSettings settings;
        std::unordered_set<std::string> excludedApps; // lower-cased
    };
    model::AtomicSnapshot<EffectiveSettings> settings_;
};

} // namespace lankey::core::smart
