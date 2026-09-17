#pragma once

#include <functional>

#include "core/model/KeyEvent.h"

namespace lankey::core {

// Platform -> core: the stream of raw key events (win32 low-level hook today, TSF or
// another OS later).
//
// The platform invokes the handler on ITS thread (the hook thread). The handler returns
// true to swallow the key (the application never sees it) or false to let it through.
// The handler must be fast (< 1 ms) and must not throw.
class IKeySource {
public:
    using Handler = std::function<bool(const model::KeyEvent&)>;

    virtual ~IKeySource() = default;

    virtual void setHandler(Handler handler) = 0;
    virtual void start() = 0;
    virtual void stop() = 0;
};

} // namespace lankey::core
