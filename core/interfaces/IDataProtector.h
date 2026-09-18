#pragma once

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include "core/model/Error.h"

namespace lankey::core {

// Platform -> core: seals a byte image so that only this user on this machine can open
// it again (DPAPI on Windows). The lexicon database lives in memory and is written to
// disk only through protect(); a copied file is useless elsewhere (PLAN 8.3, option A).
//
// DB thread only. Implementations must never log or retain the plaintext.
class IDataProtector {
public:
    virtual ~IDataProtector() = default;

    [[nodiscard]] virtual lk::expected<std::vector<std::uint8_t>>
    protect(std::span<const std::uint8_t> plain) = 0;
    [[nodiscard]] virtual lk::expected<std::vector<std::uint8_t>>
    unprotect(std::span<const std::uint8_t> sealed) = 0;

    // Short identifier written into the file header, so a file sealed by one scheme is
    // never fed to another ("dpapi", "fake").
    [[nodiscard]] virtual std::string_view scheme() const noexcept = 0;
};

// Overwrites secret bytes in a way the optimiser may not elide.
void secureZero(void* p, std::size_t n) noexcept;

} // namespace lankey::core
