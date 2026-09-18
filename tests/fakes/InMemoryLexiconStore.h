#pragma once

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "core/interfaces/ILexiconStore.h"
#include "core/model/Thresholds.h"

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
        rules_.clear();
        blacklist_.clear();
        return {};
    }

    // Age-out is a storage policy under test in lexicon_store_test; here nothing expires.
    [[nodiscard]] lk::expected<int> cleanup(std::int64_t /*nowUnixSeconds*/) override {
        ++cleanups;
        return 0;
    }
    int cleanups = 0;

    [[nodiscard]] lk::expected<std::vector<core::model::CorrectionRule>>
    loadCorrections() override {
        std::vector<core::model::CorrectionRule> out;
        for (const auto& [key, rule] : rules_)
            out.push_back(rule);
        return out;
    }
    [[nodiscard]] lk::expected<std::vector<std::u32string>> loadBlacklist() override {
        return std::vector<std::u32string>(blacklist_.begin(), blacklist_.end());
    }
    [[nodiscard]] lk::expected<void> reinforceCorrection(std::u32string_view wrong,
                                                         std::u32string_view correct, double delta,
                                                         core::model::CorrectionRuleSource source,
                                                         std::int64_t nowUnixSeconds) override {
        auto& r = rules_[std::u32string(wrong)];
        if (r.wrong.empty()) r.wrong = std::u32string(wrong);
        r.updatedAt = nowUnixSeconds;
        if (r.correct == correct) {
            r.confidence = std::min(1.0, r.confidence + delta);
        } else {
            r.correct = std::u32string(correct);
            r.confidence = delta;
            r.timesRejected = 0;
        }
        r.source = source;
        return {};
    }
    [[nodiscard]] lk::expected<bool> rejectCorrection(std::u32string_view wrong, double penalty,
                                                      std::int64_t nowUnixSeconds) override {
        auto& r = rules_[std::u32string(wrong)];
        if (r.wrong.empty()) r.wrong = std::u32string(wrong);
        r.updatedAt = nowUnixSeconds;
        r.confidence = std::max(0.0, r.confidence - penalty);
        ++r.timesRejected;
        if (r.timesRejected >= core::model::Thresholds::kRejectionsBeforeBlacklist) {
            blacklist_.insert(std::u32string(wrong));
            return true;
        }
        return false;
    }
    [[nodiscard]] lk::expected<void> noteCorrectionApplied(std::u32string_view wrong) override {
        if (auto it = rules_.find(std::u32string(wrong)); it != rules_.end()) {
            ++it->second.timesApplied;
        }
        return {};
    }
    [[nodiscard]] lk::expected<void> blacklist(std::u32string_view phrase,
                                               std::int64_t /*nowUnixSeconds*/) override {
        blacklist_.insert(std::u32string(phrase));
        return {};
    }

    // Test helpers
    [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }
    [[nodiscard]] const core::model::LexiconEntry* find(const std::u32string& joined) const {
        const auto it = entries_.find(joined);
        return it == entries_.end() ? nullptr : &it->second;
    }

    [[nodiscard]] const core::model::CorrectionRule* rule(const std::u32string& wrong) const {
        const auto it = rules_.find(wrong);
        return it == rules_.end() ? nullptr : &it->second;
    }

private:
    std::map<std::u32string, core::model::LexiconEntry> entries_;
    std::map<std::u32string, core::model::CorrectionRule> rules_;
    std::set<std::u32string> blacklist_;
};

} // namespace lankey::tests
