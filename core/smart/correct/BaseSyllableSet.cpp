#include "core/smart/correct/BaseSyllableSet.h"

namespace lankey::core::smart {

namespace {

constexpr const char32_t* const kBuiltinSyllables[] = {
#include "lankey/BaseSyllablesData.inc"
};

} // namespace

const BaseSyllableSet& BaseSyllableSet::builtin() {
    // Built on first use (a few hundred microseconds), then shared read-only.
    static const BaseSyllableSet instance{std::span<const char32_t* const>(kBuiltinSyllables)};
    return instance;
}

BaseSyllableSet::BaseSyllableSet(std::span<const char32_t* const> syllables) {
    set_.reserve(syllables.size());
    for (const char32_t* s : syllables)
        set_.emplace(s);
}

bool BaseSyllableSet::contains(std::u32string_view syllable) const {
    return set_.find(syllable) != set_.end();
}

void BaseSyllableSet::forEach(const std::function<void(std::u32string_view)>& fn) const {
    for (const auto& s : set_)
        fn(s);
}

} // namespace lankey::core::smart
