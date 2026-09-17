#pragma once

#include <vector>

#include "core/interfaces/ISuggestionProvider.h"

namespace lankey::tests {

// Returns a canned list and records the queries it was asked.
class FakeSuggestionProvider final : public core::ISuggestionProvider {
public:
    [[nodiscard]] core::model::SuggestionList
    suggest(const core::model::SuggestionQuery& query) const override {
        queries_.push_back(query);
        return canned_;
    }

    void setSuggestions(core::model::SuggestionList list) { canned_ = std::move(list); }
    [[nodiscard]] const std::vector<core::model::SuggestionQuery>& queries() const noexcept {
        return queries_;
    }

private:
    core::model::SuggestionList canned_;
    mutable std::vector<core::model::SuggestionQuery> queries_;
};

} // namespace lankey::tests
