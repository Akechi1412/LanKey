#pragma once

#include <chrono>

#include "core/interfaces/IClock.h"

namespace lankey::core::util {

// The real clock. Pure std::chrono, so it lives in core and works everywhere.
class SystemClock final : public IClock {
public:
    [[nodiscard]] std::int64_t nowUnixSeconds() const override {
        return std::chrono::duration_cast<std::chrono::seconds>(
                   std::chrono::system_clock::now().time_since_epoch())
            .count();
    }
    [[nodiscard]] std::int64_t nowMonotonicMs() const override {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::steady_clock::now().time_since_epoch())
            .count();
    }
};

} // namespace lankey::core::util
