#include "core/pipeline/WordBoundaryDetector.h"

namespace lankey::core::pipeline {

using model::KeyEvent;
using model::VirtualKey;

namespace {

bool isSentenceEnd(char32_t c) noexcept {
    switch (c) {
    case U'.':
    case U'!':
    case U'?':
    case U';':
    case U':':
        return true;
    default:
        return false;
    }
}

} // namespace

Boundary WordBoundaryDetector::classify(const KeyEvent& key) noexcept {
    switch (key.key) {
    case VirtualKey::Enter:
    case VirtualKey::Escape:
    case VirtualKey::Home:
    case VirtualKey::End:
    case VirtualKey::ArrowLeft:
    case VirtualKey::ArrowRight:
    case VirtualKey::ArrowUp:
    case VirtualKey::ArrowDown:
    case VirtualKey::Delete:
        return Boundary::Paragraph;
    case VirtualKey::Space:
    case VirtualKey::Tab:
        return Boundary::Syllable;
    case VirtualKey::Punctuation:
        // Every printable non-letter ends the syllable; only sentence punctuation also
        // ends the phrase, so "chào, bạn" still links the two syllables.
        return isSentenceEnd(key.unicode) ? Boundary::Paragraph : Boundary::Syllable;
    default:
        break;
    }
    if (key.isLetter() || key.isDigit() || key.key == VirtualKey::Backspace) {
        return Boundary::None;
    }
    // Unknown key (function keys, media keys...): treat as navigation.
    return Boundary::Paragraph;
}

char32_t WordBoundaryDetector::terminatorFor(const KeyEvent& key) noexcept {
    switch (key.key) {
    case VirtualKey::Enter:
        return U'\n';
    case VirtualKey::Tab:
        return U'\t';
    case VirtualKey::Space:
        return U' ';
    case VirtualKey::Punctuation:
        return key.unicode;
    default:
        return 0;
    }
}

} // namespace lankey::core::pipeline
