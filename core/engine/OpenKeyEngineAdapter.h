#pragma once

#include <string>

#include "core/interfaces/IVietnameseEngine.h"

namespace lankey::core::engine {

// Wraps the OpenKey engine (third_party/engine-openkey) behind IVietnameseEngine.
//
// OpenKey keeps ALL of its state in globals (typing buffer, settings, result struct), so
// only one adapter instance may be alive per process; the constructor enforces that.
// The engine reports its result as {backspaceCount, charData[]} in a global hook-state
// struct; the adapter translates that into an EngineResult and mirrors the on-screen
// syllable so ComposedText can be reported (the engine never exposes it directly).
// Only this file and its .cpp may include upstream headers (see README, "Kiến trúc").
//
// Threading: same as IVietnameseEngine - called on the hook thread only.
class OpenKeyEngineAdapter final : public IVietnameseEngine {
public:
    OpenKeyEngineAdapter();
    ~OpenKeyEngineAdapter() override;

    OpenKeyEngineAdapter(const OpenKeyEngineAdapter&) = delete;
    OpenKeyEngineAdapter& operator=(const OpenKeyEngineAdapter&) = delete;

    [[nodiscard]] model::EngineResult process(const model::KeyEvent& key) override;
    void reset() override;
    void configure(const model::EngineSettings& settings) override;
    [[nodiscard]] const model::EngineSettings& settings() const override { return settings_; }

private:
    [[nodiscard]] model::EngineResult passThrough() const;
    void applySettingsToGlobals() const;

    model::EngineSettings settings_;
    // Mirror of what the application shows for the syllable being composed.
    std::u32string onScreen_;
    bool transformApplied_ = false;
};

}  // namespace lankey::core::engine
