#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "core/model/Error.h"

namespace lankey::core::clipboard {

struct ClipboardItem {
    std::u32string text;
    std::int64_t copiedMs = 0;
    bool pinned = false;
};

// What the user copied, newest first, with the pinned ones held above the rest.
//
// The history lives in memory and dies with the process. Only PINNED items are written
// out, and even those go through DPAPI on the way: the rest is what somebody copied this
// afternoon, and it has no business outliving the session.
//
// Nothing here watches the clipboard - the platform does that and calls add(). This class
// is the part worth testing without a clipboard in the room.
//
// Threading: UI thread only. The hook thread never touches the history.
class ClipboardStore {
public:
    // Remembers `text`, unless it is blank or longer than Thresholds::kClipboardMaxItemChars.
    // Copying something already in the history moves it to the top rather than repeating
    // it, and leaves a pin alone. Returns false when nothing was stored.
    bool add(std::u32string text, std::int64_t nowMs);

    [[nodiscard]] std::size_t size() const noexcept { return items_.size(); }
    // Pinned items first, then the rest newest first. Index 0 is what Ctrl+Alt+V shows on
    // top; it is the same order everywhere so the settings list and the popup agree.
    [[nodiscard]] const ClipboardItem& at(std::size_t index) const { return items_[index]; }

    // False when `index` is past the end - a stale row in a list that has moved on.
    bool setPinned(std::size_t index, bool pinned);
    bool remove(std::size_t index);
    void clear() noexcept { items_.clear(); }

    // The pinned items as JSON, for the caller to seal and write. The unpinned ones are
    // deliberately not included.
    [[nodiscard]] std::string serializePinned() const;
    // Replaces the contents with what serializePinned() wrote.
    [[nodiscard]] lk::expected<void> load(std::string_view json);

private:
    void sort();

    std::vector<ClipboardItem> items_;
};

} // namespace lankey::core::clipboard
