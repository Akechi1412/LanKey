#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "core/smart/suggest/PhraseTrie.h"

namespace lankey::core::smart {
namespace {

using model::LexiconEntry;
using model::Phrase;
using model::Syllable;

Phrase phrase(const std::u32string& joined) {
    Phrase p;
    std::u32string cur;
    for (const char32_t c : joined) {
        if (c == U' ') {
            p.syllables.push_back(Syllable::fromComposed(cur));
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    if (!cur.empty()) p.syllables.push_back(Syllable::fromComposed(cur));
    return p;
}

PhraseTrie::Item item(const std::u32string& joined, double score) {
    PhraseTrie::Item it;
    it.entry.phrase = phrase(joined);
    it.entry.frequency = 10;
    it.staticScore = score;
    return it;
}

std::vector<std::u32string> keys(const std::vector<const PhraseTrie::Item*>& items) {
    std::vector<std::u32string> out;
    for (const auto* i : items)
        out.push_back(i->entry.phrase.joined());
    return out;
}

TEST(PhraseTrie, FindExactKeys) {
    const auto t = PhraseTrie::build(
        {item(U"chương", 1), item(U"chương trình", 2), item(U"chuyên", 3), item(U"chuyển", 4)});
    EXPECT_EQ(t.size(), 4u);
    ASSERT_NE(t.find(U"chương trình"), nullptr);
    EXPECT_EQ(t.find(U"chương trình")->staticScore, 2);
    EXPECT_NE(t.find(U"chuyên"), nullptr);
    EXPECT_EQ(t.find(U"chu"), nullptr);
    EXPECT_EQ(t.find(U"chương trình học"), nullptr);
    EXPECT_EQ(t.find(U""), nullptr);
}

TEST(PhraseTrie, RadixCompressionKeepsNodeCountSmall) {
    const auto t = PhraseTrie::build(
        {item(U"chương", 1), item(U"chương trình", 2), item(U"chuyên", 3), item(U"chuyển", 4)});
    // root, "ch", "ương", " trình", "uy", "ên", "ển" = 7 nodes, not one per code point.
    EXPECT_EQ(t.nodeCount(), 7u);
}

TEST(PhraseTrie, CollectReturnsBestFirst) {
    const auto t =
        PhraseTrie::build({item(U"chương", 1.0), item(U"chương trình", 5.0), item(U"chuyên", 3.0),
                           item(U"chuyển", 4.0), item(U"cá", 9.0)});
    std::vector<const PhraseTrie::Item*> out;
    t.collect(U"ch", 10, out);
    EXPECT_EQ(keys(out),
              (std::vector<std::u32string>{U"chương trình", U"chuyển", U"chuyên", U"chương"}));
}

TEST(PhraseTrie, CollectHonoursLimitAndMidEdgePrefix) {
    const auto t = PhraseTrie::build({item(U"chương", 1.0), item(U"chương trình", 5.0),
                                      item(U"chuyên", 3.0), item(U"chuyển", 4.0)});
    std::vector<const PhraseTrie::Item*> out;
    t.collect(U"chươ", 1, out); // prefix ends inside the "ương" edge
    EXPECT_EQ(keys(out), (std::vector<std::u32string>{U"chương trình"}));
    out.clear();
    t.collect(U"chương tr", 10, out);
    EXPECT_EQ(keys(out), (std::vector<std::u32string>{U"chương trình"}));
    out.clear();
    t.collect(U"xyz", 10, out);
    EXPECT_TRUE(out.empty());
    out.clear();
    t.collect(U"chươngz", 10, out);
    EXPECT_TRUE(out.empty());
}

TEST(PhraseTrie, DuplicateKeysKeepHigherScore) {
    const auto t = PhraseTrie::build({item(U"a b", 1.0), item(U"a b", 7.0)});
    ASSERT_NE(t.find(U"a b"), nullptr);
    EXPECT_EQ(t.find(U"a b")->staticScore, 7.0);
}

TEST(PhraseTrie, EmptyTrieIsSafe) {
    const PhraseTrie t;
    std::vector<const PhraseTrie::Item*> out;
    t.collect(U"a", 5, out);
    EXPECT_TRUE(out.empty());
    EXPECT_EQ(t.find(U"a"), nullptr);
    const auto built = PhraseTrie::build({});
    built.collect(U"", 5, out);
    EXPECT_TRUE(out.empty());
}

TEST(PhraseTrie, TenThousandPhrasesPrefixQueryFindsLongerPhraseFirst) {
    std::vector<PhraseTrie::Item> items;
    for (int i = 0; i < 10000; ++i) {
        std::u32string k = U"w";
        for (int v = i; v > 0; v /= 26)
            k.push_back(static_cast<char32_t>(U'a' + static_cast<unsigned>(v % 26)));
        items.push_back(item(k, 0.001 * i));
    }
    items.push_back(item(U"chương", 2.0));
    items.push_back(item(U"chương trình", 3.0));
    const auto t = PhraseTrie::build(std::move(items));
    EXPECT_EQ(t.size(), 10002u);
    std::vector<const PhraseTrie::Item*> out;
    t.collect(U"chương tr", 5, out);
    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(out[0]->entry.phrase.joined(), U"chương trình");
    out.clear();
    t.collect(U"ch", 5, out);
    ASSERT_EQ(out.size(), 2u);
    EXPECT_EQ(out[0]->entry.phrase.joined(), U"chương trình");
}

} // namespace
} // namespace lankey::core::smart
