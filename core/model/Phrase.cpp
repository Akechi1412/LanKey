#include "core/model/Phrase.h"

#include "core/text/VietnameseText.h"

namespace lankey::core::model {

Syllable Syllable::fromComposed(std::u32string_view composed) {
    Syllable s;
    s.text = text::caseFold(text::nfc(composed));
    return s;
}

std::u32string Phrase::joined() const {
    std::u32string out;
    for (const auto& s : syllables) {
        if (!out.empty()) out.push_back(U' ');
        out += s.text;
    }
    return out;
}

void PhraseWindow::commit(Syllable syllable) {
    if (committed.capacity() == 0) {
        committed.reserve(static_cast<std::size_t>(Thresholds::kMaxPhraseSyllables) + 1);
    }
    committed.push_back(std::move(syllable));
    if (committed.size() > static_cast<std::size_t>(Thresholds::kMaxPhraseSyllables)) {
        committed.erase(committed.begin()); // 5 moves, no allocation
    }
    current.clear();
}

void PhraseWindow::reset() noexcept {
    committed.clear();
    current.clear();
}

std::vector<Phrase> PhraseWindow::candidatePhrases() const {
    std::vector<Phrase> out;
    // Longest context first so callers can try the most specific phrase before falling
    // back ("s-2 s-1 s0" before "s-1 s0" before "s0").
    for (std::size_t len = committed.size(); len > 0; --len) {
        Phrase p;
        p.syllables.assign(committed.end() - static_cast<std::ptrdiff_t>(len), committed.end());
        out.push_back(std::move(p));
    }
    return out;
}

} // namespace lankey::core::model
