#pragma once

#include <optional>

#include "core/model/Geometry.h"

namespace lankey::core {

// Platform -> core: where the text caret is on screen, for placing the popup.
//
// Slow (UI Automation can take tens of milliseconds). Worker thread only, and only when a
// popup is about to be shown. nullopt = unknown; the UI falls back to a default spot.
class ICaretResolver {
public:
    virtual ~ICaretResolver() = default;

    [[nodiscard]] virtual std::optional<model::ScreenRect> resolve() = 0;
};

} // namespace lankey::core
