#pragma once

#include <cstdint>
#include <vector>

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
};

} // namespace lankey::core
