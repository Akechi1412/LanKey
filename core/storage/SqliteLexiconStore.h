#pragma once

#include <memory>
#include <string>
#include <vector>

#include "core/interfaces/ILexiconStore.h"

struct sqlite3;

namespace lankey::core::storage {

// ILexiconStore on SQLite. One file, WAL journal, one transaction per batch.
//
// Threading: DB thread only. open() runs the migrations (schema v1 today) and must succeed
// before any other call. The same behaviour is specified by the tests that also run
// against InMemoryLexiconStore.
//
// Phase 3 swaps the sqlite3 build for SQLCipher and adds a key from IKeyStore; the public
// surface of this class does not change.
class SqliteLexiconStore final : public ILexiconStore {
public:
    // ":memory:" gives a private in-memory database (tests).
    explicit SqliteLexiconStore(std::string path);
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

    [[nodiscard]] lk::expected<int> schemaVersion() const;
    [[nodiscard]] const std::string& path() const noexcept { return path_; }

private:
    [[nodiscard]] lk::expected<void> exec(const char* sql) const;
    [[nodiscard]] lk::expected<void> migrate();
    [[nodiscard]] model::Error lastError(const char* what) const;

    std::string path_;
    sqlite3* db_ = nullptr;
};

} // namespace lankey::core::storage
