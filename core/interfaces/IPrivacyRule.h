#pragma once

#include "core/model/SyllableCommitted.h"

namespace lankey::core {

// One privacy check. PrivacyFilter runs every rule in order and stops at the first
// verdict that is not Accept; adding a rule never means editing another.
//
// Verdicts are split because English-looking text must still be LEARNED ("gửi email")
// but never AUTO-CORRECTED; whereas a password field must not be touched at all.
enum class PrivacyVerdict {
    Accept,        // learn, suggest, correct
    NoAutoCorrect, // learn and suggest, but never correct
    Reject,        // forget this event entirely
};

class IPrivacyRule {
public:
    virtual ~IPrivacyRule() = default;

    // Worker thread. Must not keep a reference to the event.
    [[nodiscard]] virtual PrivacyVerdict
    evaluate(const model::SyllableCommitted& committed) const = 0;
};

} // namespace lankey::core
