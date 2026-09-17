#pragma once

#include <map>
#include <string>
#include <string_view>

#include "core/interfaces/IKeyStore.h"

namespace lankey::tests {

// In-memory secrets; "creates" a fixed, recognisable secret on first use.
class FakeKeyStore final : public core::IKeyStore {
public:
    [[nodiscard]] lk::expected<core::SecretBytes> loadOrCreate(std::string_view id) override {
        if (failNext_) {
            failNext_ = false;
            return lk::unexpected(core::model::Error::make(core::model::Error::Code::Io,
                                                           "FakeKeyStore: simulated failure"));
        }
        auto& secret = secrets_[std::string(id)];
        if (secret.empty()) {
            secret.assign(32, static_cast<std::uint8_t>(secrets_.size()));
        }
        return secret;
    }

    void failNextCall() { failNext_ = true; }

private:
    std::map<std::string, core::SecretBytes> secrets_;
    bool failNext_ = false;
};

} // namespace lankey::tests
