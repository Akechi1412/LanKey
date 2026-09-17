#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "core/model/Lexicon.h"

namespace lankey::core::smart {

// Immutable radix tree over Phrase::joined() (code points, NFC, case-folded), built once
// per snapshot on the worker thread and read lock-free on the hook thread.
//
// Every node carries the best static score in its subtree, so a prefix query can walk
// best-first and stop after `limit` results: the cost is bounded by `limit`, not by how
// many phrases start with "ch".
//
// Context queries need no extra structure: "s-1 " + prefix is just a longer prefix.
class PhraseTrie {
public:
    struct Item {
        model::LexiconEntry entry;
        double staticScore = 0.0;
        std::u32string key; // entry.phrase.joined(), filled by build(); the lookup key
    };

    PhraseTrie() = default;
    // Builds from any order; duplicate keys keep the higher-scored item.
    [[nodiscard]] static PhraseTrie build(std::vector<Item> items);

    // Items whose key starts with `prefix`, best static score first, at most `limit`.
    // Hook thread: no allocation beyond `out` growth and a small heap.
    void collect(std::u32string_view prefix, std::size_t limit,
                 std::vector<const Item*>& out) const;

    [[nodiscard]] const Item* find(std::u32string_view key) const;
    [[nodiscard]] std::size_t size() const noexcept { return items_.size(); }
    [[nodiscard]] std::size_t nodeCount() const noexcept { return nodes_.size(); }

private:
    struct Node {
        std::u32string label;                // edge label from parent (empty for root)
        std::vector<std::uint32_t> children; // indices into nodes_, sorted by label[0]
        std::int32_t item = -1;              // index into items_, or -1
        double maxScore = 0.0;               // best staticScore in this subtree
    };

    void insert(std::u32string_view key, std::int32_t itemIndex);
    [[nodiscard]] std::uint32_t childFor(std::uint32_t node, char32_t first) const noexcept;
    void computeMaxScores(std::uint32_t node);

    std::vector<Node> nodes_; // nodes_[0] is the root
    std::vector<Item> items_;
};

} // namespace lankey::core::smart
