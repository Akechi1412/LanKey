#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "core/model/Error.h"
#include "core/model/Language.h"
#include "core/model/Settings.h"

namespace lankey::core::convert {

// One row of the user's glossary. Any column except `note` may be empty; a row with fewer
// than two filled columns has nothing to convert and is dropped at load time.
struct Entry {
    std::u32string vi;
    std::u32string en;
    std::u32string ja;
    std::u32string note;
};

// The VI-EN-JA glossary the user keeps in dictionary.csv, indexed for lookup while typing
// and for the convert-selection hotkey.
//
// This is NOT a translation dictionary and must never be treated as one: every row was
// written by the person using it. That is what makes it safe to put its text on screen
// without asking, and why a row beats anything the smart layer guessed - it is the one
// source in LanKey that knows what this user means by a term.
//
// Keys are NFC + lowercased, with diacritics KEPT: "Đăng Nhập" and "đăng nhập" are the
// same entry, "dang nhap" is not. Folding diacritics away would make every toneless typo
// match some glossary term.
//
// Threading: built on the worker/UI thread, then published read-only to the hook thread.
// Every const member is safe to call from any thread once loading has finished.
class ConversionIndex {
public:
    // A run of trailing syllables that matched a glossary row.
    struct Match {
        const Entry* entry = nullptr;
        int syllablesMatched = 0;
    };

    // Replaces the contents with the rows of `csv` (UTF-8, BOM and CRLF tolerated).
    // The header must be the four expected column names; anything else is a file this
    // code does not understand, and guessing at its columns would put the wrong words on
    // the user's screen.
    [[nodiscard]] lk::expected<void> load(std::string_view csv);

    // The rows as they are kept in settings.json. This is where they come from now; the
    // CSV overload is only still here to import a dictionary.csv written by an older
    // version. Cannot fail: a row the index cannot use is skipped, the same as in a file.
    void load(const std::vector<model::GlossaryEntry>& rows);

    [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }
    // Longest row in syllables, clamped to kMaxSyllables: how far back lookupTyped looks.
    [[nodiscard]] int maxSyllables() const noexcept { return maxSyllables_; }

    // The row whose `language` column is exactly `text` (case-insensitively), or nullptr.
    [[nodiscard]] const Entry* find(model::Language language, std::u32string_view text) const;

    // The longest run of trailing syllables of `window` that is a row of the `language`
    // column. Tries kMaxSyllables syllables first, then fewer.
    [[nodiscard]] Match lookupTyped(model::Language language,
                                    const std::vector<std::u32string>& window) const;
    // The hook thread holds its context as views into the phrase window; taking them by
    // view keeps the lookup allocation-free on the typing path.
    [[nodiscard]] Match lookupTyped(model::Language language,
                                    const std::vector<std::u32string_view>& window) const;

    // The next filled column after whichever one `text` sits in, and which language that
    // is. The caller needs both: it has to label what it just put on screen.
    struct Conversion {
        std::u32string text;
        model::Language language = model::Language::Vietnamese;
        [[nodiscard]] bool found() const noexcept { return !text.empty(); }
    };

    // The `target` column of whichever row `text` appears in, from any of its columns.
    // `found()` is false when the text is not in the glossary, when that row leaves the
    // target column empty, or when the text already IS the target column - there is
    // nothing to do in any of those cases and the caller says so.
    [[nodiscard]] Conversion translate(std::u32string_view text, model::Language target) const;

    // Every row, in the order they appear in the file. The settings window shows them;
    // nothing else needs the order.
    template <typename Fn>
    void forEach(Fn&& fn) const {
        for (const Entry& e : entries_)
            fn(e);
    }

    // Rows are at most this many syllables; longer ones are still stored and still found
    // by find()/cycle(), but lookupTyped() will not reach past this while typing.
    static constexpr int kMaxSyllables = 3;

private:
    // One body for both lookupTyped overloads: the hook thread passes views, everyone
    // else passes strings, and the joining logic should exist once.
    template <typename Range>
    [[nodiscard]] Match lookupIn(model::Language language, const Range& window) const;

    void clear();
    // Stores one row and indexes its filled columns. Returns false when the row has
    // nothing to convert between.
    bool add(Entry e);

    std::vector<Entry> entries_;
    // key (NFC, lowercased) -> index into entries_
    std::unordered_map<std::u32string, std::size_t> byVi_;
    std::unordered_map<std::u32string, std::size_t> byEn_;
    std::unordered_map<std::u32string, std::size_t> byJa_;
    int maxSyllables_ = 0;
};

} // namespace lankey::core::convert
