#pragma once

#include <memory>
#include <string>
#include <vector>

#include "core/interfaces/IDataProtector.h"
#include "core/interfaces/ILexiconStore.h"

struct sqlite3;

namespace lankey::core::storage {

// ILexiconStore on SQLite. One file, WAL journal, one transaction per batch.
//
// Threading: DB thread only. open() runs the migrations (schema v1 today) and must succeed
// before any other call. The same behaviour is specified by the tests that also run
// against InMemoryLexiconStore.
//
// Two ways to persist (ADR-012):
//   - plain file: `path` is an ordinary SQLite database (WAL journal, VACUUM on cleanup);
//   - protected (a protector is given): the database lives in memory and `path` holds
//     its image sealed by the protector (DPAPI on Windows). Every write persists the whole
//     image atomically (temp file + rename); nothing readable ever touches the disk and
//     there is no journal to leave behind. A plain file from an earlier version found at
//     `path` minus its ".enc" suffix is imported once and deleted after the first
//     successful persist.
// ":memory:" without a protector is a private in-memory database (tests).
class SqliteLexiconStore final : public ILexiconStore {
public:
    explicit SqliteLexiconStore(std::string path, IDataProtector* protector = nullptr);
    ~SqliteLexiconStore() override;

    SqliteLexiconStore(const SqliteLexiconStore&) = delete;
    SqliteLexiconStore& operator=(const SqliteLexiconStore&) = delete;

    [[nodiscard]] lk::expected<void> open();
    void close() noexcept;
    [[nodiscard]] bool isOpen() const noexcept { return db_ != nullptr; }

    [[nodiscard]] lk::expected<std::vector<model::LexiconEntry>> loadAll() override;
    [[nodiscard]] lk::expected<void> applyDeltas(const model::LexiconDeltaBatch& batch) override;
    [[nodiscard]] lk::expected<void> eraseAll() override;

    // Housekeeping (PLAN 5.6): drop stale low-frequency entries, enforce the hard limit,
    // then VACUUM. Returns the number of rows removed.
    [[nodiscard]] lk::expected<int> cleanup(std::int64_t nowUnixSeconds) override;
    [[nodiscard]] lk::expected<void>
    removeEntries(const std::vector<std::u32string>& phrases) override;
    [[nodiscard]] lk::expected<void> setBlocked(const std::vector<std::u32string>& phrases,
                                                bool blocked) override;
    [[nodiscard]] lk::expected<void> setPinned(const std::vector<std::u32string>& phrases,
                                               bool pinned) override;
    [[nodiscard]] lk::expected<std::vector<model::CorrectionRule>> loadCorrections() override;
    [[nodiscard]] lk::expected<std::vector<std::u32string>> loadBlacklist() override;
    [[nodiscard]] lk::expected<void> reinforceCorrection(std::u32string_view wrong,
                                                         std::u32string_view correct, double delta,
                                                         model::CorrectionRuleSource source,
                                                         std::int64_t nowUnixSeconds) override;
    [[nodiscard]] lk::expected<bool> rejectCorrection(std::u32string_view wrong, double penalty,
                                                      std::int64_t nowUnixSeconds) override;
    [[nodiscard]] lk::expected<void> noteCorrectionApplied(std::u32string_view wrong) override;
    [[nodiscard]] lk::expected<void> blacklist(std::u32string_view phrase,
                                               std::int64_t nowUnixSeconds) override;

    [[nodiscard]] lk::expected<int> schemaVersion() const;
    [[nodiscard]] const std::string& path() const noexcept { return path_; }
    [[nodiscard]] bool isProtected() const noexcept { return protector_ != nullptr; }

    // Writes the in-memory image to disk (protected mode; no-op otherwise). Called by every
    // mutating operation; public so the owner can force it (shutdown, tests).
    [[nodiscard]] lk::expected<void> persist();

private:
    [[nodiscard]] lk::expected<void>
    forEachPhrase(const char* sql, const std::vector<std::u32string>& phrases, int flagValue);
    [[nodiscard]] lk::expected<void> exec(const char* sql) const;
    [[nodiscard]] lk::expected<void> migrate();
    [[nodiscard]] lk::expected<void> openProtected();
    [[nodiscard]] lk::expected<void> loadImage(const std::string& file);
    [[nodiscard]] lk::expected<void> importLegacy(const std::string& file);
    [[nodiscard]] model::Error lastError(const char* what) const;

    std::string path_;
    IDataProtector* protector_ = nullptr;
    std::string legacyToRemove_; // plain file imported at open(); deleted after persist()
    sqlite3* db_ = nullptr;
};

} // namespace lankey::core::storage
