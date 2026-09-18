// Sanity checks for the fakes themselves - and, by including every one of them, a
// compile check that every interface header in core/interfaces/ is self-contained.

#include <algorithm>

#include <gtest/gtest.h>

#include "core/model/AtomicSnapshot.h"

#include "tests/fakes/FakeCaretResolver.h"
#include "tests/fakes/FakeClock.h"
#include "tests/fakes/FakeCorrector.h"
#include "tests/fakes/FakeDataProtector.h"
#include "tests/fakes/FakeEngine.h"
#include "tests/fakes/FakeFocusObserver.h"
#include "tests/fakes/FakeKeySource.h"
#include "tests/fakes/FakeSuggestionProvider.h"
#include "tests/fakes/FakeTextSink.h"
#include "tests/fakes/InMemoryLexiconStore.h"

namespace lankey::tests {
namespace {

using core::model::LexiconDelta;
using core::model::Phrase;
using core::model::Syllable;

Phrase phrase(std::initializer_list<const char32_t*> parts) {
    Phrase p;
    for (const auto* s : parts)
        p.syllables.push_back(Syllable::fromComposed(s));
    return p;
}

TEST(InMemoryLexiconStore, UpsertAccumulatesFrequencyAndTimestamps) {
    InMemoryLexiconStore store;
    ASSERT_TRUE(store.applyDeltas({{phrase({U"chương", U"trình"}), 1, 100}}).has_value());
    ASSERT_TRUE(store.applyDeltas({{phrase({U"chương", U"trình"}), 2, 200}}).has_value());
    const auto* e = store.find(U"chương trình");
    ASSERT_NE(e, nullptr);
    EXPECT_EQ(e->frequency, 3u);
    EXPECT_EQ(e->firstSeenAt, 100);
    EXPECT_EQ(e->lastUsedAt, 200);
    EXPECT_EQ(e->syllableCount(), 2);
}

TEST(InMemoryLexiconStore, LoadAllAndEraseAll) {
    InMemoryLexiconStore store;
    ASSERT_TRUE(store.applyDeltas({{phrase({U"a"}), 1, 1}, {phrase({U"b"}), 1, 1}}).has_value());
    auto all = store.loadAll();
    ASSERT_TRUE(all.has_value());
    EXPECT_EQ(all->size(), 2u);
    ASSERT_TRUE(store.eraseAll().has_value());
    EXPECT_EQ(store.size(), 0u);
}

TEST(FakeDataProtector, RoundTripsHidesPlaintextAndFails) {
    FakeDataProtector p;
    const std::vector<std::uint8_t> plain = {'s', 'e', 'c', 'r', 'e', 't'};
    const auto sealed = p.protect(plain);
    ASSERT_TRUE(sealed.has_value());
    EXPECT_EQ(std::search(sealed->begin(), sealed->end(), plain.begin(), plain.end()),
              sealed->end());
    const auto back = p.unprotect(*sealed);
    ASSERT_TRUE(back.has_value());
    EXPECT_EQ(*back, plain);
    EXPECT_FALSE(p.unprotect(plain).has_value()); // not something we sealed
    p.failNextCall();
    EXPECT_FALSE(p.protect(plain).has_value());
    EXPECT_EQ(p.scheme(), "fake");
}

TEST(FakeClock, AdvancesBothClocks) {
    FakeClock clock;
    const auto u0 = clock.nowUnixSeconds();
    clock.advanceMs(2500);
    EXPECT_EQ(clock.nowMonotonicMs(), 2500);
    EXPECT_EQ(clock.nowUnixSeconds(), u0 + 2);
}

TEST(FakeFocusObserver, NotifiesAndPublishesSnapshot) {
    FakeFocusObserver f;
    int calls = 0;
    f.onChange([&](const core::model::FocusContext&) { ++calls; });
    f.setFocus({"a.exe", true, 1});
    EXPECT_EQ(calls, 1);
    EXPECT_EQ(f.current()->appName, "a.exe");
    EXPECT_TRUE(f.current()->isPasswordField);
}

TEST(AtomicSnapshot, LoadStoreRoundTrip) {
    core::model::AtomicSnapshot<int> snap;
    EXPECT_EQ(snap.load(), nullptr);
    snap.store(std::make_shared<const int>(7));
    const auto p = snap.load();
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(*p, 7);
    snap.store(std::make_shared<const int>(8));
    EXPECT_EQ(*p, 7); // old snapshot stays valid for whoever holds it
    EXPECT_EQ(*snap.load(), 8);
}

TEST(FakeTextSinkAndOthers, Compile) {
    FakeTextSink sink;
    FakeCaretResolver caret;
    FakeCorrector corrector;
    FakeSuggestionProvider suggestions;
    FakeKeySource keys;
    FakeEngine engine;
    EXPECT_FALSE(caret.resolve().has_value());
    EXPECT_FALSE(keys.press({})); // not started -> nothing happens
    EXPECT_TRUE(suggestions.suggest({}).empty());
    EXPECT_FALSE(corrector.check({}).has_value());
    EXPECT_TRUE(sink.screen().empty());
    EXPECT_EQ(engine.settings().inputMethod, core::model::InputMethod::Telex);
}

} // namespace
} // namespace lankey::tests
