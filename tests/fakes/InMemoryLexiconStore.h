#pragma once

#include <algorithm>
#include <map>
#include <string>
#include <vector>

#include "core/interfaces/ILexiconStore.h"

namespace lankey::tests {

// Reference implementation of ILexiconStore semantics, keyed by Phrase::joined().
// SqliteLexiconStore must behave identically; the same test suite runs against both.
class InMemoryLexiconStore final : public core::ILexiconStore {
public:
    [[nodiscard]] lk::expected<std::vector<core::model::LexiconEntry>> loadAll() override {
        std::vector<core::model::LexiconEntry> out;
        out.reserve(entries_.size());
        for (const auto& [key, entry] : entries_)
            out.push_back(entry);
        return out;
    }

    [[nodiscard]] lk::expected<void>
    applyDeltas(const core::model::LexiconDeltaBatch& batch) override {
        for (const auto& d : batch) {
            auto& e = entries_[d.phrase.joined()];
            if (e.frequency == 0) {
                e.phrase = d.phrase;
                e.firstSeenAt = d.usedAt;
            }
            const auto next = static_cast<std::int64_t>(e.frequency) + d.frequencyDelta;
            e.frequency = static_cast<std::uint32_t>(next < 0 ? 0 : next);
            e.lastUsedAt = std::max(e.lastUsedAt, d.usedAt);
        }
        return {};
    }

    [[nodiscard]] lk::expected<void> eraseAll() override {
        entries_.clear();
        return {};
    }

    // Age-out is a storage policy under test in lexicon_store_test; here nothing expires.
    [[nodiscard]] lk::expected<int> cleanup(std::int64_t /*nowUnixSeconds*/) override {
        ++cleanups;
        return 0;
    }
    int cleanups = 0;

    // Test helpers
    [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }
    [[nodiscard]] const core::model::LexiconEntry* find(const std::u32string& joined) const {
        const auto it = entries_.find(joined);
        return it == entries_.end() ? nullptr : &it->second;
    }

private:
    std::map<std::u32string, core::model::LexiconEntry> entries_;
};

} // namespace lankey::tests
