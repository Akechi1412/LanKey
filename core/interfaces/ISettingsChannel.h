#pragma once

#include <functional>

#include "core/model/Error.h"
#include "core/model/Settings.h"

namespace lankey::core {

// How the Settings UI talks to the core. v0.1: in-process direct calls. Later: a named-pipe
// implementation when Settings moves to its own process - the core does not change.
//
// Kept deliberately small: read, write, and be told about writes. Lexicon editing goes
// through ILexiconStore, not here.
class ISettingsChannel {
public:
    using ChangeHandler = std::function<void(const model::Settings&)>;

    virtual ~ISettingsChannel() = default;

    [[nodiscard]] virtual model::Settings get() const = 0;
    [[nodiscard]] virtual lk::expected<void> set(const model::Settings& settings) = 0;
    virtual void onChange(ChangeHandler handler) = 0;
};

} // namespace lankey::core
