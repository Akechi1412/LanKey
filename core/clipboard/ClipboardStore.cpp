#include "core/clipboard/ClipboardStore.h"

#include <algorithm>

#include <nlohmann/json.hpp>

#include "core/model/Thresholds.h"
#include "core/text/Utf.h"

namespace lankey::core::clipboard {

using model::Error;
using model::Thresholds;

namespace {

bool blank(std::u32string_view s) {
    for (const char32_t c : s) {
        if (c != U' ' && c != U'\t' && c != U'\n' && c != U'\r') return false;
    }
    return true;
}

} // namespace

// Pinned above the rest, each group newest first. One order for the popup and the settings
// list: two orders would mean "the third one down" meant different things in each.
void ClipboardStore::sort() {
    std::stable_sort(items_.begin(), items_.end(),
                     [](const ClipboardItem& a, const ClipboardItem& b) {
                         if (a.pinned != b.pinned) return a.pinned;
                         return a.copiedMs > b.copiedMs;
                     });
}

bool ClipboardStore::add(std::u32string text, std::int64_t nowMs) {
    if (blank(text)) return false;
    if (text.size() > static_cast<std::size_t>(Thresholds::kClipboardMaxItemChars)) return false;

    const auto same = std::find_if(items_.begin(), items_.end(),
                                   [&](const ClipboardItem& i) { return i.text == text; });
    if (same != items_.end()) {
        // Copying the same thing again is one entry that just happened, not two entries.
        // The pin stays: it was the user's decision, not a property of the copy.
        same->copiedMs = nowMs;
        sort();
        return true;
    }

    items_.push_back({std::move(text), nowMs, false});
    sort();
    // Only unpinned items are counted against the cap, and only they are dropped. A pin
    // means "keep this", and a busy afternoon must not quietly undo that.
    std::size_t unpinned = 0;
    for (auto it = items_.begin(); it != items_.end();) {
        if (it->pinned) {
            ++it;
            continue;
        }
        ++unpinned;
        if (unpinned > static_cast<std::size_t>(Thresholds::kClipboardHistoryItems)) {
            it = items_.erase(it);
            continue;
        }
        ++it;
    }
    return true;
}

bool ClipboardStore::setPinned(std::size_t index, bool pinned) {
    if (index >= items_.size()) return false;
    items_[index].pinned = pinned;
    sort();
    return true;
}

bool ClipboardStore::remove(std::size_t index) {
    if (index >= items_.size()) return false;
    items_.erase(items_.begin() + static_cast<std::ptrdiff_t>(index));
    return true;
}

std::string ClipboardStore::serializePinned() const {
    nlohmann::json rows = nlohmann::json::array();
    for (const ClipboardItem& i : items_) {
        if (!i.pinned) continue;
        rows.push_back({{"text", text::toUtf8(i.text)}, {"copiedMs", i.copiedMs}});
    }
    return nlohmann::json{{"pinned", rows}}.dump();
}

lk::expected<void> ClipboardStore::load(std::string_view json) {
    nlohmann::json root = nlohmann::json::parse(json, nullptr, /*allow_exceptions=*/false);
    if (root.is_discarded() || !root.is_object()) {
        return lk::unexpected(
            Error::make(Error::Code::Corrupted, "clipboard store is not a JSON object"));
    }
    items_.clear();
    const auto rows = root.find("pinned");
    if (rows == root.end() || !rows->is_array()) return {};
    for (const auto& row : *rows) {
        if (!row.is_object()) continue;
        const auto text = row.find("text");
        if (text == row.end() || !text->is_string()) continue;
        ClipboardItem item;
        item.text = text::fromUtf8(text->get<std::string>());
        if (blank(item.text)) continue;
        if (const auto at = row.find("copiedMs"); at != row.end() && at->is_number_integer()) {
            item.copiedMs = at->get<std::int64_t>();
        }
        item.pinned = true;
        items_.push_back(std::move(item));
    }
    sort();
    return {};
}

} // namespace lankey::core::clipboard
