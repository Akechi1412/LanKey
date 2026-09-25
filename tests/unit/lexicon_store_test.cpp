// ILexiconStore contract, run against every implementation. InMemoryLexiconStore is the
// reference; SqliteLexiconStore must behave identically.

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sqlite3.h>
#include <sstream>
#include <string>

#include <gtest/gtest.h>

#include "core/model/Thresholds.h"
#include "core/storage/Migrations.h"
#include "core/storage/SqliteLexiconStore.h"

#include "tests/fakes/FakeDataProtector.h"
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

// The protected variant: in-memory database persisted as a sealed image in a temp file.
struct ProtectedSqliteStore {};

template <>
struct Factory<ProtectedSqliteStore> {
    // The store keeps a raw pointer to the protector and the path must outlive it: park
    // both in a wrapper that is destroyed with the store.
    struct Owner final : ILexiconStore {
        tests::FakeDataProtector protector;
        std::filesystem::path path;
        std::unique_ptr<SqliteLexiconStore> store;
        ~Owner() override {
            store.reset();
            std::error_code ec;
            std::filesystem::remove(path, ec);
        }
        lk::expected<std::vector<LexiconEntry>> loadAll() override { return store->loadAll(); }
        lk::expected<void> applyDeltas(const model::LexiconDeltaBatch& b) override {
            return store->applyDeltas(b);
        }
        lk::expected<void> eraseAll() override { return store->eraseAll(); }
        lk::expected<int> cleanup(std::int64_t now) override { return store->cleanup(now); }
        lk::expected<void> removeEntries(const std::vector<std::u32string>& p) override {
            return store->removeEntries(p);
        }
        lk::expected<void> setBlocked(const std::vector<std::u32string>& p, bool b) override {
            return store->setBlocked(p, b);
        }
        lk::expected<void> setPinned(const std::vector<std::u32string>& p, bool b) override {
            return store->setPinned(p, b);
        }
        lk::expected<std::vector<model::CorrectionRule>> loadCorrections() override {
            return store->loadCorrections();
        }
        lk::expected<std::vector<std::u32string>> loadBlacklist() override {
            return store->loadBlacklist();
        }
        lk::expected<void> reinforceCorrection(std::u32string_view w, std::u32string_view c,
                                               double d, model::CorrectionRuleSource s,
                                               std::int64_t now) override {
            return store->reinforceCorrection(w, c, d, s, now);
        }
        lk::expected<bool> rejectCorrection(std::u32string_view w, double p,
                                            std::int64_t now) override {
            return store->rejectCorrection(w, p, now);
        }
        lk::expected<void> noteCorrectionApplied(std::u32string_view w) override {
            return store->noteCorrectionApplied(w);
        }
        lk::expected<void> blacklist(std::u32string_view p, std::int64_t now) override {
            return store->blacklist(p, now);
        }
    };
    static std::unique_ptr<ILexiconStore> make() {
        auto owner = std::make_unique<Owner>();
        owner->path =
            std::filesystem::temp_directory_path() /
            ("lankey-test-" + std::to_string(static_cast<long long>(std::rand())) + ".enc");
        owner->store =
            std::make_unique<SqliteLexiconStore>(owner->path.string(), &owner->protector);
        EXPECT_TRUE(owner->store->open().has_value());
        return owner;
    }
};

template <class T>
class LexiconStoreContract : public testing::Test {
protected:
    std::unique_ptr<ILexiconStore> store = Factory<T>::make();
};

using Implementations =
    testing::Types<InMemoryLexiconStore, SqliteLexiconStore, ProtectedSqliteStore>;
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

// -- Dictionary editing ---------------------------------------------------------------------

