#include "core/storage/SqliteLexiconStore.h"

#include <sqlite3.h>
#include <string>
#include <string_view>
#include <utility>

#include "core/model/Thresholds.h"
#include "core/storage/Migrations.h"
#include "core/text/Utf.h"

namespace lankey::core::storage {

using model::Error;
using model::LexiconDeltaBatch;
using model::LexiconEntry;
using model::Phrase;
using model::Syllable;
using model::Thresholds;

namespace {

// Prepared statement with RAII finalisation. Strings cross the boundary as UTF-8 (the
// only place in core that converts from u32string to UTF-8 and back).
class Statement {
public:
    Statement(sqlite3* db, const char* sql) { sqlite3_prepare_v2(db, sql, -1, &stmt_, nullptr); }
    ~Statement() { sqlite3_finalize(stmt_); }
    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;

    [[nodiscard]] bool ok() const noexcept { return stmt_ != nullptr; }
    [[nodiscard]] sqlite3_stmt* get() const noexcept { return stmt_; }
    int step() { return sqlite3_step(stmt_); }
    void reset() {
        sqlite3_reset(stmt_);
        sqlite3_clear_bindings(stmt_);
    }

private:
    sqlite3_stmt* stmt_ = nullptr;
};

void bindText(sqlite3_stmt* stmt, int index, const std::string& utf8) {
    sqlite3_bind_text(stmt, index, utf8.c_str(), static_cast<int>(utf8.size()), SQLITE_TRANSIENT);
}

std::u32string columnU32(sqlite3_stmt* stmt, int index) {
    const auto* p = reinterpret_cast<const char*>(sqlite3_column_text(stmt, index));
    return p != nullptr ? text::fromUtf8(p) : std::u32string{};
}

const char* sourceName(model::CorrectionRuleSource s) noexcept {
    switch (s) {
    case model::CorrectionRuleSource::User:
        return "user";
    case model::CorrectionRuleSource::Builtin:
        return "builtin";
    case model::CorrectionRuleSource::Learned:
        break;
    }
    return "learned";
}

model::CorrectionRuleSource sourceFrom(const char* s) noexcept {
    using model::CorrectionRuleSource;
    if (s == nullptr) return CorrectionRuleSource::Learned;
    const std::string_view v(s);
    if (v == "user") return CorrectionRuleSource::User;
    if (v == "builtin") return CorrectionRuleSource::Builtin;
    return CorrectionRuleSource::Learned;
}

} // namespace

SqliteLexiconStore::SqliteLexiconStore(std::string path) : path_(std::move(path)) {}

SqliteLexiconStore::~SqliteLexiconStore() {
    close();
}

Error SqliteLexiconStore::lastError(const char* what) const {
    std::string msg = what;
    if (db_ != nullptr) {
        msg += ": ";
        msg += sqlite3_errmsg(db_);
    }
    return Error::make(Error::Code::Io, std::move(msg));
}

lk::expected<void> SqliteLexiconStore::exec(const char* sql) const {
    char* err = nullptr;
    if (sqlite3_exec(db_, sql, nullptr, nullptr, &err) != SQLITE_OK) {
        std::string msg = err != nullptr ? err : "sqlite3_exec failed";
        sqlite3_free(err);
        return lk::unexpected(Error::make(Error::Code::Io, std::move(msg)));
    }
    return {};
}

lk::expected<void> SqliteLexiconStore::open() {
    if (db_ != nullptr) return {};
    if (sqlite3_open_v2(path_.c_str(), &db_,
                        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_NOMUTEX,
                        nullptr) != SQLITE_OK) {
        const Error e = lastError("open");
        close();
        return lk::unexpected(e);
    }
    // PLAN 7.5. WAL is skipped for in-memory databases (not supported there).
    const char* pragmas = path_ == ":memory:"
                              ? "PRAGMA foreign_keys = ON; PRAGMA temp_store = MEMORY;"
                              : "PRAGMA journal_mode = WAL; PRAGMA synchronous = NORMAL; "
                                "PRAGMA cache_size = -8000; PRAGMA temp_store = MEMORY; "
                                "PRAGMA mmap_size = 67108864; PRAGMA foreign_keys = ON;";
    if (auto r = exec(pragmas); !r) {
        close();
        return r;
    }
    if (auto r = migrate(); !r) {
        close();
        return r;
    }
    return {};
}

void SqliteLexiconStore::close() noexcept {
    if (db_ != nullptr) {
        sqlite3_close_v2(db_);
        db_ = nullptr;
    }
}

lk::expected<int> SqliteLexiconStore::schemaVersion() const {
    // A fresh file has no `meta` table yet: that is version 0.
    Statement exists(db_, "SELECT 1 FROM sqlite_master WHERE type='table' AND name='meta'");
    if (!exists.ok()) return lk::unexpected(lastError("schema check"));
    if (exists.step() != SQLITE_ROW) return 0;

    Statement st(db_, "SELECT value FROM meta WHERE key = 'schema_version'");
    if (!st.ok()) return lk::unexpected(lastError("schema version"));
    if (st.step() != SQLITE_ROW) return 0;
    return std::stoi(reinterpret_cast<const char*>(sqlite3_column_text(st.get(), 0)));
}

lk::expected<void> SqliteLexiconStore::migrate() {
    auto current = schemaVersion();
    if (!current) return lk::unexpected(current.error());
    if (*current > latestSchemaVersion()) {
        return lk::unexpected(
            Error::make(Error::Code::Unsupported, "database schema " + std::to_string(*current) +
                                                      " is newer than this build (" +
                                                      std::to_string(latestSchemaVersion()) + ")"));
    }
    if (*current == latestSchemaVersion()) return {};

    // Every pending step in ONE transaction: either the file ends up at the latest version
    // or it is untouched.
    if (auto r = exec("BEGIN IMMEDIATE"); !r) return r;
    for (const auto& m : migrations()) {
        if (m.version <= *current) continue;
        if (auto r = exec(m.sql); !r) {
            (void)exec("ROLLBACK");
            return r;
        }
        const std::string bump = "UPDATE meta SET value = '" + std::to_string(m.version) +
                                 "' WHERE key = 'schema_version'";
        if (auto r = exec(bump.c_str()); !r) {
            (void)exec("ROLLBACK");
            return r;
        }
    }
    return exec("COMMIT");
}

lk::expected<std::vector<LexiconEntry>> SqliteLexiconStore::loadAll() {
    Statement st(db_, "SELECT phrase, frequency, first_seen_at, last_used_at, is_pinned, "
                      "is_blocked FROM user_lexicon");
    if (!st.ok()) return lk::unexpected(lastError("loadAll"));
    std::vector<LexiconEntry> out;
    for (;;) {
        const int rc = st.step();
        if (rc == SQLITE_DONE) break;
        if (rc != SQLITE_ROW) return lk::unexpected(lastError("loadAll step"));
        LexiconEntry e;
        e.phrase = Phrase::fromJoined(
            text::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(st.get(), 0))));
        e.frequency = static_cast<std::uint32_t>(sqlite3_column_int64(st.get(), 1));
        e.firstSeenAt = sqlite3_column_int64(st.get(), 2);
        e.lastUsedAt = sqlite3_column_int64(st.get(), 3);
        e.pinned = sqlite3_column_int(st.get(), 4) != 0;
        e.blocked = sqlite3_column_int(st.get(), 5) != 0;
        out.push_back(std::move(e));
    }
    return out;
}

