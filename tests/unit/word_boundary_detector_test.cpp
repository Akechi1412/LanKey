#include <gtest/gtest.h>

#include "core/pipeline/WordBoundaryDetector.h"

#include "tests/support/Typist.h"

namespace lankey::core::pipeline {
namespace {

using model::KeyEvent;
using model::VirtualKey;
using tests::Typist;

KeyEvent named(VirtualKey k) {
    KeyEvent e;
    e.key = k;
    return e;
}

TEST(WordBoundaryDetector, LettersDigitsBackspaceAreNotBoundaries) {
    EXPECT_EQ(WordBoundaryDetector::classify(Typist::toKeyEvent('a')), Boundary::None);
    EXPECT_EQ(WordBoundaryDetector::classify(Typist::toKeyEvent('6')), Boundary::None);
    EXPECT_EQ(WordBoundaryDetector::classify(named(VirtualKey::Backspace)), Boundary::None);
}

TEST(WordBoundaryDetector, SpaceAndPhrasePunctuationEndSyllableOnly) {
    EXPECT_EQ(WordBoundaryDetector::classify(Typist::toKeyEvent(' ')), Boundary::Syllable);
    EXPECT_EQ(WordBoundaryDetector::classify(named(VirtualKey::Tab)), Boundary::Syllable);
    EXPECT_EQ(WordBoundaryDetector::classify(Typist::toKeyEvent(',')), Boundary::Syllable);
    EXPECT_EQ(WordBoundaryDetector::classify(Typist::toKeyEvent('"')), Boundary::Syllable);
    EXPECT_EQ(WordBoundaryDetector::classify(Typist::toKeyEvent('(')), Boundary::Syllable);
    EXPECT_EQ(WordBoundaryDetector::classify(Typist::toKeyEvent('-')), Boundary::Syllable);
}

TEST(WordBoundaryDetector, SentenceEndAndNavigationEndParagraph) {
    for (const char c : {'.', '!', '?', ';', ':'}) {
        EXPECT_EQ(WordBoundaryDetector::classify(Typist::toKeyEvent(c)), Boundary::Paragraph) << c;
    }
    for (const auto k :
         {VirtualKey::Enter, VirtualKey::Escape, VirtualKey::Home, VirtualKey::End,
          VirtualKey::ArrowLeft, VirtualKey::ArrowUp, VirtualKey::Delete, VirtualKey::Unknown}) {
        EXPECT_EQ(WordBoundaryDetector::classify(named(k)), Boundary::Paragraph);
    }
}

TEST(WordBoundaryDetector, TerminatorReproducesWhatWasTyped) {
    EXPECT_EQ(WordBoundaryDetector::terminatorFor(Typist::toKeyEvent(' ')), U' ');
    EXPECT_EQ(WordBoundaryDetector::terminatorFor(Typist::toKeyEvent('.')), U'.');
    EXPECT_EQ(WordBoundaryDetector::terminatorFor(named(VirtualKey::Enter)), U'\n');
    EXPECT_EQ(WordBoundaryDetector::terminatorFor(named(VirtualKey::Tab)), U'\t');
    EXPECT_EQ(WordBoundaryDetector::terminatorFor(named(VirtualKey::ArrowLeft)), 0u);
}

} // namespace
} // namespace lankey::core::pipeline
