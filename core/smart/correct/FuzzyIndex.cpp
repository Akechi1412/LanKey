#include "core/smart/correct/FuzzyIndex.h"

#include <algorithm>

namespace lankey::core::smart {

using text::VietnameseDistance;

namespace {

void skeletonOf(const VietnameseDistance::Units& units, std::u32string& out) {
    out.clear();
    for (const char32_t u : units)
        out.push_back(VietnameseDistance::unitClass(u));
}

} // namespace

void FuzzyIndex::insert(std::u32string_view key) {
    Entry entry;
    entry.key.assign(key);
    VietnameseDistance::tokenize(key, entry.units);
    std::u32string skeleton;
    skeletonOf(entry.units, skeleton);
    // Duplicate? Only keys sharing the skeleton can be equal.
    if (const auto it = buckets_.find(skeleton); it != buckets_.end()) {
        for (const std::uint32_t i : it->second) {
            if (keys_[i].key == key) return;
        }
    }
    const auto index = static_cast<std::uint32_t>(keys_.size());
    keys_.push_back(std::move(entry));
    buckets_[skeleton].push_back(index);
    std::u32string variant;
    for (std::size_t i = 0; i < skeleton.size(); ++i) {
        variant = skeleton;
        variant.erase(i, 1);
        auto& bucket = buckets_[variant];
        // Deleting either of two equal neighbours gives the same variant: index once.
        if (bucket.empty() || bucket.back() != index) bucket.push_back(index);
    }
}

void FuzzyIndex::search(std::u32string_view query, int maxScaled, std::vector<Match>& out) const {
    out.clear();
    if (keys_.empty() || maxScaled < 0) return;
    // Worker-thread lookup: scratch state persists per thread; `stamp` marks the keys
    // already examined in this search without clearing anything.
    static thread_local VietnameseDistance::Units queryUnits;
    static thread_local std::u32string skeleton;
    static thread_local std::u32string variant;
    static thread_local std::vector<std::uint32_t> seen;
    static thread_local std::uint32_t stamp = 0;
    if (seen.size() < keys_.size()) seen.resize(keys_.size(), 0);
    if (++stamp == 0) {
        std::fill(seen.begin(), seen.end(), 0);
        stamp = 1;
    }
    VietnameseDistance::tokenize(query, queryUnits);
    skeletonOf(queryUnits, skeleton);

    const auto examine = [&](const std::u32string& bucketKey) {
        const auto it = buckets_.find(bucketKey);
        if (it == buckets_.end()) return;
        for (const std::uint32_t i : it->second) {
            if (seen[i] == stamp) continue;
            seen[i] = stamp;
            const int d = VietnameseDistance::scaledUnits(queryUnits, keys_[i].units);
            if (d <= maxScaled) out.push_back({keys_[i].key, d});
        }
    };
    examine(skeleton);
    if (maxScaled >= VietnameseDistance::kScale) {
        // One full edit allowed: keys whose skeleton is this one with a unit deleted, and
        // keys equal to (or one deletion away from) this one with a unit deleted.
        for (std::size_t i = 0; i < skeleton.size(); ++i) {
            variant = skeleton;
            variant.erase(i, 1);
            examine(variant);
        }
    }
    std::stable_sort(out.begin(), out.end(), [](const Match& a, const Match& b) {
        return a.scaledDistance < b.scaledDistance;
    });
}

} // namespace lankey::core::smart
