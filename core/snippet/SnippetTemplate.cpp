#include "core/snippet/SnippetTemplate.h"

#include <algorithm>

namespace lankey::core::snippet {

namespace {

constexpr std::u32string_view kDefaultDate = U"dd/MM/yyyy";
constexpr std::u32string_view kDefaultTime = U"HH:mm";

void appendPadded(std::u32string& out, int value, int width) {
    std::u32string digits;
    int v = value;
    do {
        digits.insert(digits.begin(), static_cast<char32_t>(static_cast<unsigned>(U'0') +
                                                            static_cast<unsigned>(v % 10)));
        v /= 10;
    } while (v != 0);
    while (static_cast<int>(digits.size()) < width)
        digits.insert(digits.begin(), U'0');
    out += digits;
}

// How many of `letter` start at `from`.
std::size_t runLength(std::u32string_view s, std::size_t from) {
    std::size_t n = 0;
    while (from + n < s.size() && s[from + n] == s[from])
        ++n;
    return n;
}

} // namespace

std::u32string SnippetTemplate::formatTime(const std::tm& time, std::u32string_view format) {
    std::u32string out;
    for (std::size_t i = 0; i < format.size();) {
        const char32_t c = format[i];
        const std::size_t run = runLength(format, i);
        switch (c) {
        case U'y':
            // yyyy = 2026, yy = 26; any other run length is not a year pattern.
            if (run == 4) {
                appendPadded(out, time.tm_year + 1900, 4);
                i += run;
                continue;
            }
            if (run == 2) {
                appendPadded(out, (time.tm_year + 1900) % 100, 2);
                i += run;
                continue;
            }
            break;
        case U'M':
            if (run == 1 || run == 2) {
                appendPadded(out, time.tm_mon + 1, static_cast<int>(run));
                i += run;
                continue;
            }
            break;
        case U'd':
            if (run == 1 || run == 2) {
                appendPadded(out, time.tm_mday, static_cast<int>(run));
                i += run;
                continue;
            }
            break;
        case U'H':
            if (run == 1 || run == 2) {
                appendPadded(out, time.tm_hour, static_cast<int>(run));
                i += run;
                continue;
            }
            break;
        case U'm':
            if (run == 1 || run == 2) {
                appendPadded(out, time.tm_min, static_cast<int>(run));
                i += run;
                continue;
            }
            break;
        case U's':
            if (run == 1 || run == 2) {
                appendPadded(out, time.tm_sec, static_cast<int>(run));
                i += run;
                continue;
            }
            break;
        default:
            break;
        }
        out.push_back(c);
        ++i;
    }
    return out;
}

bool SnippetTemplate::isReservedName(std::u32string_view name) noexcept {
    return name == U"date" || name == U"time" || name == U"clipboard" || name == U"cursor" ||
           name == U"param";
}

SnippetTemplate SnippetTemplate::parse(std::u32string_view body) {
    SnippetTemplate t;
    std::u32string literal;
    const auto flush = [&] {
        if (literal.empty()) return;
        t.tokens_.push_back({Kind::Literal, std::move(literal)});
        literal.clear();
    };

    for (std::size_t i = 0; i < body.size();) {
        if (body[i] != U'{') {
            literal.push_back(body[i++]);
            continue;
        }
        const std::size_t close = body.find(U'}', i + 1);
        if (close == std::u32string_view::npos) {
            // No closing brace at all: the rest is text the user wrote.
            literal.append(body.substr(i));
            break;
        }
        const std::u32string_view inside = body.substr(i + 1, close - i - 1);
        if (inside.find(U'{') != std::u32string_view::npos) {
            // Another brace opens before this one closes, so THIS brace does not start a
            // hole - "{ {date}" is a stray brace followed by a real one. Emit it as text
            // and let the later brace have its turn.
            literal.push_back(body[i++]);
            continue;
        }
        const std::size_t colon = inside.find(U':');
        const std::u32string_view name = inside.substr(0, colon);
        const std::u32string_view arg =
            colon == std::u32string_view::npos ? std::u32string_view{} : inside.substr(colon + 1);

        Token token;
        bool known = true;
        if (name == U"date") {
            token = {Kind::Date, std::u32string(arg.empty() ? kDefaultDate : arg)};
        } else if (name == U"time") {
            token = {Kind::Time, std::u32string(arg.empty() ? kDefaultTime : arg)};
        } else if (name == U"clipboard" && colon == std::u32string_view::npos) {
            token = {Kind::Clipboard, {}};
            t.usesClipboard_ = true;
        } else if (name == U"cursor" && colon == std::u32string_view::npos) {
            token = {Kind::Cursor, {}};
        } else if (name == U"param" && !arg.empty()) {
            token = {Kind::Param, std::u32string(arg)};
            if (std::find(t.params_.begin(), t.params_.end(), token.text) == t.params_.end()) {
                t.params_.push_back(token.text);
            }
        } else if (colon == std::u32string_view::npos && !name.empty()) {
            // A user variable. Whether it exists is decided at render time, against the
            // context; parsing does not know the user's variable list.
            token = {Kind::Variable, std::u32string(name)};
        } else {
            known = false;
        }

        if (!known) {
            literal.append(body.substr(i, close - i + 1));
            i = close + 1;
            continue;
        }
        flush();
        t.tokens_.push_back(std::move(token));
        i = close + 1;
    }
    flush();
    return t;
}

Rendered SnippetTemplate::render(const RenderContext& context) const {
    Rendered out;
    // Where the caret goes, as an offset from the START while building; turned into an
    // offset from the end once the whole text exists.
    std::size_t caret = std::u32string::npos;
    // Where the first unanswered {param} left a gap. The caret lands there when the body
    // did not ask for one anywhere else, so a snippet with a blank to fill puts the user
    // in the blank - no dialog, no hunting with the arrow keys.
    std::size_t firstGap = std::u32string::npos;

    for (const Token& token : tokens_) {
        switch (token.kind) {
        case Kind::Literal:
            out.text += token.text;
            break;
        case Kind::Date:
        case Kind::Time:
            out.text += formatTime(context.now, token.text);
            break;
        case Kind::Clipboard:
            out.text += context.clipboard;
            break;
        case Kind::Cursor:
            if (caret == std::u32string::npos) {
                caret = out.text.size();
            } else {
                // A second caret is not a caret. Keep it visible instead of dropping it.
                out.text += U"{cursor}";
            }
            break;
        case Kind::Param: {
            const auto it = context.params.find(token.text);
            // An unanswered parameter renders as nothing: leaving "{param:Tên}" in a
            // message the user is about to send would be worse. The gap it leaves is
            // where the caret goes, so the blank is the place they are already typing.
            if (it != context.params.end()) {
                out.text += it->second;
            } else if (firstGap == std::u32string::npos) {
                firstGap = out.text.size();
            }
            break;
        }
        case Kind::Variable: {
            const auto it = context.variables.find(token.text);
            if (it != context.variables.end()) {
                out.text += it->second;
            } else {
                // Not a variable this user defined, so it was never a hole: show it back.
                out.text += U'{';
                out.text += token.text;
                out.text += U'}';
            }
            break;
        }
        }
    }
    // An explicit {cursor} wins: the user said where they want to be, and a blank they
    // left empty on purpose should not drag the caret away from it.
    const std::size_t landing = caret != std::u32string::npos ? caret : firstGap;
    if (landing != std::u32string::npos) {
        out.cursorOffsetFromEnd = static_cast<int>(out.text.size() - landing);
    }
    return out;
}

} // namespace lankey::core::snippet