TYPED_TEST(LexiconStoreContract, RemoveBlockAndPinEntries) {
    ASSERT_TRUE(
        this->store
            ->applyDeltas(
                {{phrase({U"a"}), 1, 1}, {phrase({U"b"}), 1, 1}, {phrase({U"c", U"d"}), 1, 1}})
            .has_value());
    ASSERT_TRUE(this->store->removeEntries({U"a", U"không có"}).has_value());
    EXPECT_EQ(find(*this->store->loadAll(), U"a"), nullptr);
    ASSERT_TRUE(this->store->setBlocked({U"b"}, true).has_value());
    ASSERT_TRUE(this->store->setPinned({U"c d"}, true).has_value());
    const auto all = this->store->loadAll();
    ASSERT_TRUE(all.has_value());
    EXPECT_EQ(all->size(), 2u);
    EXPECT_TRUE(find(*all, U"b")->blocked);
    EXPECT_TRUE(find(*all, U"c d")->pinned);
    ASSERT_TRUE(this->store->setBlocked({U"b"}, false).has_value());
    EXPECT_FALSE(find(*this->store->loadAll(), U"b")->blocked);
    EXPECT_TRUE(this->store->removeEntries({}).has_value()); // empty batch is a no-op
}

// -- correction_map / blacklist ---------------------------------------------------------------

using model::CorrectionRule;
using model::CorrectionRuleSource;

const CorrectionRule* rule(const std::vector<CorrectionRule>& all, const std::u32string& wrong) {
    for (const auto& r : all) {
        if (r.wrong == wrong) return &r;
    }
    return nullptr;
}

TYPED_TEST(LexiconStoreContract, CorrectionsStartEmpty) {
    EXPECT_TRUE(this->store->loadCorrections()->empty());
    EXPECT_TRUE(this->store->loadBlacklist()->empty());
}

TYPED_TEST(LexiconStoreContract, ReinforceAccumulatesConfidenceCappedAtOne) {
    for (int i = 0; i < 6; ++i) {
        ASSERT_TRUE(this->store
                        ->reinforceCorrection(U"sữa lỗi", U"sửa lỗi", 0.2,
                                              CorrectionRuleSource::Learned, 100 + i)
                        .has_value());
    }
    const auto all = this->store->loadCorrections();
    ASSERT_TRUE(all.has_value());
    const auto* r = rule(*all, U"sữa lỗi");
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(r->correct, U"sửa lỗi");
    EXPECT_DOUBLE_EQ(r->confidence, 1.0);
    EXPECT_EQ(r->source, CorrectionRuleSource::Learned);
    EXPECT_EQ(r->timesRejected, 0);
}

TYPED_TEST(LexiconStoreContract, ReinforceWithNewTargetStartsOver) {
    ASSERT_TRUE(
        this->store
            ->reinforceCorrection(U"chuơng", U"chương", 0.6, CorrectionRuleSource::Learned, 1)
            .has_value());
    ASSERT_TRUE(
        this->store
            ->reinforceCorrection(U"chuơng", U"chướng", 0.2, CorrectionRuleSource::Learned, 2)
            .has_value());
    // Keep the vector alive: rule() returns a pointer into it, and `*expected` on a
    // temporary would leave that pointer dangling.
    const auto all = this->store->loadCorrections();
    ASSERT_TRUE(all.has_value());
    const auto* r = rule(*all, U"chuơng");
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(r->correct, U"chướng");
    EXPECT_DOUBLE_EQ(r->confidence, 0.2);
}

TYPED_TEST(LexiconStoreContract, RejectLowersConfidenceAndBlacklistsAtThreshold) {
    ASSERT_TRUE(
        this->store->reinforceCorrection(U"sữa", U"sửa", 0.8, CorrectionRuleSource::Learned, 1)
            .has_value());
    auto first = this->store->rejectCorrection(U"sữa", 0.3, 2);
    ASSERT_TRUE(first.has_value());
    EXPECT_FALSE(*first);
    {
        const auto all = this->store->loadCorrections();
        ASSERT_TRUE(all.has_value());
        const auto* r = rule(*all, U"sữa");
        ASSERT_NE(r, nullptr);
        EXPECT_NEAR(r->confidence, 0.5, 1e-9);
        EXPECT_EQ(r->timesRejected, 1);
    }
    auto second = this->store->rejectCorrection(U"sữa", 0.3, 3);
    ASSERT_TRUE(second.has_value());
    EXPECT_TRUE(*second);
    const auto black = this->store->loadBlacklist();
    ASSERT_TRUE(black.has_value());
    ASSERT_EQ(black->size(), 1u);
    EXPECT_EQ((*black)[0], U"sữa");
}

