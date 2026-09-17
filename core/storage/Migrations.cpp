#include "core/storage/Migrations.h"

namespace lankey::core::storage {

namespace {

// v1: the unit of storage is a PHRASE of 1..3 syllables (PLAN 5.3). `phrase` is NFC,
// case-folded, syllables separated by exactly one space.
constexpr const char* kV1 = R"sql(
CREATE TABLE user_lexicon (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    phrase          TEXT    NOT NULL,
    phrase_nosign   TEXT    NOT NULL,
    syllable_count  INTEGER NOT NULL CHECK (syllable_count BETWEEN 1 AND 3),
    frequency       INTEGER NOT NULL DEFAULT 1,
    first_seen_at   INTEGER NOT NULL,
    last_used_at    INTEGER NOT NULL,
    is_pinned       INTEGER NOT NULL DEFAULT 0,
    is_blocked      INTEGER NOT NULL DEFAULT 0
);
CREATE UNIQUE INDEX idx_lexicon_phrase ON user_lexicon(phrase);
CREATE INDEX        idx_lexicon_nosign ON user_lexicon(phrase_nosign);
CREATE INDEX        idx_lexicon_rank   ON user_lexicon(frequency DESC, last_used_at DESC);

CREATE TABLE lexicon_app_context (
    lexicon_id INTEGER NOT NULL REFERENCES user_lexicon(id) ON DELETE CASCADE,
    app_name   TEXT    NOT NULL,
    frequency  INTEGER NOT NULL DEFAULT 1,
    PRIMARY KEY (lexicon_id, app_name)
);

CREATE TABLE correction_map (
    id             INTEGER PRIMARY KEY AUTOINCREMENT,
    wrong_phrase   TEXT    NOT NULL,
    correct_phrase TEXT    NOT NULL,
    confidence     REAL    NOT NULL DEFAULT 0.2,
    times_applied  INTEGER NOT NULL DEFAULT 0,
    times_rejected INTEGER NOT NULL DEFAULT 0,
    source         TEXT    NOT NULL CHECK (source IN ('learned','user','builtin')),
    created_at     INTEGER NOT NULL,
    updated_at     INTEGER NOT NULL
);
CREATE UNIQUE INDEX idx_correction_wrong ON correction_map(wrong_phrase);

CREATE TABLE autocorrect_blacklist (
    phrase     TEXT PRIMARY KEY,
    reason     TEXT NOT NULL CHECK (reason IN ('user_undo','user_manual')),
    created_at INTEGER NOT NULL
);

CREATE TABLE user_macros (
    id         INTEGER PRIMARY KEY AUTOINCREMENT,
    shortcut   TEXT NOT NULL UNIQUE,
    expansion  TEXT NOT NULL,
    created_at INTEGER NOT NULL
);

CREATE TABLE app_settings (
    app_name        TEXT PRIMARY KEY,
    input_mode      INTEGER,
    suggestion_on   INTEGER NOT NULL DEFAULT 1,
    autocorrect_on  INTEGER NOT NULL DEFAULT 1,
    learning_on     INTEGER NOT NULL DEFAULT 1
);

CREATE TABLE meta (key TEXT PRIMARY KEY, value TEXT NOT NULL);
INSERT INTO meta VALUES ('schema_version', '1');
)sql";

// v2: phrases grow from 3 to 5 syllables so "hệ điều hành windows" can be learned and
// predicted as one unit. SQLite cannot ALTER a CHECK constraint; rebuild the table.
constexpr const char* kV2 = R"sql(
CREATE TABLE user_lexicon_v2 (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    phrase          TEXT    NOT NULL,
    phrase_nosign   TEXT    NOT NULL,
    syllable_count  INTEGER NOT NULL CHECK (syllable_count BETWEEN 1 AND 5),
    frequency       INTEGER NOT NULL DEFAULT 1,
    first_seen_at   INTEGER NOT NULL,
    last_used_at    INTEGER NOT NULL,
    is_pinned       INTEGER NOT NULL DEFAULT 0,
    is_blocked      INTEGER NOT NULL DEFAULT 0
);
INSERT INTO user_lexicon_v2 SELECT * FROM user_lexicon;
DROP TABLE user_lexicon;
ALTER TABLE user_lexicon_v2 RENAME TO user_lexicon;
CREATE UNIQUE INDEX idx_lexicon_phrase ON user_lexicon(phrase);
CREATE INDEX        idx_lexicon_nosign ON user_lexicon(phrase_nosign);
CREATE INDEX        idx_lexicon_rank   ON user_lexicon(frequency DESC, last_used_at DESC);
)sql";

// v3: phrase_nosign was written on every UPSERT and indexed, but nothing ever read it -
// diacritic-less matching, when it comes, is done in RAM from the snapshot. Dropping the
// column and index removes a third of the row bytes and one index update per write.
constexpr const char* kV3 = R"sql(
DROP INDEX idx_lexicon_nosign;
ALTER TABLE user_lexicon DROP COLUMN phrase_nosign;
)sql";

constexpr Migration kMigrations[] = {
    {1, kV1},
    {2, kV2},
    {3, kV3},
};

} // namespace

std::span<const Migration> migrations() noexcept {
    return kMigrations;
}

int latestSchemaVersion() noexcept {
    return kMigrations[std::size(kMigrations) - 1].version;
}

} // namespace lankey::core::storage