lk::expected<void> SqliteLexiconStore::applyDeltas(const LexiconDeltaBatch& batch) {
    if (batch.empty()) return {};
    // PLAN 5.5: UPSERT, frequency accumulates, last_used_at moves forward only.
    Statement st(db_, "INSERT INTO user_lexicon (phrase, syllable_count, frequency, "
                      "first_seen_at, last_used_at) VALUES (?, ?, MAX(0, ?), ?, ?) "
                      "ON CONFLICT(phrase) DO UPDATE SET "
                      "frequency = MAX(0, frequency + ?), "
                      "last_used_at = MAX(last_used_at, excluded.last_used_at)");
    if (!st.ok()) return lk::unexpected(lastError("applyDeltas prepare"));

    if (auto r = exec("BEGIN IMMEDIATE"); !r) return r;
    for (const auto& d : batch) {
        bindText(st.get(), 1, text::toUtf8(d.phrase.joined()));
        sqlite3_bind_int(st.get(), 2, d.phrase.syllableCount());
        sqlite3_bind_int(st.get(), 3, d.frequencyDelta);
        sqlite3_bind_int64(st.get(), 4, d.usedAt);
        sqlite3_bind_int64(st.get(), 5, d.usedAt);
        sqlite3_bind_int(st.get(), 6, d.frequencyDelta);
        if (st.step() != SQLITE_DONE) {
            const Error e = lastError("applyDeltas step");
            (void)exec("ROLLBACK");
            return lk::unexpected(e);
        }
        st.reset();
    }
    return exec("COMMIT");
}

