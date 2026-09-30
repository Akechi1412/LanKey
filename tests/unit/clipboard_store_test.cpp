// The clipboard history (Phase 5 D).
//
// This one holds whatever the user copied, which is the most sensitive thing LanKey ever
// touches. So the tests are as much about what it REFUSES to keep as about what it keeps.

#include <string>

#include <gtest/gtest.h>

#include "core/clipboard/ClipboardStore.h"
#include "core/model/Thresholds.h"

namespace lankey::core::clipboard {
namespace {

using model::Thresholds;

// Copies are stamped with a clock the test controls, so "most recent" is not a race.
struct Store : testing::Test {
    ClipboardStore store;
    std::int64_t now = 1000;

    void copy(std::u32string text) { store.add(std::move(text), now += 1000); }
};

TEST_F(Store, KeepsWhatWasCopiedNewestFirst) {
    copy(U"một");
    copy(U"hai");
    copy(U"ba");
    ASSERT_EQ(store.size(), 3u);
    EXPECT_EQ(store.at(0).text, U"ba");
    EXPECT_EQ(store.at(2).text, U"một");
}

TEST_F(Store, CopyingTheSameThingAgainMovesItUpInsteadOfRepeatingIt) {
    // Copying the same snippet three times while working is normal; three identical rows
    // would push the rest of the history off the end for nothing.
    copy(U"một");
    copy(U"hai");
    copy(U"một");
    ASSERT_EQ(store.size(), 2u);
    EXPECT_EQ(store.at(0).text, U"một");
    EXPECT_EQ(store.at(1).text, U"hai");
}

TEST_F(Store, EmptyAndBlankCopiesAreNotHistory) {
    copy(U"");
    copy(U"   ");
    copy(U"\n\t ");
    EXPECT_EQ(store.size(), 0u);
}

TEST_F(Store, SomethingTooLongIsNotKeptAtAll) {
    // Not truncated: half a document pasted back is worse than nothing offered.
    copy(std::u32string(static_cast<std::size_t>(Thresholds::kClipboardMaxItemChars) + 1, U'x'));
    EXPECT_EQ(store.size(), 0u);
    copy(std::u32string(static_cast<std::size_t>(Thresholds::kClipboardMaxItemChars), U'x'));
    EXPECT_EQ(store.size(), 1u);
}

TEST_F(Store, TheOldestFallsOffTheEnd) {
    for (int i = 0; i < Thresholds::kClipboardHistoryItems + 5; ++i)
        copy(U"mục " + std::u32string(static_cast<std::size_t>(i) + 1, U'!'));
    EXPECT_EQ(store.size(), static_cast<std::size_t>(Thresholds::kClipboardHistoryItems));
}

// --- pinning -------------------------------------------------------------------------------

TEST_F(Store, PinnedItemsComeFirstAndSurviveTheCap) {
    copy(U"giữ lại");
    ASSERT_TRUE(store.setPinned(0, true));
    for (int i = 0; i < Thresholds::kClipboardHistoryItems + 5; ++i)
        copy(std::u32string(static_cast<std::size_t>(i) + 1, U'z'));
    // The pin is still there, at the top, and it did not cost an ordinary slot.
    EXPECT_EQ(store.at(0).text, U"giữ lại");
    EXPECT_TRUE(store.at(0).pinned);
    EXPECT_EQ(store.size(), static_cast<std::size_t>(Thresholds::kClipboardHistoryItems) + 1);
}

TEST_F(Store, UnpinningPutsAnItemBackAmongTheOrdinaryOnes) {
    copy(U"một");
    copy(U"hai");
    ASSERT_TRUE(store.setPinned(1, true)); // "một"
    EXPECT_EQ(store.at(0).text, U"một");
    ASSERT_TRUE(store.setPinned(0, false));
    // Back in recency order: "hai" was copied later.
    EXPECT_EQ(store.at(0).text, U"hai");
    EXPECT_EQ(store.at(1).text, U"một");
}

TEST_F(Store, CopyingAPinnedItemAgainLeavesItPinned) {
    copy(U"chữ ký");
    ASSERT_TRUE(store.setPinned(0, true));
    copy(U"khác");
    copy(U"chữ ký");
    ASSERT_EQ(store.size(), 2u);
    EXPECT_TRUE(store.at(0).pinned) << "a pin is the user's decision, not a side effect";
}

// --- removing ------------------------------------------------------------------------------

TEST_F(Store, OneItemCanBeRemoved) {
    copy(U"một");
    copy(U"hai");
    ASSERT_TRUE(store.remove(0));
    ASSERT_EQ(store.size(), 1u);
    EXPECT_EQ(store.at(0).text, U"một");
}

TEST_F(Store, ClearKeepsNothingNotEvenPins) {
    // "Xoá lịch sử" has to mean it. A pin the user forgot about surviving that would be a
    // nasty surprise.
    copy(U"một");
    store.setPinned(0, true);
    copy(U"hai");
    store.clear();
    EXPECT_EQ(store.size(), 0u);
}

TEST_F(Store, OutOfRangeIndexesAreRefusedRatherThanGuessed) {
    copy(U"một");
    EXPECT_FALSE(store.setPinned(5, true));
    EXPECT_FALSE(store.remove(5));
    EXPECT_EQ(store.size(), 1u);
}

// --- what gets written to disk ---------------------------------------------------------------

TEST_F(Store, OnlyPinnedItemsAreWorthKeepingBetweenSessions) {
    // The rest is what you copied this afternoon; it has no business outliving the session,
    // and every item that does is one more thing sitting on the disk.
    copy(U"tạm thời");
    copy(U"chữ ký");
    ASSERT_TRUE(store.setPinned(0, true));

    ClipboardStore restored;
    ASSERT_TRUE(restored.load(store.serializePinned()).has_value());
    ASSERT_EQ(restored.size(), 1u);
    EXPECT_EQ(restored.at(0).text, U"chữ ký");
    EXPECT_TRUE(restored.at(0).pinned);
}

TEST_F(Store, BrokenStoredDataIsRefusedRatherThanPartlyRead) {
    ClipboardStore restored;
    EXPECT_FALSE(restored.load("{ not json").has_value());
    EXPECT_EQ(restored.size(), 0u);
}

} // namespace
} // namespace lankey::core::clipboard
