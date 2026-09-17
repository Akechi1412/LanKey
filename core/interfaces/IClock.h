#pragma once

#include <cstdint>

namespace lankey::core {

// Time source, injected so tests can drive recency decay, the Undo window and flush
// timers deterministically (FakeClock).
class IClock {
public:
    virtual ~IClock() = default;

    // Wall clock, for timestamps stored in the lexicon.
    [[nodiscard]] virtual std::int64_t nowUnixSeconds() const = 0;
    // Monotonic, for timeouts and intervals; never goes backwards.
    [[nodiscard]] virtual std::int64_t nowMonotonicMs() const = 0;
};

} // namespace lankey::core
