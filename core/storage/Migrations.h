#pragma once

#include <span>

namespace lankey::core::storage {

// One schema step. Applied in order inside a single transaction from the current
// schema_version up to the latest. NEVER edit an existing migration: add a new one.
struct Migration {
    int version;     // schema_version after this step
    const char* sql; // may contain several statements
};

[[nodiscard]] std::span<const Migration> migrations() noexcept;
[[nodiscard]] int latestSchemaVersion() noexcept;

} // namespace lankey::core::storage
