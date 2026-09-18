#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "core/interfaces/ISuggestionProvider.h"
#include "core/model/AtomicSnapshot.h"
#include "core/model/Lexicon.h"
#include "core/model/Settings.h"
#include "core/smart/suggest/PhraseTrie.h"

namespace lankey::core::smart {

// F1: answers "what phrase is the user typing?" from the personal lexicon.
//
// Reads an immutable PhraseTrie snapshot published by the worker (publish()) and the
// current SuggestionSettings snapshot; never locks. Lookup order is longest context first
// (up to kSuggestionContextSyllables words of context):
//   "s-2 s-1 " + prefix  ->  phrases continuing the last two words
//   "s-1 "     + prefix  ->  phrases continuing the last word
//                prefix  ->  anything starting with the prefix (completion only)
// An empty prefix means PREDICTION: the user just finished a word and we propose what
// usually follows it ("hệ điều " -> "hành", "hành windows"). Results are merged,
// de-duplicated, ranked by static + dynamic score, and cut to kMaxSuggestions.
//
// Threading: suggest() on the hook thread; publish()/setSettings() from the worker.
class SuggestionEngine final : public ISuggestionProvider {
public:
    SuggestionEngine();

    [[nodiscard]] model::SuggestionList suggest(const model::SuggestionQuery& query) const override;

    // Worker side. Build a trie from the lexicon with scores computed "now".
    [[nodiscard]] static std::shared_ptr<const PhraseTrie>
    buildSnapshot(const std::vector<model::LexiconEntry>& entries, std::int64_t nowUnixSeconds,
                  const model::SuggestionSettings& settings);
    void publish(std::shared_ptr<const PhraseTrie> trie) { trie_.store(std::move(trie)); }
    void setSettings(const model::SuggestionSettings& settings) {
        settings_.store(std::make_shared<const model::SuggestionSettings>(settings));
    }

    [[nodiscard]] bool hasSnapshot() const noexcept { return trie_.load() != nullptr; }

private:
    model::AtomicSnapshot<PhraseTrie> trie_;
    model::AtomicSnapshot<model::SuggestionSettings> settings_;
};

// Re-apply the user's capitalisation pattern from what they typed to what we insert:
// "Chư" -> "Chương trình", "CHƯ" -> "CHƯƠNG TRÌNH", "chư" -> "chương trình".

} // namespace lankey::core::smart