lk::expected<void> SqliteLexiconStore::eraseAll() {
    // Delete everything the user ever taught us, then shrink the file so the bytes are not
    // left lying around in free pages, and fold the WAL back in.
    if (auto r = exec("DELETE FROM lexicon_app_context; DELETE FROM user_lexicon; "
                      "DELETE FROM correction_map; DELETE FROM autocorrect_blacklist; "
                      "DELETE FROM user_macros; DELETE FROM app_settings;");
        !r) {
        return r;
    }
    if (auto r = exec("VACUUM"); !r) return r;
    if (path_ != ":memory:") (void)exec("PRAGMA wal_checkpoint(TRUNCATE)");
    return {};
}

lk::expected<int> SqliteLexiconStore::cleanup(std::int64_t nowUnixSeconds) {
    constexpr std::int64_t kDay = 86400;
    // Phrases of 2-3 syllables are far more numerous, so they age out twice as fast.
    const std::string sql =
        "DELETE FROM user_lexicon WHERE is_pinned = 0 AND ("
        "  (frequency = 1 AND last_used_at < " +
        std::to_string(nowUnixSeconds - 90 * kDay) +
        "   AND syllable_count = 1) OR"
        "  (frequency = 1 AND last_used_at < " +
        std::to_string(nowUnixSeconds - 45 * kDay) +
        "   AND syllable_count > 1) OR"
        "  (frequency < 3 AND last_used_at < " +
        std::to_string(nowUnixSeconds - 180 * kDay) +
        "   AND syllable_count = 1) OR"
        "  (frequency < 3 AND last_used_at < " +
        std::to_string(nowUnixSeconds - 90 * kDay) +
        "   AND syllable_count > 1)); "
        // Hard limit: drop the weakest rows beyond kLexiconHardLimit.
        "DELETE FROM user_lexicon WHERE is_pinned = 0 AND id IN ("
        "  SELECT id FROM user_lexicon ORDER BY frequency ASC, last_used_at ASC "
        "  LIMIT MAX(0, (SELECT COUNT(*) FROM user_lexicon) - " +
        std::to_string(Thresholds::kLexiconHardLimit) + "));";
    if (auto r = exec("BEGIN IMMEDIATE"); !r) return lk::unexpected(r.error());
    const int before = sqlite3_total_changes(db_);
    if (auto r = exec(sql.c_str()); !r) {
        (void)exec("ROLLBACK");
        return lk::unexpected(r.error());
    }
    if (auto r = exec("COMMIT"); !r) return lk::unexpected(r.error());
    const int removed = sqlite3_total_changes(db_) - before;
    if (removed > 0) (void)exec("VACUUM");
    return removed;
}

