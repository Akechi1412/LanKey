#include "core/snippet/SnippetIndex.h"

#include <algorithm>

#include <nlohmann/json.hpp>

#include "core/text/Utf.h"
#include "core/text/VietnameseText.h"

namespace lankey::core::snippet {

using model::Error;

namespace {

std::u32string folded(std::u32string_view s) {
    return text::caseFold(text::nfc(s));
}

std::u32string stringField(const nlohmann::json& object, const char* key) {
    const auto it = object.find(key);
    if (it == object.end() || !it->is_string()) return {};
    return text::nfc(text::fromUtf8(it->get<std::string>()));
}

} // namespace

std::u32string firstLine(std::u32string_view body) {
    const auto end = body.find(U'\n');
    return std::u32string(end == std::u32string_view::npos ? body : body.substr(0, end));
}

void SnippetIndex::clear() {
    snippets_.clear();
    byAbbr_.clear();
    variables_.clear();
    maxAbbrLength_ = 0;
    usesClipboard_ = false;
}

bool SnippetIndex::add(Snippet s) {
    // A row still being written - no trigger, or nothing to insert - is skipped rather
    // than reported: these are edited by hand and half-finished lines are normal.
    if (s.abbr.empty() || s.body.empty()) return false;
    s.parsed = SnippetTemplate::parse(s.body);

    // A repeated abbreviation takes the last row: the user edits top to bottom, and the
    // line they wrote last is the one they meant.
    const auto existing = byAbbr_.find(s.abbr);
    if (existing != byAbbr_.end()) {
        snippets_[existing->second] = std::move(s);
        return true;
    }
    maxAbbrLength_ = (std::max)(maxAbbrLength_, static_cast<int>(s.abbr.size()));
    byAbbr_[s.abbr] = snippets_.size();
    snippets_.push_back(std::move(s));
    return true;
}

void SnippetIndex::finish() {
    // Walked at the end rather than accumulated as rows arrive: a row that replaces an
    // earlier one must be able to take the clipboard back out again.
    usesClipboard_ = false;
    for (const Snippet& s : snippets_)
        usesClipboard_ = usesClipboard_ || s.parsed.usesClipboard();
}

void SnippetIndex::load(const model::SnippetSettings& settings) {
    clear();
    for (const auto& [name, value] : settings.variables)
        variables_[text::nfc(text::fromUtf8(name))] = text::nfc(text::fromUtf8(value));
    for (const auto& item : settings.items) {
        Snippet s;
        s.abbr = text::nfc(text::fromUtf8(item.abbr));
        s.body = text::nfc(text::fromUtf8(item.body));
        s.autoExpand = item.autoExpand;
        add(std::move(s));
    }
    finish();
}

lk::expected<void> SnippetIndex::load(std::string_view json) {
    clear();

    nlohmann::json root = nlohmann::json::parse(json, nullptr, /*allow_exceptions=*/false);
    if (root.is_discarded() || !root.is_object()) {
        return lk::unexpected(
            Error::make(Error::Code::Corrupted, "snippets.json is not a JSON object"));
    }

    if (const auto vars = root.find("variables"); vars != root.end() && vars->is_object()) {
        for (const auto& [name, value] : vars->items()) {
            if (!value.is_string()) continue;
            variables_[text::nfc(text::fromUtf8(name))] =
                text::nfc(text::fromUtf8(value.get<std::string>()));
        }
    }

    const auto list = root.find("snippets");
    if (list == root.end() || !list->is_array()) return {};
    for (const auto& item : *list) {
        if (!item.is_object()) continue;
        Snippet s;
        s.abbr = stringField(item, "abbr");
        s.body = stringField(item, "body");
        if (const auto autoIt = item.find("auto"); autoIt != item.end() && autoIt->is_boolean()) {
            s.autoExpand = autoIt->get<bool>();
        }
        add(std::move(s));
    }
    finish();
    return {};
}

const Snippet* SnippetIndex::find(std::u32string_view abbr) const {
    const auto it = byAbbr_.find(std::u32string(abbr));
    return it == byAbbr_.end() ? nullptr : &snippets_[it->second];
}

std::vector<const Snippet*> SnippetIndex::search(std::u32string_view text) const {
    const std::u32string needle = folded(text);
    std::vector<const Snippet*> out;
    for (const Snippet& s : snippets_) {
        if (needle.empty() || folded(s.abbr).find(needle) != std::u32string::npos ||
            folded(s.body).find(needle) != std::u32string::npos) {
            out.push_back(&s);
        }
    }
    return out;
}

} // namespace lankey::core::snippet