TYPED_TEST(LexiconStoreContract, RejectingADictionaryCorrectionCountsToo) {
    // No rule exists for a fuzzy dictionary correction; two Undos still blacklist it.
    ASSERT_FALSE(*this->store->rejectCorrection(U"nguời", 0.3, 1));
    ASSERT_TRUE(*this->store->rejectCorrection(U"nguời", 0.3, 2));
    EXPECT_EQ(this->store->loadBlacklist()->size(), 1u);
}

TYPED_TEST(LexiconStoreContract, ManualBlacklistAndAppliedCounter) {
    ASSERT_TRUE(this->store->blacklist(U"git", 1).has_value());
    ASSERT_TRUE(this->store->blacklist(U"git", 2).has_value()); // idempotent
    EXPECT_EQ(this->store->loadBlacklist()->size(), 1u);

    ASSERT_TRUE(this->store->reinforceCorrection(U"a", U"b", 0.2, CorrectionRuleSource::User, 1)
                    .has_value());
    ASSERT_TRUE(this->store->noteCorrectionApplied(U"a").has_value());
    ASSERT_TRUE(this->store->noteCorrectionApplied(U"a").has_value());
    const auto all = this->store->loadCorrections();
    ASSERT_TRUE(all.has_value());
    const auto* r = rule(*all, U"a");
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(r->timesApplied, 2);
    EXPECT_EQ(r->source, CorrectionRuleSource::User);
}

TYPED_TEST(LexiconStoreContract, EraseAllClearsCorrectionsAndBlacklist) {
    ASSERT_TRUE(this->store->reinforceCorrection(U"a", U"b", 0.2, CorrectionRuleSource::Learned, 1)
                    .has_value());
    ASSERT_TRUE(this->store->blacklist(U"c", 1).has_value());
    ASSERT_TRUE(this->store->eraseAll().has_value());
    EXPECT_TRUE(this->store->loadCorrections()->empty());
    EXPECT_TRUE(this->store->loadBlacklist()->empty());
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

// -- Protected (sealed image) mode ----------------------------------------------------------

struct TempEnc {
    std::filesystem::path path;
    TempEnc() {
        path = std::filesystem::temp_directory_path() /
               ("lankey-test-" + std::to_string(static_cast<long long>(std::rand())) + ".enc");
        std::filesystem::remove(path);
    }
    ~TempEnc() {
        std::error_code ec;
        std::filesystem::remove(path, ec);
        std::filesystem::remove(path.string() + ".tmp", ec);
        std::filesystem::remove(legacy(), ec);
    }
    [[nodiscard]] std::string legacy() const {
        std::string p = path.string();
        return p.substr(0, p.size() - 4) + ".db";
    }
};

std::string fileBytes(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::stringstream buf;
    buf << in.rdbuf();
    return buf.str();
}

TEST(ProtectedSqliteStore, PersistsSealedImageAndReopens) {
    const TempEnc t;
    tests::FakeDataProtector protector;
    {
        SqliteLexiconStore s(t.path.string(), &protector);
        ASSERT_TRUE(s.open().has_value()) << s.open().error().message;
        EXPECT_TRUE(s.isProtected());
        ASSERT_TRUE(s.applyDeltas({{phrase({U"chương", U"trình"}), 3, 100}}).has_value());
    }
    ASSERT_TRUE(std::filesystem::exists(t.path));
    const std::string bytes = fileBytes(t.path);
    EXPECT_EQ(bytes.substr(0, 8), "LANKEYDB");
    // Nothing readable on disk: not the SQLite header, not a table name, not the phrase.
    EXPECT_EQ(bytes.find("SQLite format"), std::string::npos);
    EXPECT_EQ(bytes.find("user_lexicon"), std::string::npos);
    EXPECT_EQ(bytes.find("tr\xc3\xacnh"), std::string::npos);
    EXPECT_FALSE(std::filesystem::exists(t.path.string() + ".tmp"));
    EXPECT_GT(protector.protectCalls, 0);

    SqliteLexiconStore again(t.path.string(), &protector);
    ASSERT_TRUE(again.open().has_value()) << again.open().error().message;
    const auto all = again.loadAll();
    ASSERT_TRUE(all.has_value());
    ASSERT_EQ(all->size(), 1u);
    EXPECT_EQ((*all)[0].frequency, 3u);
    EXPECT_EQ(*again.schemaVersion(), storage::latestSchemaVersion());
}

TEST(ProtectedSqliteStore, ImportsAndRemovesALegacyPlainDatabase) {
    const TempEnc t;
    {
        SqliteLexiconStore plain(t.legacy());
        ASSERT_TRUE(plain.open().has_value());
        ASSERT_TRUE(plain.applyDeltas({{phrase({U"cũ"}), 7, 1}}).has_value());
    }
    ASSERT_TRUE(std::filesystem::exists(t.legacy()));
    tests::FakeDataProtector protector;
    SqliteLexiconStore s(t.path.string(), &protector);
    ASSERT_TRUE(s.open().has_value()) << s.open().error().message;
    const auto* e = find(*s.loadAll(), U"cũ");
    ASSERT_NE(e, nullptr);
    EXPECT_EQ(e->frequency, 7u);
    // The readable copy (and any journal) is gone once the sealed image exists.
    EXPECT_FALSE(std::filesystem::exists(t.legacy()));
    EXPECT_FALSE(std::filesystem::exists(t.legacy() + "-wal"));
    EXPECT_TRUE(std::filesystem::exists(t.path));
}

TEST(ProtectedSqliteStore, RefusesAnImageSealedByAnotherScheme) {
    const TempEnc t;
    tests::FakeDataProtector protector;
    {
        SqliteLexiconStore s(t.path.string(), &protector);
        ASSERT_TRUE(s.open().has_value());
    }
    // Rewrite the header with a different scheme name.
    std::string bytes = fileBytes(t.path);
    ASSERT_GT(bytes.size(), 14u);
    bytes[9] = 5;
    bytes.replace(10, 4, "dpapi");
    {
        std::ofstream out(t.path, std::ios::binary | std::ios::trunc);
        out << bytes;
    }
    SqliteLexiconStore s(t.path.string(), &protector);
    const auto r = s.open();
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, model::Error::Code::Unsupported);
    EXPECT_FALSE(s.isOpen());
}