lk::expected<std::vector<model::CorrectionRule>> SqliteLexiconStore::loadCorrections() {
    Statement st(db_, "SELECT wrong_phrase, correct_phrase, confidence, times_applied, "
                      "times_rejected, source, updated_at FROM correction_map");
    if (!st.ok()) return lk::unexpected(lastError("loadCorrections"));
    std::vector<model::CorrectionRule> out;
    for (;;) {
        const int rc = st.step();
        if (rc == SQLITE_DONE) break;
        if (rc != SQLITE_ROW) return lk::unexpected(lastError("loadCorrections step"));
        model::CorrectionRule r;
        r.wrong = columnU32(st.get(), 0);
        r.correct = columnU32(st.get(), 1);
        r.confidence = sqlite3_column_double(st.get(), 2);
        r.timesApplied = sqlite3_column_int(st.get(), 3);
        r.timesRejected = sqlite3_column_int(st.get(), 4);
        r.source = sourceFrom(reinterpret_cast<const char*>(sqlite3_column_text(st.get(), 5)));
        r.updatedAt = sqlite3_column_int64(st.get(), 6);
        out.push_back(std::move(r));
    }
    return out;
}

lk::expected<std::vector<std::u32string>> SqliteLexiconStore::loadBlacklist() {
    Statement st(db_, "SELECT phrase FROM autocorrect_blacklist");
    if (!st.ok()) return lk::unexpected(lastError("loadBlacklist"));
    std::vector<std::u32string> out;
    for (;;) {
        const int rc = st.step();
        if (rc == SQLITE_DONE) break;
        if (rc != SQLITE_ROW) return lk::unexpected(lastError("loadBlacklist step"));
        out.push_back(columnU32(st.get(), 0));
    }
    return out;
}

lk::expected<void> SqliteLexiconStore::reinforceCorrection(std::u32string_view wrong,
                                                           std::u32string_view correct,
                                                           double delta,
                                                           model::CorrectionRuleSource source,
                                                           std::int64_t nowUnixSeconds) {
    // Same target: accumulate. A new target for the same mistake means the old rule was
    // wrong for this user: start over with the new one.
    Statement st(db_, "INSERT INTO correction_map (wrong_phrase, correct_phrase, confidence, "
                      "source, created_at, updated_at) VALUES (?, ?, ?, ?, ?, ?) "
                      "ON CONFLICT(wrong_phrase) DO UPDATE SET "
                      "confidence = CASE WHEN correct_phrase = excluded.correct_phrase "
                      "  THEN MIN(1.0, confidence + excluded.confidence) "
                      "  ELSE excluded.confidence END, "
                      "times_rejected = CASE WHEN correct_phrase = excluded.correct_phrase "
                      "  THEN times_rejected ELSE 0 END, "
                      "correct_phrase = excluded.correct_phrase, "
                      "source = excluded.source, updated_at = excluded.updated_at");
    if (!st.ok()) return lk::unexpected(lastError("reinforceCorrection prepare"));
    bindText(st.get(), 1, text::toUtf8(wrong));
    bindText(st.get(), 2, text::toUtf8(correct));
    sqlite3_bind_double(st.get(), 3, delta);
    sqlite3_bind_text(st.get(), 4, sourceName(source), -1, SQLITE_STATIC);
    sqlite3_bind_int64(st.get(), 5, nowUnixSeconds);
    sqlite3_bind_int64(st.get(), 6, nowUnixSeconds);
    if (st.step() != SQLITE_DONE) return lk::unexpected(lastError("reinforceCorrection step"));
    return {};
}

