#pragma once

#include "core/model/Suggestion.h"

namespace lankey::core {

// Answers "what might the user be typing?" on EVERY keystroke, on the hook thread.
// Implementations must be O(len(prefix)) with a bounded search, allocation-light and
// lock-free (read an immutable snapshot).
class ISuggestionProvider {
public:
    virtual ~ISuggestionProvider() = default;

    [[nodiscard]] virtual model::SuggestionList
    suggest(const model::SuggestionQuery& query) const = 0;
};

} // namespace lankey::core
