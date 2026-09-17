// ILexiconStore contract, run against every implementation. InMemoryLexiconStore is the
// reference; SqliteLexiconStore must behave identically.

#include <cstdio>
#include <filesystem>
#include <memory>
#include <sqlite3.h>
#include <string>

#include <gtest/gtest.h>

#include "core/model/Thresholds.h"
#include "core/storage/Migrations.h"
#include "core/storage/SqliteLexiconStore.h"

#include "tests/fakes/InMemoryLexiconStore.h"

namespace lankey::core {
namespace {

using model::LexiconDelta;
using model::LexiconEntry;
using model::Phrase;
using model::Syllable;
using storage::SqliteLexiconStore;
using tests::InMemoryLexiconStore;

Phrase phrase(std::initializer_list<const char32_t*> parts) {
    Phrase p;
    for (const auto* s : parts)
        p.syllables.push_back(Syllable::fromComposed(s));
    return p;
}

const LexiconEntry* find(const std::vector<LexiconEntry>& all, const std::u32string& joined) {
    for (const auto& e : all) {
        if (e.phrase.joined() == joined) return &e;
    }
    return nullptr;
}

template <class T>
struct Factory;

template <>
struct Factory<InMemoryLexiconStore> {
    static std::unique_ptr<ILexiconStore> make() {
        return std::make_unique<InMemoryLexiconStore>();
    }
};

template <>
struct Factory<SqliteLexiconStore> {
    static std::unique_ptr<ILexiconStore> make() {
        auto s = std::make_unique<SqliteLexiconStore>(":memory:");
        EXPECT_TRUE(s->open().has_value());
        return s;
    }
};

template <class T>
class LexiconStoreContract : public testing::Test {
protected:
    std::unique_ptr<ILexiconStore> store = Factory<T>::make();
};

using Implementations = testing::Types<InMemoryLexiconStore, SqliteLexiconStore>;
TYPED_TEST_SUITE(LexiconStoreContract, Implementations);

TYPED_TEST(LexiconStoreContract, StartsEmpty) {
    const auto all = this->store->loadAll();
    ASSERT_TRUE(all.has_value());
    EXPECT_TRUE(all->empty());
}

TYPED_TEST(LexiconStoreContract, UpsertAccumulatesFrequencyAndKeepsFirstSeen) {
    ASSERT_TRUE(this->store->applyDeltas({{phrase({U"chương", U"trình"}), 1, 100}}).has_value());
    ASSERT_TRUE(this->store->applyDeltas({{phrase({U"chương", U"trình"}), 2, 200}}).has_value());
    const auto all = this->store->loadAll();
    ASSERT_TRUE(all.has_value());
    const auto* e = find(*all, U"chương trình");
    ASSERT_NE(e, nullptr);
    EXPECT_EQ(e->frequency, 3u);
    EXPECT_EQ(e->firstSeenAt, 100);
    EXPECT_EQ(e->lastUsedAt, 200);
    EXPECT_EQ(e->syllableCount(), 2);
    EXPECT_FALSE(e->pinned);
    EXPECT_FALSE(e->blocked);
}

TYPED_TEST(LexiconStoreContract, LastUsedNeverGoesBackwards) {
    ASSERT_TRUE(this->store->applyDeltas({{phrase({U"a"}), 1, 500}}).has_value());
    ASSERT_TRUE(this->store->applyDeltas({{phrase({U"a"}), 1, 300}}).has_value());
    const auto all = this->store->loadAll();
    EXPECT_EQ(find(*all, U"a")->lastUsedAt, 500);
}

TYPED_TEST(LexiconStoreContract, BatchIsAppliedAsAWhole) {
    ASSERT_TRUE(
        this->store
            ->applyDeltas(
                {{phrase({U"a"}), 1, 1}, {phrase({U"b"}), 1, 1}, {phrase({U"a", U"b"}), 1, 1}})
            .has_value());
    const auto all = this->store->loadAll();
    ASSERT_TRUE(all.has_value());
    EXPECT_EQ(all->size(), 3u);
}

TYPED_TEST(LexiconStoreContract, EmptyBatchIsNoop) {
    ASSERT_TRUE(this->store->applyDeltas({}).has_value());
    EXPECT_TRUE(this->store->loadAll()->empty());
}

TYPED_TEST(LexiconStoreContract, EraseAllLeavesNothing) {
    ASSERT_TRUE(this->store->applyDeltas({{phrase({U"a"}), 1, 1}}).has_value());
    ASSERT_TRUE(this->store->eraseAll().has_value());
    EXPECT_TRUE(this->store->loadAll()->empty());
    // Still usable afterwards.
    ASSERT_TRUE(this->store->applyDeltas({{phrase({U"b"}), 1, 1}}).has_value());
    EXPECT_EQ(this->store->loadAll()->size(), 1u);
}

// -- SQLite-specific: files, persistence, migrations, cleanup ------------------------------

struct TempDb {
    std::filesystem::path path;
    TempDb() {
        path = std::filesystem::temp_directory_path() /
               ("lankey-test-" + std::to_string(static_cast<long long>(std::rand())) + ".db");
        std::filesystem::remove(path);
    }
    ~TempDb() {
        std::error_code ec;
        std::filesystem::remove(path, ec);
        std::filesystem::remove(path.string() + "-wal", ec);
        std::filesystem::remove(path.string() + "-shm", ec);
    }
};

TEST(SqliteLexiconStore, PersistsAcrossReopen) {
    const TempDb db;
    {
        SqliteLexiconStore s(db.path.string());
        ASSERT_TRUE(s.open().has_value()) << s.open().error().message;
        ASSERT_TRUE(s.applyDeltas({{phrase({U"hệ", U"điều", U"hành"}), 4, 77}}).has_value());
        EXPECT_EQ(*s.schemaVersion(), storage::latestSchemaVersion());
    }
    SqliteLexiconStore s(db.path.string());
    ASSERT_TRUE(s.open().has_value());
    const auto all = s.loadAll();
    ASSERT_TRUE(all.has_value());
    ASSERT_EQ(all->size(), 1u);
    EXPECT_EQ((*all)[0].phrase.joined(), U"hệ điều hành");
    EXPECT_EQ((*all)[0].frequency, 4u);
}

TEST(SqliteLexiconStore, MigratesV1ToLatestKeepingRows) {
    const TempDb db;
    {
        // Hand-build a v1 file: 3-syllable limit, one row.
        sqlite3* raw = nullptr;
        ASSERT_EQ(sqlite3_open(db.path.string().c_str(), &raw), SQLITE_OK);
        ASSERT_EQ(sqlite3_exec(raw, storage::migrations()[0].sql, nullptr, nullptr, nullptr),
                  SQLITE_OK);
        ASSERT_EQ(sqlite3_exec(raw,
                               "INSERT INTO user_lexicon (phrase, phrase_nosign, syllable_count, "
                               "frequency, first_seen_at, last_used_at) VALUES "
                               "('chương trình','chuong trinh',2,7,1,2)",
                               nullptr, nullptr, nullptr),
                  SQLITE_OK);
        sqlite3_close(raw);
    }
    SqliteLexiconStore s(db.path.string());
    ASSERT_TRUE(s.open().has_value()) << s.open().error().message;
    EXPECT_EQ(*s.schemaVersion(), storage::latestSchemaVersion());
    const auto all = s.loadAll();
    ASSERT_TRUE(all.has_value());
    ASSERT_EQ(all->size(), 1u);
    EXPECT_EQ((*all)[0].frequency, 7u);
    // 5-syllable phrases are accepted after the migration.
    ASSERT_TRUE(s.applyDeltas({{phrase({U"a", U"b", U"c", U"d", U"e"}), 1, 3}}).has_value());
    EXPECT_EQ(s.loadAll()->size(), 2u);
}

TEST(SqliteLexiconStore, RefusesNewerSchema) {
    const TempDb db;
    {
        SqliteLexiconStore s(db.path.string());
        ASSERT_TRUE(s.open().has_value());
    }
    {
        // Pretend a future build bumped the schema.
        sqlite3* raw = nullptr;
        ASSERT_EQ(sqlite3_open(db.path.string().c_str(), &raw), SQLITE_OK);
        ASSERT_EQ(sqlite3_exec(raw, "UPDATE meta SET value='999' WHERE key='schema_version'",
                               nullptr, nullptr, nullptr),
                  SQLITE_OK);
        sqlite3_close(raw);
    }
    SqliteLexiconStore s(db.path.string());
    const auto r = s.open();
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, model::Error::Code::Unsupported);
    EXPECT_FALSE(s.isOpen());
}

TEST(SqliteLexiconStore, CleanupDropsStaleAndEnforcesLimit) {
    SqliteLexiconStore s(":memory:");
    ASSERT_TRUE(s.open().has_value());
    constexpr std::int64_t kNow = 1'700'000'000;
    constexpr std::int64_t kDay = 86400;
    ASSERT_TRUE(
        s.applyDeltas({
                          {phrase({U"fresh"}), 1, kNow},                    // kept
                          {phrase({U"stale"}), 1, kNow - 100 * kDay},       // freq 1, > 90 days
                          {phrase({U"old", U"pair"}), 1, kNow - 50 * kDay}, // 2-syl, > 45 days
                          {phrase({U"rare"}), 2, kNow - 200 * kDay},        // freq < 3, > 180 days
                          {phrase({U"kept"}), 2, kNow - 100 * kDay}, // freq < 3 but recent enough
                      })
            .has_value());
    const auto removed = s.cleanup(kNow);
    ASSERT_TRUE(removed.has_value()) << removed.error().message;
    EXPECT_EQ(*removed, 3);
    const auto all = s.loadAll();
    EXPECT_NE(find(*all, U"fresh"), nullptr);
    EXPECT_NE(find(*all, U"kept"), nullptr);
    EXPECT_EQ(find(*all, U"stale"), nullptr);
    EXPECT_EQ(find(*all, U"old pair"), nullptr);
    EXPECT_EQ(find(*all, U"rare"), nullptr);
}

TEST(SqliteLexiconStore, OpenFailsOnBadPath) {
    SqliteLexiconStore s("Z:/definitely/not/here/x.db");
    const auto r = s.open();
    EXPECT_FALSE(r.has_value());
    EXPECT_FALSE(s.isOpen());
}

} // namespace
} // namespace lankey::core
