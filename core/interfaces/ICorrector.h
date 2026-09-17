#pragma once

#include <optional>

#include "core/model/Correction.h"
#include "core/model/SyllableCommitted.h"

namespace lankey::core {

// Decides whether the syllable that just ended (or a phrase ending with it) should be
// replaced. Worker thread. The returned replacement carries the generation from the event
// so the hook thread can discard it if the user has typed since.
class ICorrector {
public:
    virtual ~ICorrector() = default;

    [[nodiscard]] virtual std::optional<model::Correction>
    check(const model::SyllableCommitted& committed) const = 0;
};

} // namespace lankey::core
