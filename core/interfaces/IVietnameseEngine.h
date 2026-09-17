#pragma once

#include "core/model/ComposedText.h"
#include "core/model/EngineSettings.h"
#include "core/model/KeyEvent.h"

namespace lankey::core {

// The single contract between LanKey and the upstream Telex/VNI engine (OpenKey).
//
// Everything else in LanKey includes only this header and NEVER an upstream header.
// Swapping engines = writing a new adapter in core/engine/ and changing one line in
// app/Composition.cpp. See README, "Thêm engine mới".
//
// Threading: process() and reset() are called on the hook thread -> no I/O, no locks,
// no large allocations. configure() is called on that same thread when the user changes
// settings. The adapter must hide any global mutable state of the upstream: the rest of
// LanKey has no singletons and no global mutable state.
class IVietnameseEngine {
public:
    virtual ~IVietnameseEngine() = default;

    // Process one key. The adapter must ignore KeyEvents with injectedBySelf = true and
    // return PassThrough (in case the pipeline did not filter them first).
    [[nodiscard]] virtual model::EngineResult process(const model::KeyEvent& key) = 0;

    // Forget the syllable being composed. Called on: space/punctuation, mouse click,
    // focus change, navigation keys, Alt+Tab. Produces no output.
    virtual void reset() = 0;

    virtual void configure(const model::EngineSettings& settings) = 0;
    [[nodiscard]] virtual const model::EngineSettings& settings() const = 0;
};

} // namespace lankey::core
