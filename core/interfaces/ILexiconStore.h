#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "core/model/Correction.h"
#include "core/model/Error.h"
#include "core/model/Lexicon.h"

namespace lankey::core {

// Persistent user lexicon. Implementations: SqliteLexiconStore (real), InMemoryLexiconStore
// (tests). DB thread only; nothing on the hook thread ever touches this.
//
// correction_map, autocorrect_blacklist and macros get their own methods here as those
// features land (Phase 2).
class ILexiconStore {
public:
    virtual ~ILexiconStore() = default;

    [[nodiscard]] virtual lk::expected<std::vector<model::LexiconEntry>> loadAll() = 0;
    // Applies a whole batch atomically (UPSERT: frequency += delta, lastUsedAt = max).
    [[nodiscard]] virtual lk::expected<void> applyDeltas(const model::LexiconDeltaBatch& batch) = 0;
    // "Delete all my data": must leave nothing behind, including journals.
    [[nodiscard]] virtual lk::expected<void> eraseAll() = 0;
    // Ages out unpinned rows nobody has used for months and enforces the hard row limit.
    // Returns how many rows were removed.
    [[nodiscard]] virtual lk::expected<int> cleanup(std::int64_t nowUnixSeconds) = 0;

    // -- correction_map / autocorrect_blacklist (F2) --------------------------------------
    [[nodiscard]] virtual lk::expected<std::vector<model::CorrectionRule>> loadCorrections() = 0;
    [[nodiscard]] virtual lk::expected<std::vector<std::u32string>> loadBlacklist() = 0;
    // The user fixed `wrong` into `correct` once more: confidence += delta (capped at 1).
    // A different `correct` for the same `wrong` replaces the rule and starts over.
    [[nodiscard]] virtual lk::expected<void>
    reinforceCorrection(std::u32string_view wrong, std::u32string_view correct, double delta,
                        model::CorrectionRuleSource source, std::int64_t nowUnixSeconds) = 0;
    // The user undid our correction of `wrong`: confidence -= penalty, times_rejected++;
    // at kRejectionsBeforeBlacklist the phrase is blacklisted. Returns true if it was.
    [[nodiscard]] virtual lk::expected<bool>
    rejectCorrection(std::u32string_view wrong, double penalty, std::int64_t nowUnixSeconds) = 0;
    [[nodiscard]] virtual lk::expected<void> noteCorrectionApplied(std::u32string_view wrong) = 0;
    [[nodiscard]] virtual lk::expected<void> blacklist(std::u32string_view phrase,
                                                       std::int64_t nowUnixSeconds) = 0;
};

} // namespace lankey::core
