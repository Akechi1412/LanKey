#pragma once

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include "core/interfaces/IDataProtector.h"

namespace lankey::tests {

// Reversible, obviously-not-secure sealing for tests: a marker plus every byte XORed with
// a constant, so a sealed image never contains the plaintext and a wrong scheme or a
// simulated failure can be exercised.
class FakeDataProtector final : public core::IDataProtector {
public:
    [[nodiscard]] lk::expected<std::vector<std::uint8_t>>
    protect(std::span<const std::uint8_t> plain) override {
        if (failNext_) {
            failNext_ = false;
            return lk::unexpected(core::model::Error::make(core::model::Error::Code::Io,
                                                           "FakeDataProtector: simulated failure"));
        }
        std::vector<std::uint8_t> out(kMarker.begin(), kMarker.end());
        for (const std::uint8_t b : plain)
            out.push_back(static_cast<std::uint8_t>(b ^ kMask));
        ++protectCalls;
        return out;
    }

    [[nodiscard]] lk::expected<std::vector<std::uint8_t>>
    unprotect(std::span<const std::uint8_t> sealed) override {
        if (sealed.size() < kMarker.size() ||
            std::string_view(reinterpret_cast<const char*>(sealed.data()), kMarker.size()) !=
                kMarker) {
            return lk::unexpected(core::model::Error::make(core::model::Error::Code::Corrupted,
                                                           "FakeDataProtector: bad marker"));
        }
        std::vector<std::uint8_t> out;
        out.reserve(sealed.size() - kMarker.size());
        for (std::size_t i = kMarker.size(); i < sealed.size(); ++i)
            out.push_back(static_cast<std::uint8_t>(sealed[i] ^ kMask));
        ++unprotectCalls;
        return out;
    }

    [[nodiscard]] std::string_view scheme() const noexcept override { return "fake"; }

    void failNextCall() { failNext_ = true; }

    int protectCalls = 0;
    int unprotectCalls = 0;

private:
    static constexpr std::string_view kMarker = "FAKESEAL";
    static constexpr std::uint8_t kMask = 0x5A;
    bool failNext_ = false;
};

} // namespace lankey::tests
