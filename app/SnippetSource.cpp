#include "app/SnippetSource.h"

#include <ctime>

namespace lankey::app {

std::optional<core::SnippetMatch> SnippetSource::match(std::u32string_view rawWord) const {
    const auto index = index_.load();
    if (!index) return std::nullopt;
    // Nothing can match a word longer than the longest abbreviation, and most words are.
    // One integer compare keeps the common keystroke out of the hash table entirely.
    if (static_cast<int>(rawWord.size()) > index->maxAbbrLength()) return std::nullopt;
    const auto* snippet = index->find(rawWord);
    if (snippet == nullptr) return std::nullopt;
    return core::SnippetMatch{snippet->abbr, core::snippet::firstLine(snippet->body),
                              snippet->autoExpand};
}

std::optional<core::snippet::Rendered> SnippetSource::expand(std::u32string_view abbr) const {
    const auto index = index_.load();
    if (!index) return std::nullopt;
    const auto* snippet = index->find(abbr);
    if (snippet == nullptr) return std::nullopt;
    // Parameters are deliberately left unanswered here. Asking would mean a dialog, and a
    // dialog cannot be raised from the hook thread - nor should it be: it would take the
    // focus away from the window the user is typing in. Unanswered renders as a blank and
    // the caret lands in it, so they fill it in place, which is fewer steps anyway.
    core::snippet::RenderContext context;
    const std::time_t now = std::time(nullptr);
    // localtime_s over localtime: the latter hands back a pointer into a buffer shared
    // with every other thread in the process.
    if (localtime_s(&context.now, &now) != 0) return std::nullopt;
    context.variables = index->variables();
    if (const auto clipboard = clipboard_.load()) context.clipboard = *clipboard;
    return snippet->parsed.render(context);
}

} // namespace lankey::app
