#pragma once

#include <cstddef>
#include <map>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "core/model/Error.h"
#include "core/model/Settings.h"
#include "core/snippet/SnippetTemplate.h"

namespace lankey::core::snippet {

// The first line of a body, for the one-line preview in the popup and the picker. A body
// with nothing but a newline in it previews as empty, which is correct: there is nothing
// to show.
[[nodiscard]] std::u32string firstLine(std::u32string_view body);

// One entry of snippets.json.
struct Snippet {
    std::u32string abbr;
    std::u32string body;
    // Expand the moment the ending key is typed, instead of offering it in the popup.
    // Off unless the user asks for it per snippet: text appearing without being asked for
    // is the thing people hate most about snippet tools.
    bool autoExpand = false;
    SnippetTemplate parsed;
};

// The user's snippets.json, indexed for matching while typing and for the picker.
//
// Abbreviations are matched EXACTLY, case included. An abbreviation is a key the user
// presses on purpose; folding case would let "Ky" at the start of a sentence fire the
// snippet meant for "ky".
//
// Threading: built on the UI thread, published read-only to the hook thread.
class SnippetIndex {
public:
    // The snippets as they are kept in settings.json. This is where they come from now;
    // the JSON overload is only still here to import a snippets.json written by an older
    // version. Cannot fail: a half-written row is skipped, the same as in a file.
    void load(const model::SnippetSettings& settings);

    [[nodiscard]] lk::expected<void> load(std::string_view json);

    [[nodiscard]] std::size_t size() const noexcept { return snippets_.size(); }
    // Longest abbreviation in characters: how far back the typing matcher has to look.
    [[nodiscard]] int maxAbbrLength() const noexcept { return maxAbbrLength_; }
    [[nodiscard]] const std::map<std::u32string, std::u32string>& variables() const noexcept {
        return variables_;
    }

    // Whether any snippet reads the clipboard. False means nothing needs to keep a copy of
    // it, which is the point: the clipboard holds passwords often enough that watching it
    // should be something the user's own file asked for.
    [[nodiscard]] bool usesClipboard() const noexcept { return usesClipboard_; }

    [[nodiscard]] const Snippet* find(std::u32string_view abbr) const;

    // Snippets whose abbreviation or body contains `text` (case-insensitively), in file
    // order. An empty `text` lists everything - what the picker shows before any typing.
    [[nodiscard]] std::vector<const Snippet*> search(std::u32string_view text) const;

    template <typename Fn>
    void forEach(Fn&& fn) const {
        for (const Snippet& s : snippets_)
            fn(s);
    }

private:
    void clear();
    // Stores one snippet, replacing an earlier one with the same abbreviation. Returns
    // false when there is nothing to store.
    bool add(Snippet s);
    // Recomputes usesClipboard_ from the snippets that survived.
    void finish();

    std::vector<Snippet> snippets_;
    std::unordered_map<std::u32string, std::size_t> byAbbr_;
    std::map<std::u32string, std::u32string> variables_;
    int maxAbbrLength_ = 0;
    bool usesClipboard_ = false;
};

} // namespace lankey::core::snippet
