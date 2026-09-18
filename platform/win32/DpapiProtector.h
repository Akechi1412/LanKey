#pragma once

#include "core/interfaces/IDataProtector.h"

namespace lankey::platform::win32 {

// IDataProtector over the Windows Data Protection API (CryptProtectData, user scope):
// the sealed image can only be opened by the same Windows account on the same machine,
// no key file to manage, no prompt. Tied to the account, not to a master password
// (PLAN 8.3 option A); a copied %APPDATA% is useless elsewhere.
//
// DB thread only. Plaintext buffers handed back by DPAPI are wiped before release.
class DpapiProtector final : public core::IDataProtector {
public:
    [[nodiscard]] lk::expected<std::vector<std::uint8_t>>
    protect(std::span<const std::uint8_t> plain) override;
    [[nodiscard]] lk::expected<std::vector<std::uint8_t>>
    unprotect(std::span<const std::uint8_t> sealed) override;
    [[nodiscard]] std::string_view scheme() const noexcept override { return "dpapi"; }
};

} // namespace lankey::platform::win32
