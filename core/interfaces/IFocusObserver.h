#pragma once

#include <functional>
#include <memory>

#include "core/model/FocusContext.h"

namespace lankey::core {

// Platform -> core: which application/control has keyboard focus.
//
// current() returns an immutable snapshot and is safe to call on the hook thread on
// every key. The change callback is invoked on the platform's own thread whenever focus
// moves; InputPipeline uses it to reset the composition and bump the generation.
class IFocusObserver {
public:
    using ChangeHandler = std::function<void(const model::FocusContext&)>;

    virtual ~IFocusObserver() = default;

    [[nodiscard]] virtual std::shared_ptr<const model::FocusContext> current() const = 0;
    virtual void onChange(ChangeHandler handler) = 0;
};

} // namespace lankey::core
