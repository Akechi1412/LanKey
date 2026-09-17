#pragma once

#include <optional>
#include <vector>

#include "core/interfaces/ICorrector.h"

namespace lankey::tests {

// Returns a canned correction (or none) and records what it was asked about.
class FakeCorrector final : public core::ICorrector {
public:
    [[nodiscard]] std::optional<core::model::Correction>
    check(const core::model::SyllableCommitted& committed) const override {
        seen_.push_back(committed);
        return canned_;
    }

    void setCorrection(std::optional<core::model::Correction> c) { canned_ = std::move(c); }
    [[nodiscard]] const std::vector<core::model::SyllableCommitted>& seen() const noexcept {
        return seen_;
    }

private:
    std::optional<core::model::Correction> canned_;
    mutable std::vector<core::model::SyllableCommitted> seen_;
};

} // namespace lankey::tests
