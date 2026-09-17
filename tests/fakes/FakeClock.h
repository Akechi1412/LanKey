#pragma once

#include <cstdint>

#include "core/interfaces/IClock.h"

namespace lankey::tests {

// Deterministic clock: time only moves when the test says so.
class FakeClock final : public core::IClock {
public:
    [[nodiscard]] std::int64_t nowUnixSeconds() const override { return unixSeconds_; }
    [[nodiscard]] std::int64_t nowMonotonicMs() const override { return monotonicMs_; }

    void advanceMs(std::int64_t ms) {
        monotonicMs_ += ms;
        unixSeconds_ += ms / 1000;
    }
    void setUnixSeconds(std::int64_t s) { unixSeconds_ = s; }
    void setMonotonicMs(std::int64_t ms) { monotonicMs_ = ms; }

private:
    std::int64_t unixSeconds_ = 1'700'000'000; // an arbitrary fixed "now"
    std::int64_t monotonicMs_ = 0;
};

} // namespace lankey::tests
