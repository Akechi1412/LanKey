#include "core/smart/suggest/SuggestionEngine.h"

#include <algorithm>
#include <string>
#include <string_view>

#include "core/model/Thresholds.h"
#include "core/smart/suggest/Scorer.h"
#include "core/text/VietnameseText.h"

namespace lankey::core::smart {

using model::LexiconEntry;
using model::Suggestion;
using model::SuggestionList;
using model::SuggestionQuery;
using model::SuggestionSettings;
using model::Thresholds;

std::u32string applyCasing(std::u32string_view typed, std::u32string_view folded) {
    std::u32string out(folded);
    if (typed.empty()) return out;

    const bool firstUpper = text::toLower(typed[0]) != typed[0];
    const bool allUpper = typed.size() > 1 && std::ranges::all_of(typed, [](char32_t c) {
                              return !text::isLetter(c) || text::toLower(c) != c;
                          });
    if (allUpper) {
        for (auto& c : out)
            c = text::toUpper(c);
    } else if (firstUpper) {
        out[0] = text::toUpper(out[0]);
    }
    return out;
}

SuggestionEngine::SuggestionEngine() {
    settings_.store(std::make_shared<const SuggestionSettings>());
}

std::shared_ptr<const PhraseTrie>
SuggestionEngine::buildSnapshot(const std::vector<LexiconEntry>& entries,
                                std::int64_t nowUnixSeconds, const SuggestionSettings& settings) {
    std::vector<PhraseTrie::Item> items;
    items.reserve(entries.size());
    for (const auto& e : entries) {
        if (e.blocked || e.frequency < static_cast<std::uint32_t>(Thresholds::kLearnMinFrequency)) {
            continue; // never suggestable: keep the trie small and the search bounded
        }
        items.push_back({e, Scorer::staticScore(e, nowUnixSeconds, settings), {}});
    }
    return std::make_shared<const PhraseTrie>(PhraseTrie::build(std::move(items)));
}

SuggestionList SuggestionEngine::suggest(const SuggestionQuery& query) const {
    SuggestionList result;
    const auto trie = trie_.load();
    const auto settings = settings_.load();
    if (!trie || !settings || !settings->enabled) return result;

    // Two modes share one lookup:
    //   completion - the user is mid-syllable (prefix non-empty): need >= minPrefixLength
    //   prediction - the user just finished a word (prefix empty): context alone drives it
    const bool predicting = query.prefix.empty();
    if (predicting && query.context.empty()) return result;
    if (!predicting && query.prefix.size() < static_cast<std::size_t>(settings->minPrefixLength)) {
        return result;
    }

    // Hook-thread hot path: scratch buffers live across calls so a keystroke does not pay
    // for their allocation. Candidates are ranked as (item pointer, tail offset, score)
    // and only the top kMaxSuggestions are turned into Suggestions: a Phrase copy per
    // candidate that is then sorted away was most of this function's cost.
    struct Candidate {
        const PhraseTrie::Item* item;
        std::size_t tailStart; // index into item->key where the insert begins
        double score;
    };
    static thread_local std::u32string key;
    static thread_local std::vector<const PhraseTrie::Item*> found;
    static thread_local std::vector<Candidate> candidates;
    candidates.clear();

    // Longest context first. Each lookup is one prefix walk in the same trie. Prediction
    // never runs with an empty context: that would list the whole lexicon.
    const std::size_t maxContext = std::min<std::size_t>(
        query.context.size(), static_cast<std::size_t>(Thresholds::kSuggestionContextSyllables));
    const std::size_t minContext = predicting ? 1 : 0;
    for (std::size_t ctx = maxContext + 1; ctx-- > minContext;) {
        key.clear();
        for (std::size_t i = query.context.size() - ctx; i < query.context.size(); ++i) {
            key += query.context[i];
            key.push_back(U' ');
        }
        const std::size_t contextLength = key.size();
        key += query.prefix;

        found.clear();
        trie->collect(key, static_cast<std::size_t>(Thresholds::kSuggestionCandidates), found);
        for (const auto* item : found) {
            const std::u32string_view joined = item->key;
            // Must extend what is on screen: something has to be left to insert.
            if (joined.size() <= key.size()) continue;
            // When predicting, the continuation must start at a word boundary: "hệ " must
            // not propose "hệp".
            if (predicting && joined[contextLength - 1] != U' ') continue;
            // The same phrase can be reached again with a shorter context (its key starts
            // with the shorter key too); the longer context found it first and wins. One
            // item per key in the trie, so pointer identity is phrase identity.
            if (std::ranges::any_of(candidates,
                                    [item](const Candidate& c) { return c.item == item; })) {
                continue;
            }
            const std::size_t tailLength = joined.size() - contextLength;
            // appMatch stays false until lexicon_app_context exists (Phase 4).
            const double score =
                item->staticScore + Scorer::dynamicScore(tailLength, query.prefix.size(),
                                                         /*appMatch=*/false, *settings);
            candidates.push_back({item, contextLength, score});
        }
    }

    std::ranges::stable_sort(
        candidates, [](const Candidate& a, const Candidate& b) { return a.score > b.score; });
    const std::size_t count =
        std::min(candidates.size(), static_cast<std::size_t>(Thresholds::kMaxSuggestions));
    result.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        const Candidate& c = *(candidates.begin() + static_cast<std::ptrdiff_t>(i));
        const std::u32string_view tail(c.item->key.data() + c.tailStart,
                                       c.item->key.size() - c.tailStart);
        Suggestion s;
        s.phrase = c.item->entry.phrase;
        s.insert = applyCasing(query.typed, tail);
        s.deleteCount = static_cast<int>(query.typed.size());
        s.score = c.score;
        result.push_back(std::move(s));
    }
    return result;
}

} // namespace lankey::core::smart
