#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "core/snippet/SnippetTemplate.h"

namespace lankey::core {

// A snippet recognised under the caret. The body is deliberately not here: turning it
// into text needs the clock, the clipboard and sometimes a question to the user, and none
// of that can happen on every keystroke.
struct SnippetMatch {
    std::u32string abbr;    // what matched, and the key expand() is asked for
    std::u32string preview; // the first line of the body: what the popup shows
    bool autoExpand = false;
};

// The user's snippets, as the hook thread sees them.
class ISnippetSource {
public:
    virtual ~ISnippetSource() = default;

    // Matches an abbreviation against the RAW KEYS of the word being typed, not against
    // what is on screen. In Telex the keys "osnn" stand on screen as "ónn", and the keys
    // are what the user chose as the trigger.
    //
    // Called on every keystroke on the hook thread: a snapshot lookup, no locks, no I/O.
    [[nodiscard]] virtual std::optional<SnippetMatch> match(std::u32string_view rawWord) const = 0;

    // The text to put on screen for `abbr`, as of now. Returning nullopt means "insert
    // nothing" - the snippet is gone, or the user cancelled the parameter dialog - and the
    // abbreviation is then left on screen exactly as they typed it.
    //
    // Also called on the hook thread, so it must return promptly. An implementation that
    // needs to ask the user something owns that problem; it may not block here.
    [[nodiscard]] virtual std::optional<snippet::Rendered>
    expand(std::u32string_view abbr) const = 0;
};

} // namespace lankey::core
