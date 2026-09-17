#pragma once

#include "core/model/TextReplacement.h"

namespace lankey::core {

// Core -> platform: change the text before the caret in the focused application.
//
// Called on the hook thread, after the generation check has passed. The platform owns
// the strategy (one SendInput batch, key by key, clipboard paste) and the loop guard
// (marking injected keys so the hook ignores them).
class ITextSink {
public:
    virtual ~ITextSink() = default;

    virtual void apply(const model::TextReplacement& replacement) = 0;
};

} // namespace lankey::core
