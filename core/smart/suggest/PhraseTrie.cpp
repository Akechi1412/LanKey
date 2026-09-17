#include "core/smart/suggest/PhraseTrie.h"

#include <algorithm>
#include <utility>

namespace lankey::core::smart {

namespace {

constexpr std::uint32_t kNone = 0xFFFFFFFFu;

std::size_t commonPrefix(std::u32string_view a, std::u32string_view b) noexcept {
    const auto n = std::min(a.size(), b.size());
    std::size_t i = 0;
    while (i < n && a[i] == b[i])
        ++i;
    return i;
}

} // namespace

PhraseTrie PhraseTrie::build(std::vector<Item> items) {
    PhraseTrie t;
    t.nodes_.emplace_back(); // root
    t.items_ = std::move(items);
    for (std::size_t i = 0; i < t.items_.size(); ++i) {
        // Joined once here; the hook thread reads item.key instead of re-joining per hit.
        t.items_[i].key = t.items_[i].entry.phrase.joined();
        t.insert(t.items_[i].key, static_cast<std::int32_t>(i));
    }
    t.computeMaxScores(0);
    return t;
}

std::uint32_t PhraseTrie::childFor(std::uint32_t node, char32_t first) const noexcept {
    const auto& kids = nodes_[node].children;
    const auto it =
        std::lower_bound(kids.begin(), kids.end(), first, [this](std::uint32_t idx, char32_t c) {
            return nodes_[idx].label[0] < c;
        });
    if (it != kids.end() && nodes_[*it].label[0] == first) return *it;
    return kNone;
}

void PhraseTrie::insert(std::u32string_view key, std::int32_t itemIndex) {
    std::uint32_t node = 0;
    std::u32string_view rest = key;
    while (!rest.empty()) {
        const std::uint32_t child = childFor(node, rest[0]);
        if (child == kNone) {
            Node n;
            n.label.assign(rest.begin(), rest.end());
            n.item = itemIndex;
            nodes_.push_back(std::move(n));
            const auto idx = static_cast<std::uint32_t>(nodes_.size() - 1);
            auto& kids = nodes_[node].children;
            kids.insert(std::lower_bound(kids.begin(), kids.end(), idx,
                                         [this](std::uint32_t a, std::uint32_t b) {
                                             return nodes_[a].label[0] < nodes_[b].label[0];
                                         }),
                        idx);
            return;
        }
        const std::size_t shared = commonPrefix(nodes_[child].label, rest);
        if (shared < nodes_[child].label.size()) {
            // Split the edge: child keeps the tail, a new middle node takes the head.
            Node middle;
            middle.label = nodes_[child].label.substr(0, shared);
            nodes_[child].label.erase(0, shared);
            nodes_.push_back(std::move(middle));
            const auto midIdx = static_cast<std::uint32_t>(nodes_.size() - 1);
            nodes_[midIdx].children.push_back(child);
            auto& kids = nodes_[node].children;
            *std::find(kids.begin(), kids.end(), child) = midIdx;
            node = midIdx;
        } else {
            node = child;
        }
        rest.remove_prefix(shared);
    }
    // Key ends exactly at `node`.
    if (nodes_[node].item < 0 ||
        items_[static_cast<std::size_t>(itemIndex)].staticScore >
            items_[static_cast<std::size_t>(nodes_[node].item)].staticScore) {
        nodes_[node].item = itemIndex;
    }
}

void PhraseTrie::computeMaxScores(std::uint32_t node) {
    // Iterative post-order to avoid deep recursion on long keys.
    std::vector<std::pair<std::uint32_t, bool>> stack{{node, false}};
    while (!stack.empty()) {
        auto [n, visited] = stack.back();
        stack.pop_back();
        if (!visited) {
            stack.emplace_back(n, true);
            for (const auto c : nodes_[n].children)
                stack.emplace_back(c, false);
            continue;
        }
        double best = nodes_[n].item >= 0
                          ? items_[static_cast<std::size_t>(nodes_[n].item)].staticScore
                          : -1e300;
        for (const auto c : nodes_[n].children)
            best = std::max(best, nodes_[c].maxScore);
        nodes_[n].maxScore = best;
    }
}

const PhraseTrie::Item* PhraseTrie::find(std::u32string_view key) const {
    if (nodes_.empty()) return nullptr;
    std::uint32_t node = 0;
    std::u32string_view rest = key;
    while (!rest.empty()) {
        const std::uint32_t child = childFor(node, rest[0]);
        if (child == kNone) return nullptr;
        const auto& label = nodes_[child].label;
        if (rest.size() < label.size() || rest.substr(0, label.size()) != label) return nullptr;
        rest.remove_prefix(label.size());
        node = child;
    }
    return nodes_[node].item >= 0 ? &items_[static_cast<std::size_t>(nodes_[node].item)] : nullptr;
}

void PhraseTrie::collect(std::u32string_view prefix, std::size_t limit,
                         std::vector<const Item*>& out) const {
    if (nodes_.empty() || limit == 0) return;

    // Walk down to the node covering `prefix` (the match may end in the middle of an edge).
    std::uint32_t node = 0;
    std::u32string_view rest = prefix;
    while (!rest.empty()) {
        const std::uint32_t child = childFor(node, rest[0]);
        if (child == kNone) return;
        const auto& label = nodes_[child].label;
        const std::size_t shared = commonPrefix(label, rest);
        if (shared < rest.size() && shared < label.size()) return; // diverged
        rest.remove_prefix(shared);
        node = child;
    }

    // Best-first over the subtree. Heap entries are either nodes (keyed by subtree max) or
    // items (keyed by their own score); popping an item means nothing left can beat it.
    struct Entry {
        double score;
        std::uint32_t node; // kNone when this entry is an item
        std::int32_t item;
        bool operator<(const Entry& o) const noexcept { return score < o.score; }
    };
    // Hook-thread hot path: the heap's storage survives across calls (no allocation per
    // keystroke after warm-up). std::priority_queue cannot be cleared, so drive the heap
    // algorithms over a plain vector.
    static thread_local std::vector<Entry> heap;
    heap.clear();
    const auto push = [&](Entry e) {
        heap.push_back(e);
        std::push_heap(heap.begin(), heap.end());
    };
    const auto pop = [&] {
        std::pop_heap(heap.begin(), heap.end());
        const Entry e = heap.back();
        heap.pop_back();
        return e;
    };
    push({nodes_[node].maxScore, node, -1});
    while (!heap.empty() && out.size() < limit) {
        const Entry e = pop();
        if (e.node == kNone) {
            out.push_back(&items_[static_cast<std::size_t>(e.item)]);
            continue;
        }
        const Node& n = nodes_[e.node];
        if (n.item >= 0) {
            push({items_[static_cast<std::size_t>(n.item)].staticScore, kNone, n.item});
        }
        for (const auto c : n.children)
            push({nodes_[c].maxScore, c, -1});
    }
}

} // namespace lankey::core::smart
