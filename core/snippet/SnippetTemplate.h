#pragma once

#include <ctime>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace lankey::core::snippet {

// Everything a body needs in order to become text. Passed in rather than read from the
// clock and the clipboard inside render(), so the same body renders the same way in a
// test as on screen.
struct RenderContext {
    std::tm now{};                                      // local time, already broken down
    std::u32string clipboard;                           // {clipboard}
    std::map<std::u32string, std::u32string> params;    // answers to {param:Name}
    std::map<std::u32string, std::u32string> variables; // the user's own {name} values
};

struct Rendered {
    std::u32string text;
    // How far from the end of `text` the caret should end up, in characters: the number of
    // ← presses to send after inserting. 0 means "at the end", which is the usual case.
    int cursorOffsetFromEnd = 0;
};

// One snippet body, parsed once into literal runs and holes.
//
// The holes are {date}, {date:<format>}, {time}, {clipboard}, {cursor}, {param:Name} and
// the user's own {name} variables. Anything else between braces - an unknown name, an
// unclosed brace - is NOT a hole and is kept exactly as the user wrote it. That rule is
// deliberate: the body is their text, and a body that quietly loses a word is worse than
// one that shows a typo back.
//
// Pure and immutable after parse(); safe to read from any thread.
class SnippetTemplate {
public:
    [[nodiscard]] static SnippetTemplate parse(std::u32string_view body);

    [[nodiscard]] Rendered render(const RenderContext& context) const;

    // The {param:...} names, in the order they first appear and without repeats. An
    // unanswered one renders as nothing and the caret lands in the gap it left, so a body
    // with a blank drops the user straight into the blank. Callers that can ask the user
    // for values (a picker, a dialog) fill RenderContext::params instead.
    [[nodiscard]] const std::vector<std::u32string>& params() const noexcept { return params_; }

    // Names the template claims for itself. A user variable called one of these can never
    // fire - {date} is the date whatever the user defined - so the settings window warns
    // instead of letting them wonder. Exposed here so that list cannot drift from parse().
    [[nodiscard]] static bool isReservedName(std::u32string_view name) noexcept;

    // Whether the body reads the clipboard. Asked so that nothing has to keep a copy of
    // the clipboard around unless a snippet actually uses one.
    [[nodiscard]] bool usesClipboard() const noexcept { return usesClipboard_; }

    // Formats `time` the way {date:<format>} does. dd MM yyyy HH mm ss are replaced;
    // every other character is copied through, so "yyyy年MM月" works.
    [[nodiscard]] static std::u32string formatTime(const std::tm& time, std::u32string_view format);

private:
    enum class Kind : std::uint8_t { Literal, Date, Time, Clipboard, Cursor, Param, Variable };

    struct Token {
        Kind kind = Kind::Literal;
        std::u32string text; // Literal: the text. Date: the format. Param/Variable: the name.
    };

    std::vector<Token> tokens_;
    std::vector<std::u32string> params_;
    bool usesClipboard_ = false;
};

} // namespace lankey::core::snippet
