#pragma once

#include <string>

#include "core/model/Error.h"
#include "core/model/Settings.h"

namespace lankey::core::storage {

// settings.json <-> model::Settings. Unknown keys are ignored, missing keys keep their
// defaults, so old files load in newer builds and vice versa (within reason: schemaVersion
// is written for future migrations).
//
// Threading: main/UI thread. Files are written atomically (temp file + rename).
class JsonSettingsStore {
public:
    explicit JsonSettingsStore(std::string path);

    // Missing file = defaults (not an error). Corrupt file = Error::Code::Corrupted.
    [[nodiscard]] lk::expected<model::Settings> load() const;
    [[nodiscard]] lk::expected<void> save(const model::Settings& settings) const;

    [[nodiscard]] static std::string serialize(const model::Settings& settings);
    [[nodiscard]] static lk::expected<model::Settings> parse(const std::string& json);

    [[nodiscard]] const std::string& path() const noexcept { return path_; }

private:
    std::string path_;
};

} // namespace lankey::core::storage