lk::expected<bool> SqliteLexiconStore::rejectCorrection(std::u32string_view wrong, double penalty,
                                                        std::int64_t nowUnixSeconds) {
    const std::string key = text::toUtf8(wrong);
    if (auto r = exec("BEGIN IMMEDIATE"); !r) return lk::unexpected(r.error());
    const auto fail = [&](const char* what) {
        const Error e = lastError(what);
        (void)exec("ROLLBACK");
        return lk::unexpected(e);
    };
    // A dictionary correction has no rule yet: record the rejection as a zero-confidence
    // rule so the count survives and the second Undo blacklists it like any other.
    {
        Statement st(db_, "INSERT INTO correction_map (wrong_phrase, correct_phrase, confidence, "
                          "source, created_at, updated_at) "
                          "VALUES (?, '', 0.0, 'learned', ?, ?) "
                          "ON CONFLICT(wrong_phrase) DO NOTHING");
        if (!st.ok()) return fail("rejectCorrection insert");
        bindText(st.get(), 1, key);
        sqlite3_bind_int64(st.get(), 2, nowUnixSeconds);
        sqlite3_bind_int64(st.get(), 3, nowUnixSeconds);
        if (st.step() != SQLITE_DONE) return fail("rejectCorrection insert step");
    }
    {
        Statement st(db_, "UPDATE correction_map SET confidence = MAX(0.0, confidence - ?), "
                          "times_rejected = times_rejected + 1, updated_at = ? "
                          "WHERE wrong_phrase = ?");
        if (!st.ok()) return fail("rejectCorrection update");
        sqlite3_bind_double(st.get(), 1, penalty);
        sqlite3_bind_int64(st.get(), 2, nowUnixSeconds);
        bindText(st.get(), 3, key);
        if (st.step() != SQLITE_DONE) return fail("rejectCorrection update step");
    }
    int rejected = 0;
    {
        Statement st(db_, "SELECT times_rejected FROM correction_map WHERE wrong_phrase = ?");
        if (!st.ok()) return fail("rejectCorrection read");
        bindText(st.get(), 1, key);
        if (st.step() == SQLITE_ROW) rejected = sqlite3_column_int(st.get(), 0);
    }
    bool blacklisted = false;
    if (rejected >= Thresholds::kRejectionsBeforeBlacklist) {
        Statement st(db_, "INSERT OR IGNORE INTO autocorrect_blacklist (phrase, reason, "
                          "created_at) VALUES (?, 'user_undo', ?)");
        if (!st.ok()) return fail("rejectCorrection blacklist");
        bindText(st.get(), 1, key);
        sqlite3_bind_int64(st.get(), 2, nowUnixSeconds);
        if (st.step() != SQLITE_DONE) return fail("rejectCorrection blacklist step");
        blacklisted = true;
    }
    if (auto r = exec("COMMIT"); !r) return lk::unexpected(r.error());
    return blacklisted;
}

lk::expected<void> SqliteLexiconStore::noteCorrectionApplied(std::u32string_view wrong) {
    Statement st(db_, "UPDATE correction_map SET times_applied = times_applied + 1 "
                      "WHERE wrong_phrase = ?");
    if (!st.ok()) return lk::unexpected(lastError("noteCorrectionApplied"));
    bindText(st.get(), 1, text::toUtf8(wrong));
    if (st.step() != SQLITE_DONE) return lk::unexpected(lastError("noteCorrectionApplied step"));
    return {};
}

lk::expected<void> SqliteLexiconStore::blacklist(std::u32string_view phrase,
                                                 std::int64_t nowUnixSeconds) {
    Statement st(db_, "INSERT OR IGNORE INTO autocorrect_blacklist (phrase, reason, created_at) "
                      "VALUES (?, 'user_manual', ?)");
    if (!st.ok()) return lk::unexpected(lastError("blacklist"));
    bindText(st.get(), 1, text::toUtf8(phrase));
    sqlite3_bind_int64(st.get(), 2, nowUnixSeconds);
    if (st.step() != SQLITE_DONE) return lk::unexpected(lastError("blacklist step"));
    return {};
}

} // namespace lankey::core::storage