TEST(ProtectedSqliteStore, ProtectorFailureKeepsTheOldImage) {
    const TempEnc t;
    tests::FakeDataProtector protector;
    SqliteLexiconStore s(t.path.string(), &protector);
    ASSERT_TRUE(s.open().has_value());
    ASSERT_TRUE(s.applyDeltas({{phrase({U"a"}), 1, 1}}).has_value());
    const std::string before = fileBytes(t.path);
    protector.failNextCall();
    EXPECT_FALSE(s.applyDeltas({{phrase({U"b"}), 1, 1}}).has_value());
    EXPECT_EQ(fileBytes(t.path), before); // the write failed, the file did not change
    EXPECT_FALSE(std::filesystem::exists(t.path.string() + ".tmp"));
}

TEST(ProtectedSqliteStore, EraseAllLeavesOnlyAnEmptySealedImage) {
    const TempEnc t;
    tests::FakeDataProtector protector;
    SqliteLexiconStore s(t.path.string(), &protector);
    ASSERT_TRUE(s.open().has_value());
    ASSERT_TRUE(s.applyDeltas({{phrase({U"bí", U"mật"}), 5, 1}}).has_value());
    ASSERT_TRUE(s.eraseAll().has_value());
    EXPECT_TRUE(s.loadAll()->empty());
    const std::string bytes = fileBytes(t.path);
    EXPECT_EQ(bytes.find("m\xe1\xba\xadt"), std::string::npos);
    EXPECT_FALSE(std::filesystem::exists(t.legacy()));
}

TEST(SqliteLexiconStore, OpenFailsOnBadPath) {
    SqliteLexiconStore s("Z:/definitely/not/here/x.db");
    const auto r = s.open();
    EXPECT_FALSE(r.has_value());
    EXPECT_FALSE(s.isOpen());
}

} // namespace
} // namespace lankey::core
