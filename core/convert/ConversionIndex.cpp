#include "core/convert/ConversionIndex.h"

#include <algorithm>

#include "core/text/Utf.h"
#include "core/text/VietnameseText.h"

namespace lankey::core::convert {

using model::Error;
using model::Language;

namespace {

// One physical line of the file, honouring quotes: a quoted field may contain commas,
// newlines and doubled quotes. Returns the fields of the next record and advances `pos`.
// Returns false at end of input.
bool nextRecord(std::string_view csv, std::size_t& pos, std::vector<std::string>& fields) {
    fields.clear();
    if (pos >= csv.size()) return false;
    std::string field;
    bool quoted = false;
    bool any = false;
    while (pos < csv.size()) {
        const char c = csv[pos++];
        if (quoted) {
            if (c != '"') {
                field.push_back(c);
                continue;
            }
            if (pos < csv.size() && csv[pos] == '"') { // "" inside a quoted field
                field.push_back('"');
                ++pos;
                continue;
            }
            quoted = false;
            continue;
        }
        if (c == '"' && field.empty()) {
            quoted = true;
            any = true;
            continue;
        }
        if (c == ',') {
            fields.push_back(std::move(field));
            field.clear();
            any = true;
            continue;
        }
        if (c == '\n') break;
        if (c == '\r') continue; // CRLF from Excel and Notepad
        field.push_back(c);
        any = true;
    }
    fields.push_back(std::move(field));
    return any || !fields.empty();
}

std::string trimmed(std::string_view s) {
    const auto isSpace = [](unsigned char c) { return c == ' ' || c == '\t'; };
    std::size_t b = 0;
    std::size_t e = s.size();
    while (b < e && isSpace(static_cast<unsigned char>(s[b])))
        ++b;
    while (e > b && isSpace(static_cast<unsigned char>(s[e - 1])))
        --e;
    return std::string(s.substr(b, e - b));
}

std::u32string key(std::u32string_view text) {
    return text::caseFold(text::nfc(text));
}

int syllableCount(std::u32string_view text) {
    int n = 0;
    bool inWord = false;
    for (const char32_t c : text) {
        const bool space = c == U' ';
        if (!space && !inWord) ++n;
        inWord = !space;
    }
    return n;
}

} // namespace

void ConversionIndex::clear() {
    entries_.clear();
    byVi_.clear();
    byEn_.clear();
    byJa_.clear();
    maxSyllables_ = 0;
}

bool ConversionIndex::add(Entry e) {
    // A row needs at least two filled columns to convert between; one column (or a note by
    // itself) is a line the user is still working on, not a mistake to report.
    const int filled = (e.vi.empty() ? 0 : 1) + (e.en.empty() ? 0 : 1) + (e.ja.empty() ? 0 : 1);
    if (filled < 2) return false;

    const std::size_t at = entries_.size();
    entries_.push_back(std::move(e));
    const Entry& stored = entries_.back();
    // A repeated key takes the last row: the user edits top to bottom and the line they
    // wrote last is the one they meant.
    if (!stored.vi.empty()) byVi_[key(stored.vi)] = at;
    if (!stored.en.empty()) byEn_[key(stored.en)] = at;
    if (!stored.ja.empty()) byJa_[key(stored.ja)] = at;
    maxSyllables_ = (std::max)(maxSyllables_, (std::min)(kMaxSyllables, syllableCount(stored.vi)));
    return true;
}

void ConversionIndex::load(const std::vector<model::GlossaryEntry>& rows) {
    clear();
    const auto column = [](const std::string& s) { return text::nfc(text::fromUtf8(trimmed(s))); };
    for (const auto& row : rows)
        add({column(row.vi), column(row.en), column(row.ja), column(row.note)});
}

lk::expected<void> ConversionIndex::load(std::string_view csv) {
    clear();

    if (csv.size() >= 3 && csv.compare(0, 3, "\xEF\xBB\xBF") == 0) csv.remove_prefix(3);

    std::size_t pos = 0;
    std::vector<std::string> fields;
    if (!nextRecord(csv, pos, fields)) {
        return lk::unexpected(Error::make(Error::Code::Corrupted, "dictionary.csv is empty"));
    }
    // The header is the contract. A file with other columns is somebody else's CSV, and
    // guessing which column is which would put the wrong words on the user's screen.
    static constexpr const char* kExpected[] = {"vi", "en", "ja", "note"};
    if (fields.size() < 4) {
        return lk::unexpected(Error::make(Error::Code::Corrupted,
                                          "dictionary.csv: expected the header vi,en,ja,note"));
    }
    for (std::size_t i = 0; i < 4; ++i) {
        std::string got = trimmed(fields[i]);
        for (char& c : got)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (got != kExpected[i]) {
            return lk::unexpected(Error::make(Error::Code::Corrupted,
                                              "dictionary.csv: expected the header vi,en,ja,note"));
        }
    }

    while (nextRecord(csv, pos, fields)) {
        const auto column = [&](std::size_t i) {
            return i < fields.size() ? text::nfc(text::fromUtf8(trimmed(fields[i])))
                                     : std::u32string();
        };
        add({column(0), column(1), column(2), column(3)});
    }
    return {};
}

const Entry* ConversionIndex::find(Language language, std::u32string_view text) const {
    const auto& table = language == Language::Vietnamese ? byVi_
                        : language == Language::English  ? byEn_
                                                         : byJa_;
    const auto it = table.find(key(text));
    return it == table.end() ? nullptr : &entries_[it->second];
}

namespace {

// The last `n` entries of `window`, joined with spaces - the shape of a glossary key.
template <typename Range>
void joinTail(const Range& window, int n, std::u32string& out) {
    out.clear();
    const auto total = static_cast<int>(window.size());
    for (int k = total - n; k < total; ++k) {
        if (!out.empty()) out.push_back(U' ');
        out.append(window[static_cast<std::size_t>(k)]);
    }
}

} // namespace

template <typename Range>
ConversionIndex::Match ConversionIndex::lookupIn(Language language, const Range& window) const {
    const auto available = static_cast<int>(window.size());
    static thread_local std::u32string joined;
    for (int n = (std::min)(maxSyllables_, available); n >= 1; --n) {
        joinTail(window, n, joined);
        if (const Entry* e = find(language, joined)) return {e, n};
    }
    return {};
}

ConversionIndex::Match
ConversionIndex::lookupTyped(Language language, const std::vector<std::u32string>& window) const {
    return lookupIn(language, window);
}

ConversionIndex::Match
ConversionIndex::lookupTyped(Language language,
                             const std::vector<std::u32string_view>& window) const {
    return lookupIn(language, window);
}

ConversionIndex::Conversion ConversionIndex::translate(std::u32string_view text,
                                                       Language target) const {
    const std::u32string k = key(text);
    // The row is looked up in every column, so a term is reachable from whichever
    // language the user happens to have selected.
    const std::unordered_map<std::u32string, std::size_t>* tables[] = {&byVi_, &byEn_, &byJa_};
    const auto column = static_cast<int>(target);
    for (int source = 0; source < 3; ++source) {
        const auto it = tables[source]->find(k);
        if (it == tables[source]->end()) continue;
        if (source == column) return {}; // already the language that was asked for
        const Entry& e = entries_[it->second];
        const std::u32string* columns[] = {&e.vi, &e.en, &e.ja};
        const std::u32string& wanted = *columns[column];
        if (wanted.empty()) return {}; // this row has nothing in that language
        return {wanted, target};
    }
    return {};
}

} // namespace lankey::core::convert
