#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

#include "core/model/Error.h"

namespace lankey::core {

using SecretBytes = std::vector<std::uint8_t>;

// Platform -> core: a per-user secret that never leaves the machine (DPAPI on Windows,
// keychain elsewhere). Used from Phase 3 on to derive the database encryption key.
class IKeyStore {
public:
    virtual ~IKeyStore() = default;

    // Returns the secret identified by `id`, creating and persisting a random one on first
    // use. Worker/DB thread only.
    [[nodiscard]] virtual lk::expected<SecretBytes> loadOrCreate(std::string_view id) = 0;
};

} // namespace lankey::core
